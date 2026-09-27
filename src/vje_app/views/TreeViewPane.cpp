//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TreeViewPane implementation -- view configuration, the two-way selection bridge, expansion commands, the context
//   menu, and view-state restoration across a projection rebuild. See the header for the division of responsibility.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/TreeViewPane.hpp"

#include "AppConfig.hpp"
#include "models/JsonTreeModel.hpp"
#include "services/IconLibrary.hpp"
#include "services/StatusService.hpp"
#include "style/FocusHighlight.hpp"
#include "views/PaneHeader.hpp"
#include "views/ReorderTreeView.hpp"
#include "views/TreeNodeDelegate.hpp"

#include <vje_core/document/JsonDocument.hpp>

#include <QAction>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QModelIndex>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	TreeViewPane::TreeViewPane
	(
		JsonDocument*     document,
		SelectionService* selection,
		IconLibrary*      icons,
		StatusService*    status,
		QWidget*          parent
	)
		: QWidget   ( parent )
		, selection ( selection )
		, status    ( status )
	{
		treeModel = new JsonTreeModel ( document, icons, this );

		ReorderTreeView* const reorderView = new ReorderTreeView ( this );

		treeView = reorderView;
		treeView->setObjectName ( QStringLiteral ( "treeView" ) );

		connect ( reorderView, &ReorderTreeView::nodes_dropped, this, &TreeViewPane::apply_drop );

		// CONNECTED BEFORE setModel, and the order is the whole point. QItemSelectionModel subscribes to the same
		// signal when the view builds it inside setModel, and its own fallback moves a dying current row to the row
		// ABOVE -- so a slot connected afterwards is handed a highlight that has already left the run being removed
		// and can no longer tell it was ever in it. Connecting first is what lets NAV-03's landing rule be ours (see
		// handle_rows_about_to_be_removed); everything else this pane observes may be wired in the usual place below.

		connect ( treeModel, &QAbstractItemModel::rowsAboutToBeRemoved, this, &TreeViewPane::handle_rows_about_to_be_removed );

		treeView->setModel ( treeModel );

		// NFR-05. The Explorer band names this pane on screen, but a band is a sibling widget and a screen reader
		// reading the tree has no reason to have visited it -- so the control says what it is itself.

		treeView->setAccessibleName ( tr ( "Document tree" ) );

		// -- View configuration ---------------------------------------------------------------------------------------

		treeView->setHeaderHidden ( true );                                    // One column of key labels (TREE-02).
		treeView->setSelectionBehavior ( QAbstractItemView::SelectRows );

		// TREE-09. Ctrl, Shift, Ctrl+Space and Shift+arrow are entirely QAbstractItemView's under this mode -- what
		// this pane adds is the same-parent rule they are checked against (handle_tree_selection_changed).
		//
		// Everything single-select depended on survives it: setCurrentIndex with no event and no modifier resolves to
		// ClearAndSelect | Rows under ExtendedSelection exactly as it did under SingleSelection, so every existing
		// caller -- the reveal path, the projection restore, the context-row rule -- still collapses the set to the
		// one row it names.

		treeView->setSelectionMode ( QAbstractItemView::ExtendedSelection );
		treeView->setEditTriggers ( QAbstractItemView::NoEditTriggers );       // Renaming is a command (EDIT-02).
		treeView->setIndentation ( config::tree::INDENTATION );
		treeView->setExpandsOnDoubleClick ( true );
		treeView->setAnimated ( false );
		treeView->setContextMenuPolicy ( Qt::CustomContextMenu );
		treeView->setIconSize ( QSize ( config::tree::ICON_SIZE, config::tree::ICON_SIZE ) );

		// TREE-10's unsaved-change dot, painted after the label by the tree's own delegate -- the decoration role is the
		// type glyph's (TREE-03). Where the dots go is SET-14's, which the window pushes into the model.

		nodeDelegate = new TreeNodeDelegate ( treeView );

		treeView->setItemDelegate ( nodeDelegate );

		// Tab belongs to the workspace, not to the tree (NAV-04). QAbstractItemView defaults this ON, which would make
		// Tab a second, redundant way of moving the highlight one row -- and would swallow the key before the focus
		// cycle ever saw it.

		treeView->setTabKeyNavigation ( false );

		// The virtualization contract (TREE-08). Uniform row heights let QTreeView compute the visible range
		// arithmetically instead of measuring every row, which is what keeps a large document scrollable; combined with
		// the model's lazy population, an unexpanded branch costs one row.

		treeView->setUniformRowHeights ( true );

		// EDIT-10. DragDrop rather than InternalMove, which is the mode whose name matches the gesture: InternalMove
		// forces Qt::MoveAction, and MoveAction is precisely the value that makes QAbstractItemView remove the dragged
		// rows itself -- the branch ReorderTreeView and JsonTreeModel::supportedDropActions exist to keep unreachable,
		// because the removal here is half of an undoable command.

		treeView->setDragEnabled ( true );
		treeView->setAcceptDrops ( true );
		treeView->setDropIndicatorShown ( true );
		treeView->setDragDropMode ( QAbstractItemView::DragDrop );
		treeView->setDefaultDropAction ( Qt::CopyAction );

		// Horizontal scrolling per pixel rather than per item, so a deep path in a narrow pane pans smoothly.

		treeView->setHorizontalScrollMode ( QAbstractItemView::ScrollPerPixel );
		treeView->header ()->setStretchLastSection ( false );
		treeView->header ()->setSectionResizeMode ( 0, QHeaderView::ResizeToContents );

		// The selection bar answers to keyboard focus, so its colour says which pane the next keystroke reaches
		// (STYLE-12). Under the default "Edit on: Double click" a single click leaves the keyboard here, so the tree
		// stays lit and mutes on the double click that moves the caret into the form. Under the non-default Single
		// click setting it mutes on the first click instead -- either way, the colour is telling the truth.

		FocusHighlight::install ( treeView );

		// -- Composition ----------------------------------------------------------------------------------------------

		paneHeader = new PaneHeader ( tr ( "Explorer" ), this );

		// The band recedes when the pane loses the keyboard (STYLE-14). Scoped to the PANE, not to the header: the
		// header never takes focus itself, so asking about its own would leave it permanently receded.
		//
		// The watcher is also the band's FOCUS ORACLE: PaneHeader now paints its surface from style/tab_surface.hpp,
		// which needs the boolean rather than a palette role, so that the band and the editor pane's selected tab come
		// from one named rule instead of from Fusion's private tab gradient (STYLE-13).

		FocusHighlight* const surfaceWatcher = FocusHighlight::install ( paneHeader, FocusRoles::Surface, this );

		if ( surfaceWatcher != nullptr )
		{
			connect
			(
				surfaceWatcher, &FocusHighlight::focus_state_changed,
				paneHeader,     &PaneHeader::set_pane_focused
			);

			paneHeader->set_pane_focused ( surfaceWatcher->holds_focus () );
		}

		// Clicking the band gives the TREE the keyboard, not the band (PaneHeader::clicked). A click on a pane's chrome
		// is a request to work in that pane, and the band has nothing to arrow through -- focusing it would strand the
		// keyboard on a title.

		connect ( paneHeader, &PaneHeader::clicked, this, &TreeViewPane::take_focus );

		QVBoxLayout* paneLayout = new QVBoxLayout ( this );

		paneLayout->setContentsMargins ( 0, 0, 0, 0 );
		paneLayout->setSpacing ( 0 );
		paneLayout->addWidget ( paneHeader );
		paneLayout->addWidget ( treeView );

		// -- Wiring ---------------------------------------------------------------------------------------------------

		connect
		(
			treeView->selectionModel (), &QItemSelectionModel::selectionChanged,
			this, &TreeViewPane::handle_tree_selection_changed
		);

		connect ( selection, &SelectionService::selection_changed, this, &TreeViewPane::handle_service_selection_changed );
		connect ( selection, &SelectionService::selection_cleared, this, &TreeViewPane::handle_service_selection_cleared );

		connect ( treeModel, &QAbstractItemModel::rowsRemoved,            this, &TreeViewPane::handle_rows_removed );

		connect ( treeModel, &QAbstractItemModel::modelReset,             this, &TreeViewPane::handle_model_reset );
		connect ( treeModel, &JsonTreeModel::projection_about_to_rebuild, this, &TreeViewPane::handle_projection_capture );
		connect ( treeModel, &JsonTreeModel::projection_rebuilt,          this, &TreeViewPane::handle_projection_restore );

		connect ( treeView, &QTreeView::clicked,                 this, &TreeViewPane::handle_clicked );
		connect ( treeView, &QTreeView::activated,              this, &TreeViewPane::handle_activated );
		connect ( treeView, &QWidget::customContextMenuRequested, this, &TreeViewPane::handle_context_menu );

		// The KEYBOARD route into the same menu (the Menu key, Shift+F10). It cannot go through the signal above: Qt
		// hands a custom context menu nothing but a position, and only the event carries the REASON -- so the filter is
		// what tells the two routes apart. See eventFilter.

		treeView->installEventFilter ( this );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QTreeView* TreeViewPane::view () const
	{
		return treeView;
	}

	JsonTreeModel* TreeViewPane::model () const
	{
		return treeModel;
	}

	PaneHeader* TreeViewPane::header () const
	{
		return paneHeader;
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void TreeViewPane::set_change_mark_colour ( const QColor& colour )
	{
		nodeDelegate->set_mark_colour ( colour );

		treeView->viewport ()->update ();
	}

	void TreeViewPane::take_focus ()
	{
		// The tree, not the pane: the pane is a layout container with no current row, so focusing it would strand the
		// keyboard somewhere the arrow keys mean nothing (NAV-04).

		treeView->setFocus ( Qt::TabFocusReason );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void TreeViewPane::set_context_actions ( const NodeContextActions& actions )
	{
		contextActions = actions;

		// Scope the node-operation shortcuts to the tree. A window-wide Delete / F2 / Ctrl+D would fire while the
		// user is typing in a form field or the Code View; adding the action to this widget with
		// WidgetWithChildrenShortcut means the key only reaches it when the tree (or a child of it) has focus, while the
		// menu bar's copy of the same QAction still works by click from anywhere.
		//
		// Cut / copy / paste are deliberately NOT scoped here: those keys are shared three ways (nodes, table cells,
		// ordinary text) and the routing is Phase 9's to settle.

		const QList<QAction*> treeScopedActions = { contextActions.rename, contextActions.remove, contextActions.duplicate };

		for ( QAction* action : treeScopedActions )
		{
			if ( action == nullptr )
			{
				continue;
			}

			action->setShortcutContext ( Qt::WidgetWithChildrenShortcut );

			treeView->addAction ( action );
		}
	}

	void TreeViewPane::set_view_commands ( const TreeViewCommands& commands )
	{
		viewCommands = commands;
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void TreeViewPane::expand_all ()
	{
		// TREE-05. This materializes the whole projection by design -- the user asked to see all of it. On a very large
		// document that is the one operation the lazy model cannot make cheap, so it runs under a wait cursor; its cost
		// against the NFR-02/03 document sizes is measured in Phase 17.

		QGuiApplication::setOverrideCursor ( Qt::WaitCursor );

		treeView->expandAll ();

		QGuiApplication::restoreOverrideCursor ();
	}

	void TreeViewPane::collapse_all ()
	{
		treeView->collapseAll ();

		// collapseAll closes the file node too, which leaves the pane looking empty. Keep the root open so the document
		// is still visibly there.

		treeView->expand ( treeModel->index ( 0, 0 ) );
	}

	bool TreeViewPane::current_row_is_branch () const
	{
		const QModelIndex current = treeView->currentIndex ();

		return current.isValid () && treeModel->hasChildren ( current );
	}

	void TreeViewPane::expand_current_subtree ()
	{
		const QModelIndex current = treeView->currentIndex ();

		if ( current.isValid () )
		{
			treeView->expandRecursively ( current );
		}
	}

	void TreeViewPane::collapse_current_subtree ()
	{
		const QModelIndex current = treeView->currentIndex ();

		if ( !current.isValid () )
		{
			return;
		}

		// Collapse descendants before the node itself, so re-expanding it shows a closed subtree rather than the
		// expansion state it had before.

		QList<QModelIndex> pending { current };

		while ( !pending.isEmpty () )
		{
			const QModelIndex index = pending.takeLast ();

			for ( int row = 0; row < treeModel->rowCount ( index ); ++row )
			{
				const QModelIndex child = treeModel->index ( row, 0, index );

				if ( treeView->isExpanded ( child ) )
				{
					pending.append ( child );
				}
			}

			treeView->collapse ( index );
		}
	}

	//=================================================================================================================
	// Handlers -- selection
	//=================================================================================================================

	void TreeViewPane::handle_tree_selection_changed ()
	{
		// Suppressed while the pane is applying a selection that came from the service, so the two directions cannot
		// ping-pong.

		if ( applyingServiceSelection )
		{
			return;
		}

		// Q22's array side: inside a removal window this signal is the selection model moving a dying current row to
		// a sibling, and an array element's pointer CANNOT be answered yet -- the shadow row is pre-removal while the
		// document is already post-removal, so the pointer would be stale by the size of the removed run (an object
		// member survives via its stored key; an element's token IS its position). The MODEL is asked, not a flag of
		// our own -- the selection model connected to rowsAboutToBeRemoved first (at setModel time), so its fallback
		// move runs before any slot of ours could raise one. Hold the publication until rowsRemoved, when the shadow
		// has renumbered and the same publication is exact.

		if ( treeModel->removal_in_progress () )
		{
			publicationDeferredByRemoval = true;

			return;
		}

		// TREE-09's one constraint, checked before anything is published: a set spanning two parents is refused
		// WHOLE, so the previous selection stands and nothing downstream ever sees the crossing set.

		if ( !selection_is_within_one_parent () )
		{
			restore_selection_from_service ();
			report_refused_extension ();

			return;
		}

		publish_tree_selection ();
	}

	void TreeViewPane::publish_tree_selection ()
	{
		QItemSelectionModel* const selectionModel = treeView->selectionModel ();

		const QModelIndex current = selectionModel->currentIndex ();

		QModelIndexList rows = selectionModel->selectedRows ();

		// A current row that is not SELECTED still names the subject. Several existing paths move the highlight with
		// QItemSelectionModel::NoUpdate, and the master-detail contract (NAV-01) is about the highlight, so the set
		// falls back to it rather than reporting nothing selected.

		if ( rows.isEmpty () && current.isValid () )
		{
			rows.append ( current );
		}

		// Document order (TREE-09). QItemSelectionModel returns its ranges in the order they were built, so three
		// Ctrl-clicks come back in click order -- but the same-parent rule means every row here is a sibling, so its
		// row number IS its position and one sort settles it.

		std::sort
		(
			rows.begin (), rows.end (),
			[] ( const QModelIndex& left, const QModelIndex& right ) { return left.row () < right.row (); }
		);

		// The service emits synchronously, and its re-emission of what is being written here must not be applied
		// back -- a flag rather than an origin test, because origin is reveal intent, not authorship (other writers
		// legitimately use Tree).

		publishingTreeSelection = true;

		if ( rows.isEmpty () )
		{
			selection->clear ();
		}
		else
		{
			QList<JsonPointer> pointers;

			pointers.reserve ( rows.size () );

			for ( const QModelIndex& row : rows )
			{
				pointers.append ( treeModel->pointer_for_index ( row ) );
			}

			// The primary is the current row while it is part of the set, and the last of the set otherwise -- which
			// is the Ctrl-toggle case, where the user has just switched the highlighted row OFF and the subject has to
			// be something a command can still act on.

			const QModelIndex primaryIndex = rows.contains ( current ) ? current : rows.last ();

			selection->set_multiple_selection
			(
				pointers,
				treeModel->pointer_for_index ( primaryIndex ),
				SelectionOrigin::Tree
			);
		}

		publishingTreeSelection = false;
	}

	void TreeViewPane::apply_drop ( const QModelIndex& dropParent, int dropRow )
	{
		QModelIndexList rows = treeView->selectionModel ()->selectedRows ();

		if ( rows.isEmpty () || !dropParent.isValid () )
		{
			return;
		}

		// The same-parent rule again, and it is NOT belt and braces over canDropMimeData: that answered about the
		// parent the drag STARTED from, recorded when the drag began, while this asks about the selection as it
		// stands now. They are the same in every ordinary drag and can differ if the tree was rebuilt under one.

		std::sort
		(
			rows.begin (), rows.end (),
			[] ( const QModelIndex& left, const QModelIndex& right ) { return left.row () < right.row (); }
		);

		QList<int> draggedIndices;

		draggedIndices.reserve ( rows.size () );

		for ( const QModelIndex& row : rows )
		{
			if ( row.parent () != dropParent )
			{
				return;
			}

			draggedIndices.append ( row.row () );
		}

		const int siblingCount = treeModel->rowCount ( dropParent );

		emit nodes_reorder_requested
		(
			treeModel->pointer_for_index ( dropParent ),
			plan_tree_drop      ( siblingCount, draggedIndices, dropRow ),
			dropped_run_indices ( siblingCount, draggedIndices, dropRow )
		);
	}

	bool TreeViewPane::selection_is_within_one_parent () const
	{
		const QModelIndexList rows = treeView->selectionModel ()->selectedRows ();

		if ( rows.size () <= 1 )
		{
			return true;
		}

		const QModelIndex parent = rows.first ().parent ();

		for ( const QModelIndex& row : rows )
		{
			if ( row.parent () != parent )
			{
				return false;
			}
		}

		return true;
	}

	void TreeViewPane::restore_selection_from_service ()
	{
		// The service holds the last ACCEPTED selection, by definition: every accepted one was published to it and a
		// refused one never reaches it. So there is no remembered copy of the view's last good state to keep in step
		// with the service -- one of them would eventually be wrong, and it would be the copy.

		QItemSelectionModel* const selectionModel = treeView->selectionModel ();

		applyingServiceSelection = true;

		QItemSelection restored;

		for ( const JsonPointer& pointer : selection->selection_set () )
		{
			const QModelIndex index = treeModel->index_for_pointer ( pointer );

			if ( index.isValid () )
			{
				restored.select ( index, index );
			}
		}

		selectionModel->select ( restored, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );

		// The highlight goes back with it, and with NoUpdate so restoring the CURRENT row does not re-select on top of
		// the set just written.

		const QModelIndex primary = selection->has_selection ()
		                          ? treeModel->index_for_pointer ( selection->selection () )
		                          : QModelIndex ();

		selectionModel->setCurrentIndex ( primary, QItemSelectionModel::NoUpdate );

		applyingServiceSelection = false;
	}

	void TreeViewPane::report_refused_extension ()
	{
		// The status bar alone (VAL-04's channel, not VAL-05's): a refused SELECTION is not a refused command, and a
		// modal over a stray Ctrl-click would be worse than the mistake. The message says what the rule is and what
		// the tree is now showing, because "nothing happened" is the one outcome a user cannot read off the screen.

		if ( status != nullptr )
		{
			status->show_message
			(
				tr ( "Multiple selection is limited to one parent. The previous selection stands." )
			);
		}
	}

	void TreeViewPane::handle_rows_about_to_be_removed ( const QModelIndex& parent, int first, int last )
	{
		const QModelIndex current = treeView->currentIndex ();

		// Only when the highlight is inside the run being removed. Anywhere else, Qt's persistent-index bookkeeping
		// already keeps it on the same node, and moving it would be this pane rearranging the tree behind an edit.

		const bool currentIsDying = current.isValid ()
		                         && ( current.parent () == parent )
		                         && ( current.row () >= first )
		                         && ( current.row () <= last );

		if ( !currentIsDying )
		{
			return;
		}

		// NAV-03's order: the following sibling, else the previous, else the parent. The rows have not gone yet -- this
		// is inside beginRemoveRows -- so these are PRE-removal indexes, and the selection model's persistent index
		// renumbers whichever one is chosen as the removal completes. That is exactly the mechanism the mode-gated Qt
		// code relied on, restated rather than reproduced by accident.

		QModelIndex fallback = treeModel->index ( last + 1, 0, parent );

		if ( !fallback.isValid () && ( first > 0 ) )
		{
			fallback = treeModel->index ( first - 1, 0, parent );
		}

		if ( !fallback.isValid () )
		{
			fallback = parent;   // The whole child list is going; the parent is what is left of it.
		}

		if ( !fallback.isValid () )
		{
			return;
		}

		// A plain setCurrentIndex, so the set collapses to the landing row: a removal that takes the highlighted node
		// out from under a multiple selection leaves one node selected rather than a remainder the user never chose.
		// It publishes through the ordinary bridge, which the removal window then defers to rowsRemoved.

		treeView->setCurrentIndex ( fallback );
	}

	void TreeViewPane::handle_rows_removed ()
	{
		// The window is closed: the shadow has erased and renumbered, and Qt has updated its persistent indexes, so
		// the current row's pointer is now exact. Release a publication the window held back, if any.

		if ( !publicationDeferredByRemoval )
		{
			return;
		}

		publicationDeferredByRemoval = false;

		if ( !applyingServiceSelection )
		{
			publish_tree_selection ();
		}
	}

	void TreeViewPane::handle_service_selection_changed ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		// The pane's own publication coming straight back; the view is already showing it. Everything else is applied,
		// origin Tree included: the command layer (menu adds, deletes, moves) selects its results with Tree because
		// they must reveal (NAV-03), and the highlight has to follow or the tree and the service drift apart -- which
		// is exactly what made the context menu act on the wrong node.

		if ( publishingTreeSelection )
		{
			return;
		}

		// EDITOR-04, the reveal-intent rule. An explicit navigation (Go To, Find, paste, drill-in) expands whatever it
		// takes to show the node; a form-field write-back or a programmatic selection must leave a collapsed branch
		// collapsed, or the tree jumps around while the user is typing.

		const bool reveal = reveals_selection ( origin );

		// index_for_pointer materializes the ancestors on the way down, which is what makes an unexpanded branch
		// addressable at all -- but materializing is not expanding, so a non-revealing origin still leaves the tree shut.

		const QModelIndex target = treeModel->index_for_pointer ( pointer );

		if ( !target.isValid () )
		{
			return;
		}

		// The second half of the no-reveal rule, and the one that is easy to miss: a non-revealing origin moves the
		// highlight only when the node is ALREADY visible. Merely setting the current index is not passive --
		// QAbstractItemView::currentChanged auto-scrolls to the new current row, and QTreeView::scrollTo EXPANDS every
		// collapsed ancestor on the way. So declining to call scrollTo ourselves is not enough; we must decline to move
		// the current index at all, or Qt expands the branch on our behalf.
		//
		// Leaving the highlight put is also the better interaction: the selection service and the status bar still track
		// the field, and the tree's own Up / Down keep walking the rows the user can see (NAV-02).

		if ( !reveal && !is_row_revealed ( target ) )
		{
			return;
		}

		applyingServiceSelection = true;

		treeView->selectionModel ()->setCurrentIndex
		(
			target,
			QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows
		);

		if ( reveal )
		{
			treeView->scrollTo ( target, QAbstractItemView::EnsureVisible );
		}

		applyingServiceSelection = false;
	}

	void TreeViewPane::handle_service_selection_cleared ()
	{
		applyingServiceSelection = true;

		treeView->selectionModel ()->clearSelection ();
		treeView->setCurrentIndex ( QModelIndex () );

		applyingServiceSelection = false;
	}

	//=================================================================================================================
	// Handlers -- projection lifecycle
	//=================================================================================================================

	void TreeViewPane::handle_model_reset ()
	{
		// A root subtree replacement emits BOTH this and the projection pair; the restore owns the view state in that
		// case, so stand back.

		if ( restoringProjection )
		{
			return;
		}

		// A freshly loaded document: open the file node (plus INITIAL_EXPAND_DEPTH levels beneath it) and select the
		// root, so the pane opens showing the document's shape with a live master-detail selection (NAV-01).

		const QModelIndex rootIndex = treeModel->index ( 0, 0 );

		if ( !rootIndex.isValid () )
		{
			return;
		}

		treeView->expandRecursively ( rootIndex, config::tree::INITIAL_EXPAND_DEPTH );

		treeView->setCurrentIndex ( rootIndex );
	}

	void TreeViewPane::handle_projection_capture ()
	{
		// Expansion and selection are view state named by POINTER, which is the only currency that survives the nodes
		// underneath being replaced (TREE-07).

		restoringProjection  = true;
		capturedExpansion    .clear ();
		hadCapturedSelection = false;

		const QModelIndex rootIndex = treeModel->index ( 0, 0 );

		if ( rootIndex.isValid () )
		{
			collect_expanded_pointers ( rootIndex, capturedExpansion );
		}

		const QModelIndex current = treeView->currentIndex ();

		if ( current.isValid () )
		{
			capturedSelection    = treeModel->pointer_for_index ( current );
			hadCapturedSelection = true;
		}
	}

	void TreeViewPane::handle_projection_restore ()
	{
		// Put back every expansion whose pointer still resolves; the ones that do not simply stay closed. Shallower
		// pointers are restored first so a parent is open before its child is asked to be.

		QList<JsonPointer> ordered = capturedExpansion;

		std::sort
		(
			ordered.begin (), ordered.end (),
			[] ( const JsonPointer& left, const JsonPointer& right ) { return left.token_count () < right.token_count (); }
		);

		for ( const JsonPointer& pointer : ordered )
		{
			const QModelIndex index = treeModel->index_for_pointer ( pointer );

			if ( index.isValid () )
			{
				treeView->expand ( index );
			}
		}

		// NAV-03: the prior selection at its path, else the nearest surviving ancestor, else the root -- never a reset
		// to the top.

		if ( hadCapturedSelection )
		{
			const QModelIndex target = treeModel->nearest_index_for_pointer ( capturedSelection );

			if ( target.isValid () )
			{
				applyingServiceSelection = true;

				treeView->setCurrentIndex ( target );

				applyingServiceSelection = false;

				// The pointer may have shifted to an ancestor, so the rest of the application has to be told where the
				// selection actually landed. Programmatic: this is a restoration, not a navigation, and must not expand.

				selection->set_selection ( treeModel->pointer_for_index ( target ), SelectionOrigin::Programmatic );
			}
		}

		capturedExpansion.clear ();
		hadCapturedSelection = false;
		restoringProjection  = false;
	}

	//=================================================================================================================
	// Handlers -- input
	//=================================================================================================================

	void TreeViewPane::handle_clicked ( const QModelIndex& index )
	{
		// Publish the gesture and do nothing else -- selecting the row is QTreeView's own job, already done by the time
		// this arrives, and expansion is not a click's business (EDITOR-04: "row clicks never expand or collapse").
		//
		// QTreeView does not emit clicked for a press on the branch chevron, so the expand gesture cannot arrive here
		// wearing a click's clothes.

		if ( index.isValid () )
		{
			emit node_clicked ( treeModel->pointer_for_index ( index ) );
		}
	}

	void TreeViewPane::handle_activated ( const QModelIndex& index )
	{
		if ( !index.isValid () )
		{
			return;
		}

		// Announce the activation before acting on it. A leaf has nothing to expand, and this is what carries the
		// gesture to the editor pane, which hands the caret to the corresponding field (EDITOR-04).

		emit node_activated ( treeModel->pointer_for_index ( index ) );

		// NAV-02: Enter toggles a branch. Arrow keys, Home / End, and type-to-search are QTreeView's own and need no
		// help here.

		if ( !treeModel->hasChildren ( index ) )
		{
			return;
		}

		treeView->setExpanded ( index, !treeView->isExpanded ( index ) );
	}

	void TreeViewPane::select_context_row ( const QModelIndex& target )
	{
		if ( !target.isValid () )
		{
			return;
		}

		if ( target != treeView->currentIndex () )
		{
			// Moving the current row publishes through the selection bridge like any other tree selection.

			treeView->setCurrentIndex ( target );

			return;
		}

		// The row is ALREADY current -- but the service can still disagree with the view. A no-reveal write-back
		// (FormField into a collapsed branch) tracks the field in the service while the tree highlight stays put, and
		// the menu's commands read the SERVICE -- so right-clicking the highlighted row would act on the form's field
		// instead of the clicked node. Re-publish so the two agree before the menu opens.

		const JsonPointer pointer = treeModel->pointer_for_index ( target );

		if ( !selection->has_selection () || ( selection->selection () != pointer ) )
		{
			publishingTreeSelection = true;

			selection->set_selection ( pointer, SelectionOrigin::Tree );

			publishingTreeSelection = false;
		}
	}

	bool TreeViewPane::eventFilter ( QObject* watched, QEvent* event )
	{
		// A keyboard-originated context menu, intercepted before Qt turns it into a bare position. For the keyboard case
		// Qt derives that position from the focus widget's input-method cursor rectangle, which a tree view does not
		// supply -- so it arrives at the viewport's top-left corner and the menu would open on the first visible row, or
		// on nothing at all, rather than on the row the user has arrowed to.

		if ( ( watched == treeView ) && ( event->type () == QEvent::ContextMenu ) )
		{
			const QContextMenuEvent* const contextEvent = static_cast<QContextMenuEvent*> ( event );

			if ( contextEvent->reason () == QContextMenuEvent::Keyboard )
			{
				show_context_menu_for_current_row ();

				return true;
			}
		}

		return QWidget::eventFilter ( watched, event );
	}

	void TreeViewPane::show_context_menu_for_current_row ()
	{
		const QModelIndex current = treeView->currentIndex ();

		// Anchored beneath the current row -- the same rule the keyboard Add-type chooser follows, because a keyboard
		// gesture belongs at the keyboard's subject rather than wherever the mouse was left. A row with no visual rect
		// (none current, or scrolled out of view) falls back to the viewport's corner.

		const QRect rowRect = treeView->visualRect ( current );

		const QPoint anchor = rowRect.isValid ()
		                    ? QPoint ( rowRect.left (), rowRect.bottom () + 1 )
		                    : QPoint ( 0, 0 );

		show_context_menu ( current, treeView->viewport ()->mapToGlobal ( anchor ) );
	}

	void TreeViewPane::handle_context_menu ( const QPoint& position )
	{
		// The MOUSE route: the menu belongs to the row under the pointer.

		show_context_menu ( treeView->indexAt ( position ), treeView->viewport ()->mapToGlobal ( position ) );
	}

	void TreeViewPane::show_context_menu ( const QModelIndex& target, const QPoint& globalPosition )
	{
		// The menu acts on THAT row, so make it current and bring the service into agreement first -- otherwise the
		// menu's commands would silently target whatever was selected before (see select_context_row).

		select_context_row ( target );

		QMenu menu ( this );

		// The shared node command set (NodeContextActions.hpp) -- the same menu the Form View offers on a key label, so
		// the two panes cannot drift into different commands for the same concept.

		populate_node_context_menu ( &menu, contextActions );

		menu.addSeparator ();

		// The tree's own four (TREE-05 / TREE-06), as ONE group in the order the View menu carries them: whole tree
		// first, then the current row's subtree. Shared QActions rather than local ones, so both surfaces show the same
		// label and the same enabled state -- select_context_row above has already made the clicked row current, which
		// is what the subtree pair acts on.

		for ( QAction* const viewCommand : { viewCommands.expandAll, viewCommands.collapseAll, viewCommands.expandSubtree, viewCommands.collapseSubtree } )
		{
			if ( viewCommand != nullptr )
			{
				menu.addAction ( viewCommand );
			}
		}

		menu.exec ( globalPosition );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	bool TreeViewPane::is_row_revealed ( const QModelIndex& index ) const
	{
		// "Revealed" is a property of the ANCESTORS, not of the row itself: a row is drawn when every branch above it is
		// open. Scroll position is deliberately not part of the question -- a row scrolled out of view is still one the
		// user has opened their way down to, and letting the view scroll to it is the highlight following the selection,
		// not the tree rearranging itself.

		for ( QModelIndex ancestor = index.parent (); ancestor.isValid (); ancestor = ancestor.parent () )
		{
			if ( !treeView->isExpanded ( ancestor ) )
			{
				return false;
			}
		}

		return true;
	}

	void TreeViewPane::collect_expanded_pointers ( const QModelIndex& index, QList<JsonPointer>& outPointers ) const
	{
		// Walks only what is EXPANDED, so the cost is bounded by what the user can actually see rather than by the size
		// of the document.

		if ( !treeView->isExpanded ( index ) )
		{
			return;
		}

		outPointers.append ( treeModel->pointer_for_index ( index ) );

		for ( int row = 0; row < treeModel->rowCount ( index ); ++row )
		{
			collect_expanded_pointers ( treeModel->index ( row, 0, index ), outPointers );
		}
	}
}
