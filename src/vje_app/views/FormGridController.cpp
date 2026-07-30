//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include "AppConfig.hpp"
#include "models/IGridProjection.hpp"
#include "models/JsonTableModel.hpp"
#include "models/cell_presentation.hpp"
#include "services/ClipboardService.hpp"
#include "services/SelectionService.hpp"
#include "services/SettingsStore.hpp"
#include "services/StatusService.hpp"
#include "views/JsonCellDelegate.hpp"
#include "views/cell_paste_plan.hpp"

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/CellPasteConverter.hpp>

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMessageBox>
#include <QTableView>

#include <vector>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FormGridController::FormGridController
	(
		QTableView*         view,
		IGridProjection*    projection,
		SelectionService*   selection,
		const Policy&       policy,
		const TableSupport& tableSupport,
		QObject*            parent
	)
		: QObject ( parent )
		, view       ( view )
		, projection ( projection )
		, selection  ( selection )
		, policy     ( policy )
		, tableModel ( tableSupport.model )
		, clipboard  ( tableSupport.clipboard )
		, settings   ( tableSupport.settings )
		, status     ( tableSupport.status )
	{
		cellDelegate = new JsonCellDelegate ( this );

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

		// A typed-entry commit into a provisional cell (EDITOR-12). Deferred, so the array table alone wires it.

		if ( tableModel != nullptr )
		{
			connect
			(
				tableModel, &JsonTableModel::provisional_commit_pending,
				this, &FormGridController::handle_provisional_commit
			);
		}
	}

	FormGridController::~FormGridController () = default;

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

		// EDITOR-12: the commit that just fired was into a PROVISIONAL cell. Do not navigate now -- the materialize is
		// still queued and will move the highlight itself; only record whether this was a downward advance, so it can
		// grow a fresh provisional (Enter / Tab) rather than land on the new real row.

		if ( materializePending )
		{
			growAfterMaterialize = ( move == GridMove::Down ) || ( move == GridMove::NextCell );

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

		// EDITOR-12's bottom-edge row growth: a downward advance (Enter / Tab) off the last REAL row, clamped by
		// next_position, grows a provisional row instead of standing still.

		const bool clampedAtBottom = ( next.row == current.row () ) && ( next.column == current.column () );
		const bool downwardAdvance = ( move == GridMove::Down ) || ( move == GridMove::NextCell );

		if ( is_array_table () && downwardAdvance && clampedAtBottom &&
		     ( current.row () == tableModel->element_count () - 1 ) && !tableModel->has_provisional_row () )
		{
			grow_provisional_row ( current.column () );

			return;
		}

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

		// EDITOR-12: the highlight leaving a still-empty provisional row abandons it without a trace. Suppressed while
		// the controller is itself moving the highlight (growing, materializing, applying an inbound selection).

		abandon_provisional_if_off_row ( current );

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

		if ( keyEvent->modifiers () != Qt::NoModifier )
		{
			return QObject::eventFilter ( watched, event );
		}

		const bool isEnter = ( keyEvent->key () == Qt::Key_Return ) || ( keyEvent->key () == Qt::Key_Enter );

		// EDITOR-12: a Down at the bottom edge of the array table grows a provisional row (navigating, no editor open --
		// this filter only sees the key while no editor is open). A further Down on the still-empty provisional row does
		// not stack another, so it is swallowed; otherwise QTableView's own arrow navigation is left to run.

		if ( ( keyEvent->key () == Qt::Key_Down ) && is_array_table () )
		{
			const QModelIndex current = view->currentIndex ();

			if ( current.isValid () && tableModel->is_provisional_row ( current.row () ) )
			{
				return true;   // No stacking; the provisional row is the last there is.
			}

			const bool atLastRealRow = current.isValid ()
			                        && ( current.row () == tableModel->element_count () - 1 )
			                        && !tableModel->has_provisional_row ();

			if ( atLastRealRow )
			{
				grow_provisional_row ( current.column () );

				return true;
			}

			return QObject::eventFilter ( watched, event );   // A normal Down; QTableView moves the highlight.
		}

		if ( !isEnter )
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

	//=================================================================================================================
	// Provisional-row lifecycle (EDITOR-12)
	//=================================================================================================================

	bool FormGridController::is_array_table () const
	{
		return tableModel != nullptr;
	}

	void FormGridController::grow_provisional_row ( int column )
	{
		if ( !is_array_table () || tableModel->has_provisional_row () )
		{
			return;
		}

		const int landingColumn = ( column >= 0 ) ? column : 0;

		applyingSelection = true;

		tableModel->set_provisional_row ( true );

		view->setCurrentIndex ( view->model ()->index ( tableModel->element_count (), landingColumn ) );

		applyingSelection = false;
	}

	void FormGridController::handle_provisional_commit ( int column, const QString& text )
	{
		if ( !is_array_table () )
		{
			return;
		}

		// EDITOR-12's JSON-literal rule interprets the typed text. The materialize itself is deferred, because an editor
		// is still open on the provisional row this is about to remove.

		// Through the same rule setData uses, so a typed entry into the provisional row means what the identical
		// keystrokes mean one row above it (SET-03 / EDITOR-12). A refusal leaves pendingProvisionalValue null and the
		// materialize below declines.

		pendingProvisionalValue  = typed_entry_value ( text, tableModel->string_display () );
		pendingProvisionalColumn = column;
		materializePending       = true;
		growAfterMaterialize     = false;

		QMetaObject::invokeMethod ( this, [ this ] () { finish_provisional_materialize (); }, Qt::QueuedConnection );
	}

	void FormGridController::finish_provisional_materialize ()
	{
		if ( !materializePending || !is_array_table () )
		{
			materializePending = false;

			return;
		}

		const int  newRow = tableModel->element_count ();   // Where the real element lands (the provisional row's index).
		const int  column = ( pendingProvisionalColumn >= 0 ) ? pendingProvisionalColumn : 0;
		const bool grow   = growAfterMaterialize;

		applyingSelection = true;

		const bool materialized = tableModel->materialize_provisional ( pendingProvisionalColumn, std::move ( pendingProvisionalValue ) );

		if ( materialized )
		{
			// EDITOR-12: a downward advance (Enter / Tab) grows a fresh provisional and lands on it; a plain commit lands
			// on the new real row. Either way the swap is in place -- widths and scroll are untouched.

			if ( grow )
			{
				tableModel->set_provisional_row ( true );

				view->setCurrentIndex ( view->model ()->index ( newRow + 1, column ) );
			}
			else
			{
				view->setCurrentIndex ( view->model ()->index ( newRow, column ) );
			}
		}

		applyingSelection        = false;
		materializePending       = false;
		growAfterMaterialize     = false;
		pendingProvisionalColumn = -1;
	}

	void FormGridController::abandon_provisional_if_off_row ( const QModelIndex& current )
	{
		if ( !is_array_table () || materializePending || applyingSelection || !tableModel->has_provisional_row () )
		{
			return;
		}

		const int provisionalRow = tableModel->element_count ();

		// Only a move to a DIFFERENT, VALID row abandons it. An invalid current is what a model reset produces (the view
		// clears its current index), and abandoning then would wipe the provisional row an empty array is presented WITH
		// (EDITOR-12) the instant it appears -- so an invalid current is deliberately not "leaving it for another row".

		if ( current.isValid () && ( current.row () != provisionalRow ) )
		{
			tableModel->set_provisional_row ( false );
		}
	}

	//=================================================================================================================
	// Cell clipboard (EDITOR-11)
	//=================================================================================================================

	bool FormGridController::cell_clipboard_active () const
	{
		return is_array_table () && ( clipboard != nullptr ) && view->currentIndex ().isValid ();
	}

	bool FormGridController::copy_cell ()
	{
		if ( !cell_clipboard_active () )
		{
			return false;
		}

		const QModelIndex current = view->currentIndex ();
		JsonNode* const   node    = tableModel->grid_node ( current.row (), current.column () );

		// Copy and cut on a MISSING (or provisional) cell are no-ops -- there is no value to place on the clipboard
		// (EDITOR-11) -- and they are HANDLED no-ops, not declines: the gesture belongs to the cell while the table is
		// the face, and returning false would fall through to copying the whole selected NODE over whatever the
		// clipboard held. A status message says why nothing happened (the VAL-04 refusal pattern).

		if ( cell_content ( node ) == CellContent::Missing )
		{
			if ( status != nullptr )
			{
				status->show_message ( tr ( "Nothing to copy" ), config::form::REFUSAL_MESSAGE_TIMEOUT );
			}

			return true;
		}

		clipboard->copy_cell ( *node );

		if ( status != nullptr )
		{
			status->show_message ( tr ( "Copied cell" ), config::form::REFUSAL_MESSAGE_TIMEOUT );
		}

		return true;
	}

	bool FormGridController::cut_cell ()
	{
		if ( !cell_clipboard_active () )
		{
			return false;
		}

		const QModelIndex current = view->currentIndex ();
		JsonNode* const   node    = tableModel->grid_node ( current.row (), current.column () );

		// The same handled no-op as copy_cell: a missing cell has nothing to cut, and falling through would cut the
		// whole selected node.

		if ( cell_content ( node ) == CellContent::Missing )
		{
			if ( status != nullptr )
			{
				status->show_message ( tr ( "Nothing to cut" ), config::form::REFUSAL_MESSAGE_TIMEOUT );
			}

			return true;
		}

		clipboard->copy_cell ( *node );

		// Cut is copy plus setting the cell to null, as one undo step -- container cells included. Cutting an already-null
		// cell just copies (there is nothing to null out).

		if ( node->kind () != JsonKind::Null )
		{
			tableModel->apply_cell_value ( current.row (), current.column (), JsonNode::make_null (), QStringLiteral ( "Cut Cell" ) );
		}

		if ( status != nullptr )
		{
			status->show_message ( tr ( "Cut cell" ), config::form::REFUSAL_MESSAGE_TIMEOUT );
		}

		return true;
	}

	bool FormGridController::paste_cell ()
	{
		if ( !cell_clipboard_active () || ( clipboard == nullptr ) )
		{
			return false;
		}

		std::unique_ptr<JsonNode> value = clipboard->value ();

		if ( value == nullptr )
		{
			return false;   // Nothing on the clipboard to paste.
		}

		const QModelIndex current = view->currentIndex ();
		const int         row     = current.row ();
		const int         column  = current.column ();

		// The target cell kind decides the conversion matrix column (EDITOR-11). A provisional / null / missing cell is
		// Untyped (takes the value as-is; a container is still shape-checked).

		CellTarget target = CellTarget::Untyped;

		if ( !tableModel->is_provisional_row ( row ) )
		{
			JsonNode* const   node    = tableModel->grid_node ( row, column );
			const CellContent content = cell_content ( node );

			if ( content == CellContent::Scalar )
			{
				switch ( node->kind () )
				{
					case JsonKind::String:  target = CellTarget::String;  break;
					case JsonKind::Number:  target = CellTarget::Number;  break;
					case JsonKind::Boolean: target = CellTarget::Boolean; break;
					default:                target = CellTarget::Untyped; break;
				}
			}
			else if ( content == CellContent::Container )
			{
				target = ( node->kind () == JsonKind::Object ) ? CellTarget::Object : CellTarget::Array;
			}
		}

		// The column's other values, for the shape check on a container paste (EDITOR-11). Null / missing cells are
		// passed through and ignored by the comparer.

		std::vector<const JsonNode*> columnValues;

		for ( int otherRow = 0; otherRow < tableModel->element_count (); ++otherRow )
		{
			if ( otherRow == row )
			{
				continue;
			}

			if ( JsonNode* const otherNode = tableModel->grid_node ( otherRow, column ) )
			{
				columnValues.push_back ( otherNode );
			}
		}

		const bool jaggedAllowed = ( settings != nullptr )
		                        && settings->value_bool ( settings_keys::FORM_ALLOW_JAGGED_PASTE, false );

		CellPasteDecision decision = plan_cell_paste ( *value, target, columnValues, jaggedAllowed );

		switch ( decision.plan )
		{
			case CellPastePlan::Incompatible:
			{
				// EDITOR-11's incompatible-data-type message box: a paste never degrades a value silently.

				QMessageBox::warning ( view, tr ( "Paste" ), decision.message );

				return true;
			}

			case CellPastePlan::NeedsJaggedConfirm:
			{
				// SET-05 is on: warn that continuing makes the array jagged, and paste only on confirm (EDITOR-11).

				const QMessageBox::StandardButton answer = QMessageBox::warning
				(
					view,
					tr ( "Paste" ),
					tr ( "This value does not match the array's structure. Pasting it will make the array structurally "
					     "heterogeneous (\"jagged\"). Continue?" ),
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::No
				);

				if ( answer != QMessageBox::Yes )
				{
					return true;
				}

				break;
			}

			case CellPastePlan::Apply:
			{
				break;
			}
		}

		// Apply (directly, or after the jagged confirm). A provisional target materializes the row; any other target is
		// applied in place. One undo step either way.

		if ( tableModel->is_provisional_row ( row ) )
		{
			applyingSelection = true;

			const int newRow = tableModel->element_count ();

			tableModel->materialize_provisional ( column, std::move ( decision.value ) );

			view->setCurrentIndex ( view->model ()->index ( newRow, column ) );

			applyingSelection = false;
		}
		else
		{
			tableModel->apply_cell_value ( row, column, std::move ( decision.value ), QStringLiteral ( "Paste" ) );
		}

		if ( status != nullptr )
		{
			status->show_message ( tr ( "Pasted cell" ), config::form::REFUSAL_MESSAGE_TIMEOUT );
		}

		return true;
	}
}
