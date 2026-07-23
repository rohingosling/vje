//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
#include "style/FocusHighlight.hpp"
#include "views/PaneHeader.hpp"

#include <vje_core/document/JsonDocument.hpp>

#include <QAction>
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
		QWidget*          parent
	)
		: QWidget   ( parent )
		, selection ( selection )
	{
		treeModel = new JsonTreeModel ( document, icons, this );

		treeView = new QTreeView ( this );
		treeView->setObjectName ( QStringLiteral ( "treeView" ) );
		treeView->setModel ( treeModel );

		// -- View configuration ---------------------------------------------------------------------------------------

		treeView->setHeaderHidden ( true );                                    // One column of key labels (TREE-02).
		treeView->setSelectionMode ( QAbstractItemView::SingleSelection );     // TREE-04.
		treeView->setSelectionBehavior ( QAbstractItemView::SelectRows );
		treeView->setEditTriggers ( QAbstractItemView::NoEditTriggers );       // Renaming is a command (EDIT-02).
		treeView->setIndentation ( config::tree::INDENTATION );
		treeView->setIconSize ( QSize ( config::tree::ICON_SIZE, config::tree::ICON_SIZE ) );
		treeView->setExpandsOnDoubleClick ( true );
		treeView->setAnimated ( false );
		treeView->setContextMenuPolicy ( Qt::CustomContextMenu );

		// Tab belongs to the workspace, not to the tree (NAV-04). QAbstractItemView defaults this ON, which would make
		// Tab a second, redundant way of moving the highlight one row -- and would swallow the key before the focus
		// cycle ever saw it.

		treeView->setTabKeyNavigation ( false );

		// The virtualization contract (TREE-08). Uniform row heights let QTreeView compute the visible range
		// arithmetically instead of measuring every row, which is what keeps a large document scrollable; combined with
		// the model's lazy population, an unexpanded branch costs one row.

		treeView->setUniformRowHeights ( true );

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

		connect ( treeModel, &QAbstractItemModel::modelReset,             this, &TreeViewPane::handle_model_reset );
		connect ( treeModel, &JsonTreeModel::projection_about_to_rebuild, this, &TreeViewPane::handle_projection_capture );
		connect ( treeModel, &JsonTreeModel::projection_rebuilt,          this, &TreeViewPane::handle_projection_restore );

		connect ( treeView, &QTreeView::clicked,                 this, &TreeViewPane::handle_clicked );
		connect ( treeView, &QTreeView::activated,              this, &TreeViewPane::handle_activated );
		connect ( treeView, &QWidget::customContextMenuRequested, this, &TreeViewPane::handle_context_menu );
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

		const QModelIndex current = treeView->selectionModel ()->currentIndex ();

		if ( !current.isValid () )
		{
			selection->clear ();

			return;
		}

		selection->set_selection ( treeModel->pointer_for_index ( current ), SelectionOrigin::Tree );
	}

	void TreeViewPane::handle_service_selection_changed ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		// The tree wrote this one; the view is already showing it.

		if ( origin == SelectionOrigin::Tree )
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

	void TreeViewPane::handle_context_menu ( const QPoint& position )
	{
		// Right-clicking a row acts on THAT row, so select it first -- otherwise the menu's commands would silently
		// target whatever was selected before.

		const QModelIndex target = treeView->indexAt ( position );

		if ( target.isValid () && ( target != treeView->currentIndex () ) )
		{
			treeView->setCurrentIndex ( target );
		}

		QMenu menu ( this );

		// The shared node command set (NodeContextActions.hpp) -- the same menu the Form View offers on a key label, so
		// the two panes cannot drift into different commands for the same concept.

		populate_node_context_menu ( &menu, contextActions );

		menu.addSeparator ();

		// The pane's own commands (TREE-06). Local to the menu rather than shared actions: they operate on the clicked
		// node and have no menu-bar or toolbar counterpart.

		QAction* expandSubtree   = menu.addAction ( tr ( "&Expand Subtree" ) );
		QAction* collapseSubtree = menu.addAction ( tr ( "C&ollapse Subtree" ) );

		const bool isBranch = target.isValid () && treeModel->hasChildren ( target );

		expandSubtree  ->setEnabled ( isBranch );
		collapseSubtree->setEnabled ( isBranch );

		connect ( expandSubtree,   &QAction::triggered, this, &TreeViewPane::expand_current_subtree );
		connect ( collapseSubtree, &QAction::triggered, this, &TreeViewPane::collapse_current_subtree );

		menu.exec ( treeView->viewport ()->mapToGlobal ( position ) );
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
