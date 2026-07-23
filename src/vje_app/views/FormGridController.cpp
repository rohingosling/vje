//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   FormGridController implementation. See the header for why one controller drives both Form View grids and which
//   four behaviours are policy rather than shared code.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/FormGridController.hpp"

#include "models/IGridProjection.hpp"
#include "models/cell_presentation.hpp"
#include "services/SelectionService.hpp"
#include "views/JsonCellDelegate.hpp"

#include <vje_core/document/JsonNode.hpp>

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QTableView>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FormGridController::FormGridController
	(
		QTableView*       view,
		IGridProjection*  projection,
		SelectionService* selection,
		const Policy&     policy,
		QObject*          parent
	)
		: QObject ( parent )
		, view       ( view )
		, projection ( projection )
		, selection  ( selection )
		, policy     ( policy )
	{
		cellDelegate = new JsonCellDelegate ( policy.arrowKeysNavigateInEditor, this );

		view->setItemDelegate ( cellDelegate );

		// The activation gestures EDITOR-02 / 03 specify, and ONLY those. SelectedClicked and CurrentChanged are
		// deliberately absent: a click on a field selects it and nothing more, which is what the spec means by "a click
		// on a field itself only selects it".

		view->setEditTriggers
		(
			QAbstractItemView::DoubleClicked  |     // Double-click activates.
			QAbstractItemView::EditKeyPressed |     // F2 activates.
			QAbstractItemView::AnyKeyPressed        // Typing activates, replacing the value (see the delegate).
		);

		view->setSelectionMode     ( QAbstractItemView::SingleSelection );
		view->setSelectionBehavior ( QAbstractItemView::SelectItems );      // A CELL is the unit, not a row.
		view->setContextMenuPolicy ( Qt::CustomContextMenu );

		// Tab belongs to the workspace, not to the grid (NAV-04): it moves the keyboard to the next pane. The ARROW
		// keys carry the whole of cell traversal, which is what EDITOR-03 now specifies.
		//
		// The exception is an OPEN EDITOR, where Tab stays commit-and-advance (the delegate's own filter has the key
		// first, and PaneCycler steps aside while a grid is in EditingState). Taking Tab from a half-typed value would
		// throw the user across the workspace mid-edit.

		view->setTabKeyNavigation ( false );

		view->installEventFilter ( this );

		connect ( cellDelegate, &JsonCellDelegate::editing_moved, this, &FormGridController::handle_editing_moved );

		connect ( view, &QAbstractItemView::doubleClicked,        this, &FormGridController::handle_double_clicked );
		connect ( view, &QWidget::customContextMenuRequested,     this, &FormGridController::handle_context_menu );

		connect
		(
			view->selectionModel (), &QItemSelectionModel::currentChanged,
			this, &FormGridController::handle_current_changed
		);
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	JsonCellDelegate* FormGridController::delegate () const
	{
		return cellDelegate;
	}

	JsonPointer FormGridController::current_pointer () const
	{
		const QModelIndex current = view->currentIndex ();

		if ( !current.isValid () )
		{
			return JsonPointer ();
		}

		return projection->grid_pointer ( current.row (), current.column () );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void FormGridController::set_current_pointer ( const JsonPointer& pointer )
	{
		const GridPosition cell = projection->grid_cell ( pointer );

		if ( !cell.is_valid () )
		{
			return;
		}

		applyingSelection = true;

		view->setCurrentIndex ( view->model ()->index ( cell.row, cell.column ) );

		applyingSelection = false;
	}

	void FormGridController::select_first_cell ()
	{
		const QAbstractItemModel* const model = view->model ();

		if ( ( model->rowCount () == 0 ) || ( model->columnCount () == 0 ) )
		{
			return;
		}

		const int column = ( policy.landingColumn >= 0 ) ? policy.landingColumn : 0;

		applyingSelection = true;

		view->setCurrentIndex ( view->model ()->index ( 0, column ) );

		applyingSelection = false;
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void FormGridController::activate_editing ()
	{
		const QModelIndex current = view->currentIndex ();

		if ( !current.isValid () )
		{
			return;
		}

		activate ( current );

		// EDITOR-04: "booleans open their dropdown" when the caret is handed over. Only when the view is actually on
		// screen -- a popup under the offscreen platform has no window to open into, and this path is reachable from a
		// headless test.

		if ( !view->isVisible () )
		{
			return;
		}

		if ( QComboBox* const booleanEditor = qobject_cast<QComboBox*> ( QApplication::focusWidget () ) )
		{
			booleanEditor->showPopup ();
		}
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void FormGridController::handle_editing_moved ( GridMove move )
	{
		const QModelIndex current = view->currentIndex ();

		if ( !current.isValid () )
		{
			return;
		}

		const QAbstractItemModel* const model = view->model ();

		const GridPosition next = next_position
		(
			GridPosition { current.row (), current.column () },
			move,
			model->rowCount (),
			model->columnCount ()
		);

		if ( !next.is_valid () )
		{
			return;
		}

		// EDITOR-12's bottom-edge row growth belongs exactly here -- next == current on a Down from the last row is its
		// trigger -- and lands in Phase 9. Until then the clamp stands, which is the specified behaviour everywhere
		// else on the grid's edge.

		view->setCurrentIndex ( view->model ()->index ( next.row, next.column ) );
	}

	void FormGridController::handle_double_clicked ( const QModelIndex& index )
	{
		// A double-click on an editable cell is already handled by the DoubleClicked edit trigger; what is left for us
		// is the container cell, which has no editor and drills in instead (EDITOR-05).

		if ( !index.isValid () || edits_in_place ( index ) )
		{
			// An editable cell is Qt's DoubleClicked trigger's business, and it has already opened the editor. Falling
			// through would ALSO drill in on a container member's key -- opening the rename editor and navigating away
			// from it in the same gesture.

			return;
		}

		JsonNode* const node = projection->grid_node ( index.row (), index.column () );

		if ( is_drill_in_cell ( node ) )
		{
			request_drill_in ( projection->grid_pointer ( index.row (), index.column () ) );
		}
	}

	void FormGridController::handle_current_changed ( const QModelIndex& current, const QModelIndex& previous )
	{
		Q_UNUSED ( previous );

		if ( !current.isValid () )
		{
			return;
		}

		// EDITOR-04, and only for the form: clicking into a field selects the corresponding node in the tree. Table
		// cells deliberately do not write back -- in-place cell editing must not move the tree selection.
		//
		// FormField is the origin that tells the tree to select WITHOUT expanding, so a collapsed branch stays shut
		// while the user works down a form.

		const bool writesBack = policy.writesSelectionBack && !applyingSelection && ( selection != nullptr );

		if ( writesBack )
		{
			selection->set_selection
			(
				projection->grid_pointer ( current.row (), current.column () ),
				SelectionOrigin::FormField
			);
		}
	}

	void FormGridController::handle_context_menu ( const QPoint& position )
	{
		if ( policy.contextMenuColumn < 0 )
		{
			return;
		}

		const QModelIndex target = view->indexAt ( position );

		// EDITOR-02 offers the menu on a KEY, and the commands act on that key's node. Right-clicking a value is not
		// the same gesture and is left alone (the cell clipboard that will claim it is EDITOR-11, Phase 9).

		if ( !target.isValid () || ( target.column () != policy.contextMenuColumn ) )
		{
			return;
		}

		// Act on the row that was clicked, not on whatever was current before it -- the same rule the tree's context
		// menu follows. The key cell itself becomes current, since a key is now a place the highlight can be
		// (EDITOR-02) rather than a label that bounces the highlight to its value.

		view->setCurrentIndex ( view->model ()->index ( target.row (), target.column () ) );

		emit context_menu_requested
		(
			projection->grid_pointer ( target.row (), target.column () ),
			view->viewport ()->mapToGlobal ( position )
		);
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	bool FormGridController::eventFilter ( QObject* watched, QEvent* event )
	{
		if ( ( watched != view ) || ( event->type () != QEvent::KeyPress ) )
		{
			return QObject::eventFilter ( watched, event );
		}

		const QKeyEvent* const keyEvent = static_cast<QKeyEvent*> ( event );

		const bool isEnter = ( keyEvent->key () == Qt::Key_Return ) || ( keyEvent->key () == Qt::Key_Enter );

		if ( !isEnter || ( keyEvent->modifiers () != Qt::NoModifier ) )
		{
			return QObject::eventFilter ( watched, event );
		}

		// "Enter is not a navigation key" (EDITOR-03). On a selected cell it ACTIVATES -- opening the editor, or
		// drilling into a container. This filter only ever sees Enter while no editor is open; once one is, the
		// delegate's own filter has it first and turns it into commit-and-advance.

		const QModelIndex current = view->currentIndex ();

		if ( !current.isValid () )
		{
			return QObject::eventFilter ( watched, event );
		}

		activate ( current );

		return true;
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void FormGridController::activate ( const QModelIndex& index )
	{
		// EDITABILITY IS ASKED FIRST, and the order is the point. It used to be "is this a container? then drill in" --
		// which was safe while every cell in a row spoke for the same node. Now the object form's KEY column is
		// renameable (EDIT-02), so an object member has a key that edits and a value that drills in, and asking about
		// the node before asking about the cell would send every rename into a drill-in.
		//
		// The question goes to the MODEL rather than being re-derived here: flags() is where each projection states
		// what it will accept, and setData() enforces the same predicate.

		if ( edits_in_place ( index ) )
		{
			const GridPosition editCell = projection->grid_edit_cell ( index.row (), index.column () );

			view->edit ( view->model ()->index ( editCell.row, editCell.column ) );

			return;
		}

		JsonNode* const node = projection->grid_node ( index.row (), index.column () );

		if ( is_drill_in_cell ( node ) )
		{
			request_drill_in ( projection->grid_pointer ( index.row (), index.column () ) );
		}

		// Anything else -- a null placeholder, a missing cell -- is read-only until EDITOR-12's typed entry lands in
		// Phase 9, and activating it does nothing.
	}

	bool FormGridController::edits_in_place ( const QModelIndex& index ) const
	{
		if ( !index.isValid () )
		{
			return false;
		}

		const GridPosition editCell = projection->grid_edit_cell ( index.row (), index.column () );

		const QModelIndex editIndex = view->model ()->index ( editCell.row, editCell.column );

		return editIndex.isValid () && editIndex.flags ().testFlag ( Qt::ItemIsEditable );
	}

	void FormGridController::request_drill_in ( const JsonPointer& pointer )
	{
		// Deferred onto the event loop. The consumer answers a drill-in by re-presenting the pane, and doing that
		// synchronously would rebuild the very table whose event handler is still on the stack (EDITOR-03 states the
		// same requirement directly).

		QMetaObject::invokeMethod
		(
			this,
			[ this, pointer ] () { emit drill_in_requested ( pointer ); },
			Qt::QueuedConnection
		);
	}
}
