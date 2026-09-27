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
#include "dialogs/MessageBox.hpp"
#include "models/IGridProjection.hpp"
#include "models/JsonFormModel.hpp"
#include "models/JsonTableModel.hpp"
#include "models/cell_presentation.hpp"
#include "services/ClipboardService.hpp"
#include "services/IDialogService.hpp"
#include "services/SelectionService.hpp"
#include "services/StatusService.hpp"
#include "views/JsonCellDelegate.hpp"
#include "views/cell_paste_plan.hpp"

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/CellPasteConverter.hpp>
#include <vje_core/services/clipboard_grid.hpp>
#include <vje_core/editing/paste_target_plan.hpp>
#include <vje_core/services/JsonSerializer.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QTableView>

#include <algorithm>
#include <optional>
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
		const GridSupport&  support,
		QObject*            parent
	)
		: QObject ( parent )
		, view       ( view )
		, projection ( projection )
		, selection  ( selection )
		, policy     ( policy )
		, tableModel ( support.tableModel )
		, formModel  ( support.formModel )
		, columnHeader ( support.columnHeader )
		, rowHeader    ( support.rowHeader )
		, clipboard  ( support.clipboard )
		, settings   ( support.settings )
		, status     ( support.status )
		, dialogs    ( support.dialogs )
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

		// EDITOR-16: a click on a CELL ends a header selection even where it does not move the current cell. The
		// current-changed route below cannot see that case -- a click on the cell that is already current emits
		// nothing -- and it is not rare: a header selection hides the current cell's indication (15h.3), so the user
		// has no way to know which cell they must avoid. Found by the MainWindow harness: with a column selected, a
		// click on the hidden current cell left the column selected, and the next Delete removed it.

		connect ( view, &QAbstractItemView::pressed, this, &FormGridController::clear_header_selection );

		connect
		(
			view->selectionModel (), &QItemSelectionModel::currentChanged,
			this, &FormGridController::handle_current_changed
		);

		// EDITOR-15's deferred key commit, the object form's counterpart of the table's provisional commit below.

		if ( formModel != nullptr )
		{
			connect
			(
				formModel, &JsonFormModel::provisional_key_committed,
				this, &FormGridController::handle_provisional_key,
				Qt::QueuedConnection
			);
		}

		// A typed-entry commit into a provisional cell (EDITOR-12). Deferred, so the array table alone wires it.

		if ( tableModel != nullptr )
		{
			connect
			(
				tableModel, &JsonTableModel::provisional_commit_pending,
				this, &FormGridController::handle_provisional_commit
			);

			// EDIT-15: the marker claims the array is in a particular order, so it cannot outlive a change to the
			// array. The model's own signal rather than modelReset / rowsInserted, which EDITOR-12's view-only
			// provisional row also raises without any element having moved.

			connect
			(
				tableModel, &JsonTableModel::presented_array_changed,
				this, [ this ] ()
				{
					sortedColumn = -1;

					if ( columnHeader != nullptr )
					{
						columnHeader->clear_sort_marker ();
					}
				}
			);

			// UNDO-05: an undo or redo scrolls to the row it acted on. A restored row lands wherever it was deleted
			// from, a removed one leaves wherever it was, and in a long array either is routinely below the part the
			// user is looking at -- so the undo worked and nothing on screen said so.
			//
			// Only a REPLAY's rows are found this way. An ordinary edit is not revealed by listening for inserts: most
			// insert where the user is already working (a grown row is the one under the keyboard), a table that
			// jumped on every insert would move under the user's hands, and the one that need not be on screen -- a
			// row pasted onto a row the user has scrolled away from -- reveals its own row, knowing which it is
			// (EDITOR-18, 2026-09-24). The row is noted while the replay's changes arrive and revealed once they all
			// have.

			if ( UndoController* const replayer = tableModel->undo_controller () )
			{
				// THE ROW A REPLAY ACTED ON, whichever way it acted (revised 2026-09-24, the user's choice of "any row an
				// undo changes"): a row it brought back, a row it took away -- revealed where it WAS, which is where the
				// row that moved up now is -- and a row whose values it put back. Three sources, because the model reports
				// the three differently; one rule for what to do with them.

				connect
				(
					tableModel, &QAbstractItemModel::rowsInserted,
					this, [ this, replayer ] ( const QModelIndex& parent, int first, int )
					{
						// The provisional row is a view affordance rather than a restored element, so it is never the
						// row to reveal -- element_count () already includes the element just inserted.

						if ( parent.isValid () || !replayer->is_replaying () || ( first >= tableModel->element_count () ) )
						{
							return;
						}

						note_replayed_row ( first );
					}
				);

				connect
				(
					tableModel, &QAbstractItemModel::rowsRemoved,
					this, [ this, replayer ] ( const QModelIndex& parent, int first, int )
					{
						if ( parent.isValid () || !replayer->is_replaying () )
						{
							return;
						}

						note_replayed_row ( first );
					}
				);

				connect
				(
					tableModel, &JsonTableModel::element_changed,
					this, [ this, replayer ] ( int row )
					{
						if ( replayer->is_replaying () )
						{
							note_replayed_row ( row );
						}
					}
				);

				connect
				(
					replayer, &UndoController::replayed,
					this, [ this ] ()
					{
						if ( rowToReveal < 0 )
						{
							return;
						}

						// CLAMPED to the rows that are left: a replay that took away the LAST row was at an index that
						// no longer exists, and the nearest thing to where it was is the new last row. Nothing left at
						// all is nothing to reveal.

						const int row = std::min ( rowToReveal, tableModel->element_count () - 1 );

						rowToReveal = -1;

						if ( row >= 0 )
						{
							reveal_row_later ( row );
						}
					}
				);
			}
		}

		// EDITOR-16 / EDIT-15. Both headers read the ONE selection this controller owns, and both raise their two
		// gestures back into it. The delegate reads the same field, so a cell, its column header and its row index
		// can never disagree about what is selected.

		cellDelegate->set_header_selection_source ( &headerSelection );

		if ( columnHeader != nullptr )
		{
			columnHeader->set_selection_source ( &headerSelection );

			connect ( columnHeader, &GridHeaderView::section_selected, this, [ this ] ( int section )
			{
				set_header_selection ( HeaderSelectionKind::Column, section );
			} );

			connect ( columnHeader, &GridHeaderView::sort_toggled, this, &FormGridController::toggle_sort );

			connect
			(
				columnHeader, &GridHeaderView::section_menu_requested,
				this, &FormGridController::show_column_menu
			);
		}

		if ( rowHeader != nullptr )
		{
			rowHeader->set_selection_source ( &headerSelection );

			connect ( rowHeader, &GridHeaderView::section_selected, this, [ this ] ( int section )
			{
				set_header_selection ( HeaderSelectionKind::Row, section );
			} );

			// EDITOR-17's drill-in. QHeaderView raises the double click itself; what this adds is that the target is
			// the ELEMENT rather than any cell of it.

			connect ( rowHeader, &QHeaderView::sectionDoubleClicked, this, &FormGridController::handle_row_index_activated );

			// EDITOR-22: the row index's context menu, the column header's counterpart.

			connect ( rowHeader, &GridHeaderView::section_menu_requested, this, &FormGridController::show_row_menu );
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

		int column = ( policy.landingColumn >= 0 ) ? policy.landingColumn : 0;

		// EDITOR-15: an EMPTY object presents with the provisional row as its ONLY row, and the form's landing column
		// is the VALUE column -- which on that row is deliberately not editable, there being no member yet to give a
		// value to. Landing there left the first member of an empty object untypeable (found by manual test,
		// 2026-08-03). The key is what brings the member into existence, so that is where the highlight belongs.

		if ( is_object_form () && formModel->is_provisional_row ( 0 ) )
		{
			column = JsonFormModel::KEY_COLUMN;
		}

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

		// EDITOR-15: the same bottom edge in the object form, and the same gesture -- but the highlight always lands on
		// the KEY column, whichever column the move came from, because the key is what brings the member into
		// existence and the value cell of a row with no member cannot be edited.

		if ( is_object_form () && downwardAdvance && clampedAtBottom && formModel->is_key_editing_allowed () &&
		     ( current.row () == formModel->member_count () - 1 ) && !formModel->has_provisional_row () )
		{
			formModel->set_provisional_row ( true );

			applyingSelection = true;

			view->setCurrentIndex ( view->model ()->index ( formModel->member_count (), JsonFormModel::KEY_COLUMN ) );

			applyingSelection = false;

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

		// EDITOR-16: a cell click and an arrow-key move both arrive here, and both end a header selection. The rule is
		// stated at the one place every current-cell move passes through rather than at each gesture that causes one,
		// so a route added later ends the selection without being told to.
		//
		// A header click does NOT reach here: QTableView's own selectColumn / selectRow are no-ops under
		// SingleSelection with SelectItems, so nothing moves the current cell and the selection survives its own
		// gesture.

		clear_header_selection ();

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

	void FormGridController::populate_field_menu ( QMenu& menu )
	{
		// EDITOR-20: the object form's VALUE cell answers the right-click with the three commands that already act on
		// it from the keyboard (EDITOR-14). Nothing else -- the node commands belong to the KEY, which is the cell
		// that names the node they would act on.
		//
		// Always enabled, for populate_column_menu's reason. Each of the three refuses in its own words where it has
		// to: a key column that SET-05a has locked, a clipboard holding a row or a column, a value the matrix cannot
		// convert (which since 2026-08-20 asks rather than refuses).

		QAction* const cutAction   = menu.addAction ( tr ( "Cut" ) );
		QAction* const copyAction  = menu.addAction ( tr ( "Copy" ) );
		QAction* const pasteAction = menu.addAction ( tr ( "Paste" ) );

		connect ( cutAction,   &QAction::triggered, this, [ this ] () { cut_cell   (); } );
		connect ( copyAction,  &QAction::triggered, this, [ this ] () { copy_cell  (); } );
		connect ( pasteAction, &QAction::triggered, this, [ this ] () { paste_cell (); } );
	}

	FormGridController::CellMenu FormGridController::menu_for_cell ( const QModelIndex& cell ) const
	{
		if ( !cell.isValid () )
		{
			return CellMenu::None;
		}

		// The FIELD menu is asked about first, which costs nothing -- the two columns are different numbers, and the
		// array table sets neither, so it answers None whatever is clicked. Its cells' commands live on the column
		// header (EDITOR-19), which is where the selection they act on is made.

		if ( ( policy.fieldMenuColumn >= 0 ) && ( cell.column () == policy.fieldMenuColumn ) )
		{
			return CellMenu::Field;
		}

		if ( ( policy.contextMenuColumn >= 0 ) && ( cell.column () == policy.contextMenuColumn ) )
		{
			return CellMenu::Node;
		}

		return CellMenu::None;
	}

	void FormGridController::handle_context_menu ( const QPoint& position )
	{
		// The position arrives in VIEWPORT coordinates, which is what indexAt wants -- measured, not assumed. A
		// context-menu gesture lands on the viewport, whose own policy is DefaultContextMenu, so it ignores the event
		// and Qt propagates it to the view; the propagation does NOT translate the position (probed 2026-08-22:
		// QPoint(3,3) in, QPoint(3,3) out, against a viewport origin of QPoint(1,1)).
		//
		// Written down because the obvious reading is the other one, and a mapFrom "correction" based on it shifts
		// every menu by the frame width -- which is a regression the frame being 1 px would have hidden.

		const QModelIndex target = view->indexAt ( position );
		const CellMenu    wanted = menu_for_cell ( target );

		if ( wanted == CellMenu::None )
		{
			return;
		}

		// EITHER MENU ACTS ON THE CELL THAT WAS CLICKED, not on whatever was current before it -- the rule the tree's
		// context menu follows and the one show_column_menu follows for a column. The field menu's three commands read
		// the CURRENT cell, so making it current is not bookkeeping here: it is what decides what the menu is about.

		view->setCurrentIndex ( target );

		if ( wanted == CellMenu::Field )
		{
			QMenu menu ( view );

			populate_field_menu ( menu );

			menu.exec ( view->viewport ()->mapToGlobal ( position ) );

			return;
		}

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

		// EDITOR-16's keyboard route, the spreadsheet spelling: Shift+Space selects the current cell's row and
		// Ctrl+Space its column, so neither selection needs a mouse (NFR-05).
		//
		// These are handled BEFORE the unmodified-key gate below, and swallowed rather than passed on -- Shift+Space
		// produces an ordinary space character, which QTableView's AnyKeyPressed edit trigger would otherwise take as
		// "start editing this cell with a space".

		if ( ( keyEvent->key () == Qt::Key_Space ) && is_array_table () )
		{
			if ( keyEvent->modifiers () == Qt::ShiftModifier )
			{
				select_current_row ();

				return true;
			}

			if ( keyEvent->modifiers () == Qt::ControlModifier )
			{
				select_current_column ();

				return true;
			}
		}

		if ( handle_delete_key ( *keyEvent ) || handle_paste_over_key ( *keyEvent ) )
		{
			return true;
		}

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

		// EDITOR-15: the same Down at the object form's bottom edge. This was MISSING until 2026-08-03 -- the growth
		// was wired only into handle_editing_moved, which is the DELEGATE's post-commit movement, so Enter on the last
		// field grew a row and the plain Down arrow did not. Two routes reach the bottom edge and both are the
		// requirement; a commit is not the only way a user gets there.

		if ( ( keyEvent->key () == Qt::Key_Down ) && is_object_form () && formModel->is_key_editing_allowed () )
		{
			const QModelIndex current = view->currentIndex ();

			if ( current.isValid () && formModel->is_provisional_row ( current.row () ) )
			{
				return true;   // No stacking; the provisional row is the last there is.
			}

			const bool atLastRealRow = current.isValid ()
			                        && ( current.row () == formModel->member_count () - 1 )
			                        && !formModel->has_provisional_row ();

			if ( atLastRealRow )
			{
				formModel->set_provisional_row ( true );

				applyingSelection = true;

				view->setCurrentIndex ( view->model ()->index ( formModel->member_count (), JsonFormModel::KEY_COLUMN ) );

				applyingSelection = false;

				return true;
			}

			return QObject::eventFilter ( watched, event );
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

		// EDITOR-15: activating the provisional row's VALUE cell cannot work -- there is no member yet to give a value
		// to -- and until 2026-08-03 it did nothing whatsoever, which is precisely the silent refusal VAL-05 forbids.
		// Say why, and put the highlight where the work actually starts, so the correction is one step rather than a
		// hunt for which of two blank-looking cells is the live one.
		//
		// The keystroke that got here is deliberately NOT redirected into the key editor: a character typed at a value
		// is not a key, and quietly making it one would replace a dead end with a wrong result.

		if ( is_object_form () && formModel->is_provisional_row ( index.row () )
		     && ( index.column () == JsonFormModel::VALUE_COLUMN ) )
		{
			applyingSelection = true;

			view->setCurrentIndex ( view->model ()->index ( index.row (), JsonFormModel::KEY_COLUMN ) );

			applyingSelection = false;

			report_field_clipboard ( tr ( "Enter a key first, then its value." ) );

			return;
		}

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
		// EDITOR-15's half. Same rule, same reason an invalid current does not count (see below).

		if ( is_object_form () )
		{
			if ( materializePending || applyingSelection || !formModel->has_provisional_row () )
			{
				return;
			}

			if ( current.isValid () && ( current.row () != formModel->member_count () ) )
			{
				formModel->set_provisional_row ( false );
			}

			return;
		}

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

	void FormGridController::handle_provisional_key ( const QString& key )
	{
		if ( !is_object_form () || !formModel->has_provisional_row () )
		{
			return;
		}

		materializePending = true;

		// An empty key, a duplicate (VAL-02), or key editing switched off between the keystroke and this queued call:
		// the row is abandoned rather than half-created. Reporting the duplicate is Rename Key's business, not this
		// gesture's -- the editor's live rival-key guard is what the user sees first.

		if ( !formModel->materialize_provisional ( key ) )
		{
			formModel->set_provisional_row ( false );

			materializePending = false;

			return;
		}

		// The highlight moves RIGHT to the new member's value, which EDITOR-13 has just made editable -- so a whole
		// member is type-key, Enter, type-value, Enter. A deliberate one-off: Enter commits DOWNWARD everywhere else.

		applyingSelection = true;

		view->setCurrentIndex ( view->model ()->index ( formModel->member_count () - 1, JsonFormModel::VALUE_COLUMN ) );

		applyingSelection = false;

		materializePending = false;

		if ( status != nullptr )
		{
			status->show_message ( tr ( "Added %1" ).arg ( key ), config::form::REFUSAL_MESSAGE_TIMEOUT );
		}
	}

	bool FormGridController::is_object_form () const
	{
		return formModel != nullptr;
	}

	bool FormGridController::current_cell_is_key_column () const
	{
		return is_object_form () && ( view->currentIndex ().column () == JsonFormModel::KEY_COLUMN );
	}

	//=================================================================================================================
	// The row and column clipboard (EDITOR-18)
	//
	// A ROW IS AN ELEMENT AND A COLUMN IS A MEMBER ACROSS ELEMENTS. That single asymmetry decides what reaches the
	// clipboard, what an external target receives, and what a delete removes; everything else here is EDITOR-11's
	// per-cell matrix applied several times.
	//=================================================================================================================

	bool FormGridController::delete_cell ()
	{
		return delete_table_selection ();
	}

	bool FormGridController::cell_delete_active () const
	{
		return table_selection_active ();
	}

	bool FormGridController::handle_delete_key ( const QKeyEvent& keyEvent )
	{
		// EDITOR-18 has promised this key since the day it was written, and until 2026-09-23 it never arrived. The
		// Delete QAction is scoped to the TREE (TreeViewPane::set_context_actions, so a window-wide Delete cannot fire
		// while the user is typing in a field), so with the table focused no shortcut claims the key at all and it
		// reaches this filter as an ordinary keystroke. The route through the action was only ever exercised by
		// calling cell_delete () directly -- which proves the command, not the key that was meant to reach it.
		//
		// ONLY WITH A HEADER SELECTION LIVE. With none, the key falls through exactly as it did before and QTableView
		// does nothing with it, so a current cell's Delete is unchanged. Scoping the action to the window instead was
		// the obvious fix and the wrong one: delete_node falls back to the TREE's node when the table has nothing
		// selected to remove, so Delete pressed on an ordinary cell would have removed the whole array.
		//
		// The keypad's Delete arrives carrying KeypadModifier, which the shortcut map strips before it matches -- so
		// the tree answers that key too, and stripping it here keeps the two keys one key in both panes.

		const Qt::KeyboardModifiers modifiers = keyEvent.modifiers () & ~Qt::KeypadModifier;

		if ( ( keyEvent.key () != Qt::Key_Delete ) || ( modifiers != Qt::NoModifier ) || !table_selection_active () )
		{
			return false;
		}

		return delete_table_selection ();
	}

	bool FormGridController::handle_paste_over_key ( const QKeyEvent& keyEvent )
	{
		// Paste Over's key. The table's own, for handle_delete_key's reason -- there is no window command for it at
		// all -- and only with a row or column selected, the one place Paste Over means anything. Ctrl+Shift+V was
		// free everywhere in the application when it was taken.

		const Qt::KeyboardModifiers modifiers = keyEvent.modifiers () & ~Qt::KeypadModifier;

		if ( ( keyEvent.key () != Qt::Key_V ) || ( modifiers != ( Qt::ControlModifier | Qt::ShiftModifier ) )
		  || !table_selection_active () )
		{
			return false;
		}

		return paste_over_table_selection ();
	}

	bool FormGridController::table_selection_active () const
	{
		return is_array_table () && ( clipboard != nullptr ) && headerSelection.is_active ();
	}

	std::vector<std::pair<int, int>> FormGridController::selected_cell_positions () const
	{
		std::vector<std::pair<int, int>> positions;

		if ( !is_array_table () || !headerSelection.is_active () )
		{
			return positions;
		}

		if ( headerSelection.kind == HeaderSelectionKind::Row )
		{
			// The provisional row has no cells at all: it is the row that does not exist yet, so every value a paste
			// carries is growth and there is nothing for a copy to read.

			if ( tableModel->is_provisional_row ( headerSelection.index ) )
			{
				return positions;
			}

			for ( int column = 0; column < tableModel->columnCount (); ++column )
			{
				positions.emplace_back ( headerSelection.index, column );
			}
		}
		else
		{
			// element_count () rather than rowCount (): EDITOR-12's provisional row is not an element, so it has no
			// cell to copy from and none to assign into. What a paste puts THERE is growth, handled as such.

			for ( int row = 0; row < tableModel->element_count (); ++row )
			{
				positions.emplace_back ( row, headerSelection.index );
			}
		}

		return positions;
	}

	std::vector<TableSelectionRef> FormGridController::selected_cells () const
	{
		std::vector<TableSelectionRef> cells;

		for ( const auto& [ row, column ] : selected_cell_positions () )
		{
			// grid_node answers nullptr for a MISSING cell, which is exactly the "absent" the clipboard format and
			// the paste both want -- so the ragged case needs no branch of its own anywhere in this family.
			//
			// The NAME travels with it because a paste that GROWS its target needs one and only the source has it. It is
			// the column's member key in object mode and the ARRAY's name in single-value mode (section 2.12's rule,
			// EDIT-16) -- so a scalar array's column is called after the array rather than by a placeholder.

			cells.push_back ( TableSelectionRef { tableModel->grid_node ( row, column ), tableModel->column_name ( column ) } );
		}

		return cells;
	}

	QString FormGridController::selected_cells_plain_text () const
	{
		// A ROW is a node, so it writes its JSON, exactly as EDIT-14 writes an array element -- it drops straight into
		// an array literal.
		//
		// A COLUMN is not a node. It is a slice ACROSS nodes, so there is no "its JSON" to write; each value's plain
		// text one per line is what any external target with a notion of a column expects, and an absent cell writes
		// an EMPTY LINE, which is section 2.12's CSV rule for the same reason -- it keeps "does not have that member"
		// distinguishable from "has it, and it is null".

		if ( headerSelection.kind == HeaderSelectionKind::Row )
		{
			const JsonNode* const element = tableModel->element_node ( headerSelection.index );

			return ( element != nullptr ) ? JsonSerializer::serialize ( *element ) : QString ();
		}

		QStringList lines;

		for ( const TableSelectionRef& cell : selected_cells () )
		{
			lines.append ( ( cell.value != nullptr ) ? ClipboardService::cell_plain_text ( *cell.value ) : QString () );
		}

		return lines.join ( QStringLiteral ( "\n" ) );
	}

	QString FormGridController::selection_display_text () const
	{
		if ( headerSelection.kind == HeaderSelectionKind::Row )
		{
			return tr ( "row %1" ).arg ( headerSelection.index );
		}

		const std::optional<QString> key = tableModel->column_key ( headerSelection.index );

		// A single-value table's column has no member key to name, so it is named by what it is: the values.

		return key.has_value () ? tr ( "column \"%1\"" ).arg ( key.value () ) : tr ( "the value column" );
	}

	QString FormGridController::cell_display_text ( int row, int column ) const
	{
		const std::optional<QString> key = tableModel->column_key ( column );

		return key.has_value ()
		     ? tr ( "row %1, column \"%2\"" ).arg ( row ).arg ( key.value () )
		     : tr ( "row %1" ).arg ( row );
	}

	void FormGridController::report_table_clipboard ( const QString& message )
	{
		if ( status != nullptr )
		{
			status->show_message ( message, config::form::REFUSAL_MESSAGE_TIMEOUT );
		}
	}

	void FormGridController::refuse_table_command ( const QString& message )
	{
		// BOTH channels (VAL-05): a refusal is the one outcome the user cannot see for themselves. Phase 12.5 set this
		// rule for a refused export and Phase 15 generalized it; this is the same rule reached from the grid.

		show_message_box ( view, MessageKind::Warning, tr ( "Paste" ), message );

		report_table_clipboard ( message );
	}

	bool FormGridController::copy_table_selection ()
	{
		if ( !table_selection_active () )
		{
			return false;
		}

		const std::vector<TableSelectionRef> cells = selected_cells ();

		if ( cells.empty () )
		{
			// Reached by the provisional row, which holds no values -- a handled no-op rather than a decline, for
			// copy_cell's reason: falling through would copy the whole selected NODE over the clipboard.

			report_table_clipboard ( tr ( "Nothing to copy" ) );

			return true;
		}

		const TableSelectionShape shape = ( headerSelection.kind == HeaderSelectionKind::Row )
		                                ? TableSelectionShape::Row
		                                : TableSelectionShape::Column;

		clipboard->copy_table_selection ( shape, cells, selected_cells_plain_text (), tableModel->is_object_table () );

		report_table_clipboard
		(
			tr ( "Copied %1 (%n value(s))", nullptr, static_cast<int> ( cells.size () ) ).arg ( selection_display_text () )
		);

		return true;
	}

	bool FormGridController::cut_table_selection ()
	{
		if ( !table_selection_active () )
		{
			return false;
		}

		// Cut is copy plus delete, so what reaches the clipboard is exactly what Copy would have placed there and the
		// two gestures can never disagree about what was taken (EDITOR-18, EDITOR-14's own rule one level up).

		const std::vector<TableSelectionRef> cells = selected_cells ();

		if ( cells.empty () )
		{
			report_table_clipboard ( tr ( "Nothing to cut" ) );

			return true;
		}

		const TableSelectionShape shape = ( headerSelection.kind == HeaderSelectionKind::Row )
		                                ? TableSelectionShape::Row
		                                : TableSelectionShape::Column;

		clipboard->copy_table_selection ( shape, cells, selected_cells_plain_text (), tableModel->is_object_table () );

		return delete_table_selection ();
	}

	bool FormGridController::delete_table_selection ()
	{
		if ( !table_selection_active () )
		{
			return false;
		}

		// The provisional row is not an element, so there is nothing to remove. Said in its own words rather than
		// left to Delete's Rejected wording, which is about the document root and would be simply untrue here.

		if ( ( headerSelection.kind == HeaderSelectionKind::Row )
		  && tableModel->is_provisional_row ( headerSelection.index ) )
		{
			report_table_clipboard ( tr ( "That row does not exist yet, so there is nothing to delete." ) );

			return true;
		}

		const bool                isRow   = ( headerSelection.kind == HeaderSelectionKind::Row );
		const HeaderSelectionKind kind    = headerSelection.kind;
		const int                 removed = headerSelection.index;
		const JsonPointer         target  = isRow ? tableModel->element_pointer ( removed )
		                                          : tableModel->presented_pointer ();

		const EditOutcome outcome = isRow ? tableModel->delete_row    ( removed )
		                                  : tableModel->delete_column ( removed );

		// THE SELECTION MOVES TO WHAT TOOK THE REMOVED ONE'S PLACE (the user's direction, 2026-09-23): the next row or
		// column along, which is now at the same index, so pressing Delete again removes the next one and the
		// highlight always says what the next command acts on. Every route that removes comes through here -- the
		// Delete key, both header menus, Document > Delete Node and its toolbar button, and Cut -- so all of them do
		// it. A refused delete removed nothing, and ends the selection as it always did.

		if ( outcome == EditOutcome::Applied )
		{
			select_replacement ( kind, removed );
		}
		else
		{
			clear_header_selection ();
		}

		emit edit_reported ( isRow ? EditCommand::Delete : EditCommand::DeleteColumn, outcome, target );

		return true;
	}

	void FormGridController::select_replacement ( HeaderSelectionKind kind, int removedIndex )
	{
		// What is left, counted by the model after the removal has re-projected it. element_count () rather than
		// rowCount () for a row: EDITOR-12's provisional row trails the real ones, and a row that does not exist yet
		// is no replacement -- deleting the last real row with the placeholder showing selects the real row before it.

		const int remaining = ( kind == HeaderSelectionKind::Row ) ? tableModel->element_count ()
		                                                           : tableModel->columnCount ();

		// The one now at the removed index; the one before it where the LAST was removed; nothing where none remain.

		const int replacement = std::min ( removedIndex, remaining - 1 );

		if ( replacement < 0 )
		{
			clear_header_selection ();

			return;
		}

		set_header_selection ( kind, replacement );
	}

	bool FormGridController::rename_table_column ()
	{
		const bool addressable = is_array_table ()
		                      && ( headerSelection.kind == HeaderSelectionKind::Column )
		                      && ( headerSelection.index >= 0 );

		if ( !addressable || ( dialogs == nullptr ) )
		{
			return false;
		}

		const std::optional<QString> current = tableModel->column_key ( headerSelection.index );

		// The prompt is seeded with the name it HAS, so the common edit -- correcting a typo in a long key -- is a
		// change rather than a retype. A single-value table has no key to seed it with and no member to rename, so
		// the refusal below is reached with an empty box rather than the command being hidden (section 2.12's rule,
		// which the whole of this menu follows).

		const std::optional<QString> answer = dialogs->ask_text
		(
			tr ( "Rename Column" ),
			tr ( "New column name:" ),
			current.value_or ( QString () )
		);

		if ( !answer.has_value () )
		{
			return true;                                   // Cancelled: reported in neither channel (Phase 12.5).
		}

		const JsonPointer target  = tableModel->presented_pointer ();
		const EditOutcome outcome = tableModel->rename_column ( headerSelection.index, answer.value () );

		// THE SELECTION STAYS, for clear_table_column's reason: the column is still there and still the obvious thing
		// to act on next. Its NAME changed, so the header repaints -- which the model's own reprojection does.

		emit edit_reported ( EditCommand::RenameColumn, outcome, target );

		return true;
	}

	bool FormGridController::clear_table_column ()
	{
		// NOT table_selection_active (): that gate additionally requires a clipboard, which is right for the four
		// commands that read or write one and would make this command silently dead without it.

		if ( !is_array_table () || ( headerSelection.kind != HeaderSelectionKind::Column ) || ( headerSelection.index < 0 ) )
		{
			return false;
		}

		const JsonPointer target  = tableModel->presented_pointer ();
		const EditOutcome outcome = tableModel->clear_column ( headerSelection.index );

		// THE SELECTION STAYS, which is the one place this differs from delete_table_selection. A delete's subject is
		// gone, so a selection naming it would point the next command at a different column; a clear's subject is
		// still there, still selected, and still the obvious thing to paste into next.

		emit edit_reported ( EditCommand::ClearColumn, outcome, target );

		return true;
	}

	bool FormGridController::clear_table_row ()
	{
		// NOT table_selection_active (), for clear_table_column's reason: nothing here reads or writes a clipboard.

		if ( !is_array_table () || ( headerSelection.kind != HeaderSelectionKind::Row ) || ( headerSelection.index < 0 ) )
		{
			return false;
		}

		// The provisional row is not an element and has no cells, so there is nothing to clear -- said in its own
		// words, for delete_table_selection's reason: ClearRow's Rejected wording would describe a different failure.

		if ( tableModel->is_provisional_row ( headerSelection.index ) )
		{
			report_table_clipboard ( tr ( "That row does not exist yet, so there is nothing to clear." ) );

			return true;
		}

		// The target is the ELEMENT, where clear_table_column's is the array: a row is an element, so naming it is
		// both true and short, while a column is a slice across elements that no one pointer names.

		const JsonPointer target  = tableModel->element_pointer ( headerSelection.index );
		const EditOutcome outcome = tableModel->clear_row ( headerSelection.index );

		// THE SELECTION STAYS, for clear_table_column's reason: the row is still there, still selected, and still the
		// obvious thing to paste into next.

		emit edit_reported ( EditCommand::ClearRow, outcome, target );

		return true;
	}

	CellTarget FormGridController::target_for_cell ( int row, int column ) const
	{
		// The target cell kind decides the conversion matrix column (EDITOR-11). A provisional / null / missing cell is
		// Untyped (takes the value as-is; a container is still shape-checked).

		if ( tableModel->is_provisional_row ( row ) )
		{
			return CellTarget::Untyped;
		}

		JsonNode* const   node    = tableModel->grid_node ( row, column );
		const CellContent content = cell_content ( node );

		if ( content == CellContent::Container )
		{
			return ( node->kind () == JsonKind::Object ) ? CellTarget::Object : CellTarget::Array;
		}

		if ( content != CellContent::Scalar )
		{
			return CellTarget::Untyped;
		}

		switch ( node->kind () )
		{
			case JsonKind::String:  return CellTarget::String;
			case JsonKind::Number:  return CellTarget::Number;
			case JsonKind::Boolean: return CellTarget::Boolean;
			default:                return CellTarget::Untyped;
		}
	}

	std::vector<const JsonNode*> FormGridController::column_values_excluding ( int column, int row ) const
	{
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

		return columnValues;
	}

	int FormGridController::apply_pasted_growth ( bool pastingRow, std::vector<PastedGrowth>& growth )
	{
		if ( growth.empty () )
		{
			return 0;
		}

		int created = 0;

		if ( pastingRow && tableModel->is_provisional_row ( headerSelection.index ) )
		{
			// The provisional row: there is no element to add members to, so the whole pasted row becomes ONE new
			// element. This is EDITOR-12's materialization reached by a paste, which that requirement already
			// defines -- what is new is only that several values arrive at once.

			std::vector<JsonTableModel::PastedMember> members;

			for ( PastedGrowth& entry : growth )
			{
				members.push_back ( { entry.key, std::move ( entry.value ) } );
			}

			return ( tableModel->append_pasted_element ( members ) == EditOutcome::Applied ) ? 1 : 0;
		}

		if ( pastingRow )
		{
			// A ROW grows by gaining MEMBERS: the source row had columns this table does not. Each is created on the
			// pasted-into element and as `null` on every other one, so the column exists across the whole array
			// rather than making it ragged behind the user's back.
			//
			// A keyless growth value has nothing to be called, so there is no member to make and it is dropped -- the
			// only way to reach it is pasting a scalar array's row into a wider table, where the source genuinely
			// carries no name for the extra value.

			for ( PastedGrowth& entry : growth )
			{
				if ( entry.key.isNull () )
				{
					continue;
				}

				if ( tableModel->create_pasted_member ( headerSelection.index, entry.key, std::move ( entry.value ) )
				     == EditOutcome::Applied )
				{
					++created;
				}
			}

			return created;
		}

		// A COLUMN grows by APPENDING ELEMENTS. The key each appended element is written under is the TARGET column's
		// where the table has one, and the SOURCE's where it does not -- which is the empty-array case, and the whole
		// of why the source's key travels on the clipboard. Pasting into a named column extends that column; pasting
		// into an array with no shape yet adopts the name the values came with.

		// THE NAME IS plan_paste_target'S ANSWER, not this route's own -- two callers computing it separately is how
		// one rule becomes two that agree until they do not, and it is the defect EDITOR-18 shipped with, where an
		// unnamed target fell back to the SOURCE's key and appended named-member objects into a scalar array.
		//
		// PasteRoute::Column, and it is load-bearing rather than bookkeeping. A single-column table has a name but no
		// member KEY, so column_key is nullopt here -- exactly what the tree route passes -- and until 15h.2 that
		// absence was what told the two routes apart. They now want opposite things on precisely that target (the
		// tree reshapes the array, this route overwrites its values), so the route has to say so itself.

		const std::optional<QString> targetKey = tableModel->column_key ( headerSelection.index );

		const PasteTargetPlan plan = plan_paste_target
		(
			tableModel->presented_array (),
			growth.empty () ? QString () : growth.front ().key,
			PasteRoute::Column,
			targetKey
		);

		for ( PastedGrowth& entry : growth )
		{
			std::vector<JsonTableModel::PastedMember> members;

			members.push_back ( { plan.name.isEmpty () ? QString () : plan.name, std::move ( entry.value ) } );

			if ( tableModel->append_pasted_element ( members ) == EditOutcome::Applied )
			{
				++created;
			}
		}

		return created;
	}

	bool FormGridController::refuses_table_paste_source ( const TableSelectionValue& source )
	{
		if ( source.is_empty () )
		{
			refuse_table_command ( tr ( "The clipboard does not hold a table row or column." ) );

			return true;
		}

		const bool wantsRow = ( headerSelection.kind == HeaderSelectionKind::Row );
		const bool hasRow   = ( source.shape == TableSelectionShape::Row );

		// EDITOR-18: same shape only, and the refusal NAMES both -- which is why the private format carries its shape
		// rather than the two shapes carrying a format each. A reader that merely failed to find its own format could
		// say only that nothing recognizable was there.

		if ( wantsRow != hasRow )
		{
			refuse_table_command
			(
				hasRow ? tr ( "A row cannot be pasted onto a column." )
				       : tr ( "A column cannot be pasted onto a row." )
			);

			return true;
		}

		return false;
	}

	bool FormGridController::confirm_jagged_paste ()
	{
		const QMessageBox::StandardButton answer = ask_message_box
		(
			view,
			MessageKind::Warning,
			tr ( "Paste" ),
			tr ( "Some of these values do not match the array's structure. Pasting them will make the array "
			     "structurally heterogeneous (\"jagged\"). Continue?" ),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No,
			QMessageBox::No
		);

		return answer == QMessageBox::Yes;
	}

	bool FormGridController::paste_table_selection ()
	{
		// EDITOR-18 (revised 2026-09-23): Paste INSERTS. A copied row goes in front of the selected row, which moves
		// down one; a copied column goes in front of the selected column, which moves right one. Nothing the target
		// already held is overwritten -- that is Paste Over's, one item below this in both header menus.
		//
		// IN FRONT OF rather than after, which is the spreadsheet's Insert Copied Cells and the user's choice: the
		// inserted row or column lands exactly where the user aimed, at the index they clicked.

		if ( !table_selection_active () )
		{
			return false;
		}

		TableSelectionValue source = clipboard->table_selection ();

		if ( refuses_table_paste_source ( source ) )
		{
			return true;
		}

		return ( headerSelection.kind == HeaderSelectionKind::Row ) ? insert_table_row    ( source )
		                                                            : insert_table_column ( source );
	}

	bool FormGridController::insert_table_row ( TableSelectionValue& source )
	{
		// BY KEY, the user's choice over by position: the new element carries the copied row's own member names, a
		// column the row lacks is null in it, and a name the table lacks becomes a new column that every other element
		// gains as null. A row is an ELEMENT (EDITOR-18's asymmetry), and an element's members are named, so its names
		// are what it brings -- where positional mapping would rename a copied `email` to whatever the target's second
		// column happened to be called.

		const int row = headerSelection.index;

		std::vector<JsonTableModel::PastedMember> members;

		for ( TableSelectionCell& cell : source.cells )
		{
			members.push_back ( { cell.key, std::move ( cell.value ) } );
		}

		QStringList                     newKeys;
		std::unique_ptr<JsonNode>       element = tableModel->build_pasted_element ( members, source.keyed, &newKeys );

		if ( element == nullptr )
		{
			report_table_clipboard ( tr ( "Nothing to paste" ) );

			return true;
		}

		// PLANNED BEFORE APPLIED, EDITOR-18's rule: a value that lands in a column the table already has is shape-
		// checked against that column exactly as a paste into an empty cell is (the target is Untyped -- there is no
		// cell there to convert to, so no type question arises), and on an array of VALUES the element itself is
		// checked against the elements. A new column has nothing to be checked against.

		const bool jaggedAllowed = ( settings != nullptr )
		                        && settings->value_bool ( settings_keys::FORM_ALLOW_JAGGED_PASTE, false );

		bool needsJaggedConfirm = false;

		const auto planned = [ this, jaggedAllowed, &needsJaggedConfirm ] ( const JsonNode& value, int column, const QString& where )
		{
			const CellPasteDecision decision = plan_cell_paste
			(
				value, CellTarget::Untyped, column_values_excluding ( column, -1 ), jaggedAllowed
			);

			if ( decision.plan == CellPastePlan::Incompatible )
			{
				refuse_table_command ( tr ( "%1 (%2)" ).arg ( decision.message, where ) );

				return false;
			}

			needsJaggedConfirm = needsJaggedConfirm || ( decision.plan == CellPastePlan::NeedsJaggedConfirm );

			return true;
		};

		if ( tableModel->is_object_table () )
		{
			for ( int column = 0; column < tableModel->columnCount (); ++column )
			{
				const std::optional<QString> key   = tableModel->column_key ( column );
				const JsonNode* const        value = key.has_value () ? element->find_member ( key.value () ) : nullptr;

				// The fill's nulls are what a column the row lacked becomes, and null is the shape check's wildcard.

				if ( ( value == nullptr ) || ( value->kind () == JsonKind::Null ) )
				{
					continue;
				}

				if ( !planned ( *value, column, tr ( "column \"%1\"" ).arg ( key.value () ) ) )
				{
					return true;
				}
			}
		}
		else if ( ( tableModel->element_count () > 0 ) && !planned ( *element, 0, tr ( "row %1" ).arg ( row ) ) )
		{
			return true;
		}

		if ( needsJaggedConfirm && !confirm_jagged_paste () )
		{
			return true;
		}

		const EditOutcome outcome = tableModel->insert_pasted_element ( row, std::move ( element ), newKeys );

		if ( outcome == EditOutcome::Rejected )
		{
			refuse_table_command ( tr ( "The row could not be inserted here." ) );

			return true;
		}

		// The inserted row takes the index the selection named -- the row it was pasted in front of has moved down one
		// -- so selecting what was pasted is selecting that index again. Set explicitly rather than trusted to have
		// survived: an insert that changes the table's mode re-projects it from scratch.

		set_header_selection ( HeaderSelectionKind::Row, row );

		// EDITOR-18 (2026-09-24): the selected row may be one the user has since scrolled away from, and a paste there
		// would otherwise land out of sight -- the same "it worked and nothing said so" as UNDO-05's undo.

		reveal_row_later ( row );

		QString message = tr ( "Inserted a row at %1" ).arg ( row );

		if ( !newKeys.isEmpty () )
		{
			message += tr ( " -- %n column(s) added", nullptr, static_cast<int> ( newKeys.size () ) );
		}

		report_table_clipboard ( message );

		return true;
	}

	bool FormGridController::insert_table_column ( TableSelectionValue& source )
	{
		// EDIT-16's column paste, placed. A new column is exactly what the TREE route's paste makes -- a column named
		// after the source, de-duplicated by EDIT-07's sequence where the name is taken, null where the source runs
		// short, and appended elements where it runs long; a single-column array is reshaped to take it -- so this is
		// that paste, applied by the one function that already applies it, with PasteRoute::InsertColumn saying WHERE:
		// in front of the column aimed at rather than last.
		//
		// A clashing name becomes `name (copy)` (the user's choice over refusing). An insert never writes into an
		// existing cell, so "taken" means ANY element carries the name -- see PasteRoute::InsertColumn.

		std::vector<UndoController::PastedValue> values;

		for ( TableSelectionCell& cell : source.cells )
		{
			values.push_back ( { std::move ( cell.value ) } );
		}

		const QString                listName       = source.cells.front ().key;
		const std::optional<QString> targetKey      = tableModel->column_key ( headerSelection.index );
		const int                    elementsBefore = tableModel->element_count ();

		UndoController* const undo = tableModel->undo_controller ();

		if ( undo == nullptr )
		{
			return false;
		}

		QString     refusal;
		QString     pastedName;
		EditOutcome outcome = EditOutcome::Unchanged;

		{
			// One undo step named for the gesture. paste_value_list opens its own scope inside this one, which nesting
			// makes a no-op (Q49); the document's notifications arrive when THIS one closes, so the table has
			// re-projected by the time the block below looks for the new column.

			UndoController::MacroScope macro ( *undo, QStringLiteral ( "Paste Column" ) );

			tableModel->set_provisional_row ( false );

			outcome = undo->paste_value_list
			(
				tableModel->presented_pointer (),
				values,
				listName,
				PasteRoute::InsertColumn,
				targetKey,
				&refusal,
				&pastedName
			);
		}

		if ( outcome == EditOutcome::Rejected )
		{
			refuse_table_command ( refusal.isEmpty () ? tr ( "The column could not be inserted here." ) : refusal );

			return true;
		}

		if ( outcome == EditOutcome::Unchanged )
		{
			report_table_clipboard ( tr ( "Nothing to paste" ) );

			return true;
		}

		// Select what was pasted, found by the name it was written under. An EMPTY array took the values bare and has
		// the one column, which is also where a scalar array's reshaped values would be found if the name were ever
		// missing -- so falling back to column 0 is right rather than merely safe.

		const int column = std::max ( 0, tableModel->column_index ( pastedName ) );

		set_header_selection ( HeaderSelectionKind::Column, column );

		QString message = pastedName.isEmpty ()
		                ? tr ( "Pasted %n value(s)", nullptr, static_cast<int> ( values.size () ) )
		                : tr ( "Inserted column \"%1\"" ).arg ( pastedName );

		const int grown = tableModel->element_count () - elementsBefore;

		if ( grown > 0 )
		{
			message += tr ( " -- %n element(s) added", nullptr, grown );
		}

		report_table_clipboard ( message );

		return true;
	}

	bool FormGridController::paste_over_table_selection ()
	{
		if ( !table_selection_active () )
		{
			return false;
		}

		TableSelectionValue source = clipboard->table_selection ();

		if ( refuses_table_paste_source ( source ) )
		{
			return true;
		}

		const bool wantsRow  = ( headerSelection.kind == HeaderSelectionKind::Row );
		const int  targetRow = headerSelection.index;

		const std::vector<std::pair<int, int>> targets = selected_cell_positions ();

		const bool jaggedAllowed = ( settings != nullptr )
		                        && settings->value_bool ( settings_keys::FORM_ALLOW_JAGGED_PASTE, false );

		// THE WHOLE PASTE IS PLANNED BEFORE ANY OF IT IS APPLIED. One incompatible cell refuses the entire paste with
		// a single message naming the first offending position, and one cell needing the SET-05 confirmation asks for
		// it ONCE for the paste. Twenty message boxes would be unusable, and a half-applied column would be worse.
		//
		// The plan has TWO parts, because a paste can now extend its target (EDITOR-18, revised 2026-08-18): the
		// assignments land on cells that already exist, and the growth creates the ones that do not. Both are built
		// before either is applied, so a refusal still leaves the document untouched.

		std::vector<JsonTableModel::CellAssignment> assignments;
		std::vector<PastedGrowth>                   growth;
		bool                                        needsJaggedConfirm = false;
		bool                                        needsTypeConfirm   = false;

		for ( std::size_t index = 0; index < source.cells.size (); ++index )
		{
			TableSelectionCell& cell = source.cells [ index ];

			// An ABSENT source cell leaves its target untouched, and creates nothing where there is no target:
			// EDITOR-11 already makes copy and cut on a missing cell no-ops, and a paste of nothing is nothing.

			if ( cell.value == nullptr )
			{
				continue;
			}

			// Beyond the target's extent, so this value has nowhere to land yet. It is GROWTH rather than a truncation
			// (the rule before 2026-08-18), and it is planned rather than applied here.

			if ( index >= targets.size () )
			{
				growth.push_back ( PastedGrowth { cell.key, std::move ( cell.value ) } );

				continue;
			}

			const auto& [ row, column ] = targets [ index ];

			CellPasteDecision decision = plan_cell_paste
			(
				*cell.value,
				target_for_cell ( row, column ),
				column_values_excluding ( column, row ),
				jaggedAllowed
			);

			if ( decision.plan == CellPastePlan::Incompatible )
			{
				refuse_table_command
				(
					tr ( "%1 (%2)" ).arg ( decision.message, cell_display_text ( row, column ) )
				);

				return true;
			}

			needsJaggedConfirm = needsJaggedConfirm || ( decision.plan == CellPastePlan::NeedsJaggedConfirm );
			needsTypeConfirm   = needsTypeConfirm   || ( decision.plan == CellPastePlan::NeedsTypeConfirm );

			assignments.push_back ( { row, column, std::move ( decision.value ) } );
		}

		// ONE QUESTION FOR THE PASTE, never one per cell -- which is the rule that used to make a type mismatch
		// refuse the whole gesture, applied now that the mismatch is answerable. Overwriting a column of numbers
		// with a column of strings asks once, and a No abandons the paste ENTIRELY rather than skipping the
		// offending cells: the plan is built before any of it is applied precisely so a half-done column is
		// unreachable, and a paste that landed on some cells and not others is that rule's own worst case.
		//
		// The type question comes FIRST because it is the more destructive of the two -- it replaces values, where
		// the jagged one only widens what shapes the array is allowed to hold. Both can arise in one paste, from
		// disjoint cells (a type mismatch needs two scalars, a jagged one a container source), and two modals in a
		// row is the honest cost of their being genuinely different questions.

		if ( needsTypeConfirm )
		{
			const bool confirmed = confirm_type_change
			(
				tr ( "Some of these values cannot be converted to their cells' types. Pasting them will replace "
				     "those cells and change their types. Continue?" )
			);

			if ( !confirmed )
			{
				return true;
			}
		}

		if ( needsJaggedConfirm && !confirm_jagged_paste () )
		{
			return true;
		}

		// One group for the whole gesture, opened here rather than inside apply_cell_values, because the growth and
		// the assignments are two calls and EDITOR-18's "one undo step" is about the PASTE rather than about either.
		// Lazily opened, so a paste that turns out to change nothing leaves no step behind (lesson Q43).

		UndoController::MacroScope macro ( *tableModel->undo_controller (), QStringLiteral ( "Paste Over" ) );

		const int applied = tableModel->apply_cell_values ( assignments, QStringLiteral ( "Paste Over" ) );

		const int grown = apply_pasted_growth ( wantsRow, growth );

		QString message = tr ( "Pasted into %1 (%n cell(s))", nullptr, applied ).arg ( selection_display_text () );

		if ( grown > 0 )
		{
			message += wantsRow ? tr ( " -- %n member(s) added", nullptr, grown )
			                    : tr ( " -- %n element(s) added", nullptr, grown );
		}

		report_table_clipboard ( message );

		// The row pasted over, brought into view where the user had scrolled away from it -- insert_table_row's reason.
		// Queued, and here it has to be: pasted onto the placeholder row, the element is appended inside the macro this
		// function still holds open, and the table hears about it only when that macro closes on the way out.

		if ( wantsRow && ( ( applied > 0 ) || ( grown > 0 ) ) )
		{
			reveal_row_later ( targetRow );
		}

		return true;
	}

	//=================================================================================================================
	// Header selection and sorting (EDITOR-16 / EDITOR-17 / EDIT-15)
	//=================================================================================================================

	const HeaderSelection& FormGridController::header_selection () const
	{
		return headerSelection;
	}

	void FormGridController::set_header_selection ( HeaderSelectionKind kind, int index )
	{
		if ( !is_array_table () || ( index < 0 ) )
		{
			return;
		}

		// EDITOR-12's provisional row IS selectable as a row, which it was not until EDITOR-18 was revised to let a
		// paste grow its target. It is the only way a row reaches an EMPTY array -- there is no element to select --
		// and EDITOR-12 already defines a paste into that row as the thing that materializes it. Copy, cut and delete
		// still refuse on it, each in its own words, because it has no values and no element to remove.

		const bool changed = ( headerSelection.kind != kind ) || ( headerSelection.index != index );

		headerSelection.kind  = kind;
		headerSelection.index = index;

		// EDITOR-16's "only one of the two selections is live" is a claim about what the user SEES as well as about
		// what a command acts on, so the grid's own selection is cleared here. Without it the cell that was selected
		// before the header click stays filled with the highlight beside the whole row or column that replaced it, and
		// the user is looking at two selections while the application believes in one.
		//
		// clearSelection, NOT setCurrentIndex ( QModelIndex () ): the current cell must SURVIVE, because it is where
		// the keyboard is and where an arrow key -- one of the four things that END a header selection -- has to
		// resume from. What goes is the fill; what stays is the focus rectangle, which is the honest rendering of
		// "current but not selected".
		//
		// It cannot recurse: clearSelection emits selectionChanged, and what ends a header selection is currentChanged.

		view->selectionModel ()->clearSelection ();

		refresh_header_selection_paint ();

		// Only for a change: a right-click on the row already selected re-selects it, and the enablement has nothing
		// new to hear.

		if ( changed )
		{
			emit header_selection_changed ();
		}
	}

	void FormGridController::clear_header_selection ()
	{
		if ( !headerSelection.is_active () )
		{
			return;
		}

		headerSelection = HeaderSelection {};

		refresh_header_selection_paint ();

		emit header_selection_changed ();
	}

	void FormGridController::select_current_row ()
	{
		const QModelIndex current = view->currentIndex ();

		if ( current.isValid () )
		{
			set_header_selection ( HeaderSelectionKind::Row, current.row () );
		}
	}

	void FormGridController::select_current_column ()
	{
		const QModelIndex current = view->currentIndex ();

		if ( current.isValid () )
		{
			set_header_selection ( HeaderSelectionKind::Column, current.column () );
		}
	}

	void FormGridController::populate_column_menu ( QMenu& menu )
	{
		// THREE GROUPS, and the separators are what say so. First, EDITOR-18's clipboard acting on the whole column --
		// the four commands Ctrl+X / C / V and Ctrl+Shift+V run while a column is selected, offered where the gesture
		// that selected it happened (add_table_clipboard_items, shared with the row menu). Last, the two commands that
		// are about the COLUMN ITSELF rather than about its values. Rename Column sits between.
		//
		// Delete Column reads first of the pair, which supersedes the order this menu shipped with on 2026-08-20
		// (Clear Contents first, separated from Delete) on the user's direction: the two belong together as one group
		// about the column, and separating them to keep the destructive one away from a habitual click cost more in
		// grouping than it bought in safety -- there is a confirmation-free undo behind both.
		//
		// EVERY ITEM IS ALWAYS ENABLED, which is Phase 12.5's rule rather than Phase 9's, and the reason is sharper
		// here than it was for the exports: a context menu appears under the pointer and vanishes on the next click,
		// so a disabled row in it has nowhere to explain itself and no time in which to do it. Each command's own
		// refusal therefore runs and reports in words (VAL-05) -- Delete Column on a single-value table, and Cut and
		// Copy on the provisional row, each of which already says its own piece.

		add_table_clipboard_items ( menu );

		menu.addSeparator ();

		// EDITOR-21 sits between two separators, in a group of its own: it is neither a clipboard command nor one of
		// the two that act on the column's existence, and grouping it with either would say it was.

		QAction* const renameAction = menu.addAction ( tr ( "Rename Column" ) );

		menu.addSeparator ();

		QAction* const deleteAction = menu.addAction ( tr ( "Delete Column" ) );
		QAction* const clearAction  = menu.addAction ( tr ( "Clear Contents" ) );

		deleteAction->setShortcut ( QKeySequence::Delete );

		connect ( renameAction, &QAction::triggered, this, [ this ] () { rename_table_column    (); } );
		connect ( deleteAction, &QAction::triggered, this, [ this ] () { delete_table_selection (); } );
		connect ( clearAction,  &QAction::triggered, this, [ this ] () { clear_table_column     (); } );
	}

	void FormGridController::add_table_clipboard_items ( QMenu& menu )
	{
		// The clipboard group both header menus open with -- Cut, Copy, Paste, Paste Over -- stated once, so the two
		// menus cannot come to offer different commands, or the same ones in a different order.
		//
		// Each shows its key -- the key that runs the same command with the menu CLOSED. Paste Over's is the one nobody
		// would guess, and Delete's (on the item below) only started working on 2026-09-23; a key the menu does not
		// show is a key the user has no way to learn. The keys are for reading, and they cannot collide with the
		// window's own Cut / Copy / Paste: a shortcut on one of these items can match only while its menu is the
		// active popup, and a window-scoped Ctrl+C was MEASURED not to fire with a context menu open (native
		// platform, 2026-09-23). The items die with the menu.

		QAction* const cutAction       = menu.addAction ( tr ( "Cut" ) );
		QAction* const copyAction      = menu.addAction ( tr ( "Copy" ) );
		QAction* const pasteAction     = menu.addAction ( tr ( "Paste" ) );
		QAction* const pasteOverAction = menu.addAction ( tr ( "Paste Over" ) );

		cutAction      ->setShortcut ( QKeySequence::Cut );
		copyAction     ->setShortcut ( QKeySequence::Copy );
		pasteAction    ->setShortcut ( QKeySequence::Paste );
		pasteOverAction->setShortcut ( QKeySequence ( Qt::CTRL | Qt::SHIFT | Qt::Key_V ) );

		connect ( cutAction,       &QAction::triggered, this, [ this ] () { cut_table_selection        (); } );
		connect ( copyAction,      &QAction::triggered, this, [ this ] () { copy_table_selection       (); } );
		connect ( pasteAction,     &QAction::triggered, this, [ this ] () { paste_table_selection      (); } );
		connect ( pasteOverAction, &QAction::triggered, this, [ this ] () { paste_over_table_selection (); } );
	}

	void FormGridController::show_column_menu ( int section, const QPoint& globalPosition )
	{
		if ( !is_array_table () )
		{
			return;
		}

		// SELECT FIRST. The menu's commands read the header selection, so the gesture that opens the menu is also the
		// gesture that decides what the menu is about -- which is what a spreadsheet does, and what keeps the column
		// highlighted while the user reads the menu covering part of it.

		set_header_selection ( HeaderSelectionKind::Column, section );

		QMenu menu ( view );

		populate_column_menu ( menu );

		menu.exec ( globalPosition );
	}

	void FormGridController::populate_row_menu ( QMenu& menu )
	{
		// EDITOR-22, and it is the column menu's shape with one group fewer. Above the separator, EDITOR-18's clipboard
		// acting on the whole row; below it, the two commands about the row ITSELF -- Delete Row first, then Clear
		// Contents, the order the column menu settled on. There is no rename: a row is an element, whose only name is
		// its position.
		//
		// EVERY ITEM IS ALWAYS ENABLED, for populate_column_menu's reason, and each command's own refusal reports in
		// words -- all but the two pastes say so on the provisional row, which has no element to act on, while either
		// paste materializes it as EDITOR-12 defines.
		//
		// Delete Row is delete_table_selection, the command Delete runs on a selected row. Removing an array element
		// moves every element after it up one position, so the gap closes by construction rather than by a step of
		// its own.

		add_table_clipboard_items ( menu );

		menu.addSeparator ();

		QAction* const deleteAction = menu.addAction ( tr ( "Delete Row" ) );
		QAction* const clearAction  = menu.addAction ( tr ( "Clear Contents" ) );

		deleteAction->setShortcut ( QKeySequence::Delete );

		connect ( deleteAction, &QAction::triggered, this, [ this ] () { delete_table_selection (); } );
		connect ( clearAction,  &QAction::triggered, this, [ this ] () { clear_table_row        (); } );
	}

	void FormGridController::show_row_menu ( int row, const QPoint& globalPosition )
	{
		if ( !is_array_table () )
		{
			return;
		}

		// SELECT FIRST, for show_column_menu's reason, and through the same call a left click on the row index makes
		// -- so the right-click selects the row in exactly the way the left click does. The keyboard follows by the
		// same route in both cases: the press that reached the header has already focused the table, the header
		// taking no focus of its own and Qt passing it to the nearest ancestor that does.

		set_header_selection ( HeaderSelectionKind::Row, row );

		QMenu menu ( view );

		populate_row_menu ( menu );

		menu.exec ( globalPosition );
	}

	void FormGridController::refresh_header_selection_paint ()
	{
		// The cells first: they carry the ordinary selection highlight and are the definitive signal, the header
		// sections only saying which header the selection came from.

		view->viewport ()->update ();

		if ( columnHeader != nullptr ) columnHeader->refresh ();
		if ( rowHeader    != nullptr ) rowHeader   ->refresh ();
	}

	void FormGridController::toggle_sort ( int column )
	{
		if ( !is_array_table () )
		{
			return;
		}

		// The first toggle on a column sorts ascending and the next descending. A DIFFERENT column starts again at
		// ascending: the direction describes an ordering of the column it was chosen for, and carrying it across
		// would state a choice about the new column that the user did not make.

		const Qt::SortOrder order = ( ( column == sortedColumn ) && ( sortedOrder == Qt::AscendingOrder ) )
		                          ? Qt::DescendingOrder
		                          : Qt::AscendingOrder;

		const EditOutcome outcome = tableModel->sort_by_column ( column, order );

		if ( outcome == EditOutcome::Rejected )
		{
			return;
		}

		// The marker is set AFTER the edit, not before: the edit reaches the model, which reports the array changed,
		// which clears the marker. Setting it first would leave the sort clearing its own mark.
		//
		// It is set on Unchanged as well as on Applied, because the claim the marker makes -- "the array is in this
		// order" -- is true either way; an already-sorted array is sorted.

		sortedColumn = column;
		sortedOrder  = order;

		if ( columnHeader != nullptr )
		{
			columnHeader->set_sort_marker ( column, order );
		}

		emit edit_reported ( EditCommand::SortArray, outcome, tableModel->presented_pointer () );
	}

	void FormGridController::note_replayed_row ( int row )
	{
		// Several rows in one step (a grouped delete undone, say): the FIRST, the one nearest the top and so the one
		// least likely to be on screen already.

		rowToReveal = ( rowToReveal < 0 ) ? row : std::min ( rowToReveal, row );
	}

	void FormGridController::reveal_row_later ( int row )
	{
		// On the next turn of the event loop, so the caller's change has been ANNOUNCED before anything scrolls to it.
		// That is not a precaution: a Paste Over onto the placeholder row appends its element inside a macro that is
		// still open when the paste returns, so a reveal run there would find no such row yet and do nothing. The
		// case that pins it is pasting_over_the_placeholder_row_out_of_view_scrolls_to_the_new_row -- the only one
		// that fails against an immediate call, which every other reveal case survives.

		QMetaObject::invokeMethod ( this, [ this, row ] () { reveal_row ( row ); }, Qt::QueuedConnection );
	}

	void FormGridController::reveal_row ( int row )
	{
		// The queued call can arrive after anything: the row may have gone again, or the pane may present another
		// array by now. Both are simply "nothing to reveal".

		if ( !is_array_table () || ( row < 0 ) || ( row >= tableModel->element_count () ) || ( tableModel->columnCount () == 0 ) )
		{
			return;
		}

		// VERTICALLY only. scrollTo brings a cell into view on both axes, and the user asked to see a ROW -- so the
		// horizontal position they were reading at is put back rather than jumping to the table's first column.
		// EnsureVisible is the minimal move: none at all for a row already on screen, and otherwise just far enough.

		QScrollBar* const horizontal = view->horizontalScrollBar ();
		const int         across     = horizontal->value ();

		view->scrollTo ( tableModel->index ( row, 0 ), QAbstractItemView::EnsureVisible );

		horizontal->setValue ( across );
	}

	void FormGridController::handle_row_index_activated ( int row )
	{
		if ( !is_array_table () )
		{
			return;
		}

		const JsonPointer elementPointer = tableModel->element_pointer ( row );

		// A root pointer means the row is not an element -- the provisional row, or out of range -- and EDITOR-05 has
		// nothing to drill into.

		if ( elementPointer.is_root () )
		{
			return;
		}

		request_drill_in ( elementPointer );
	}

	void FormGridController::reset_header_state ()
	{
		clear_header_selection ();

		sortedColumn = -1;
		sortedOrder  = Qt::AscendingOrder;

		if ( columnHeader != nullptr )
		{
			columnHeader->clear_sort_marker ();
		}
	}

	bool FormGridController::cell_clipboard_active () const
	{
		// BOTH faces since Phase 15 (EDITOR-14, closing OQ-2); the array table's alone before that. The gesture belongs
		// to the grid whenever a grid holds the keyboard and a cell is current -- which face it is decides only WHAT is
		// copied, not whether the gesture is the grid's.
		//
		// A live header selection answers yes on its own (EDITOR-18), without a current cell: a header click selects a
		// row or a column while the current cell is wherever it was, and on a freshly presented table there may not be
		// one at all.

		if ( table_selection_active () )
		{
			return true;
		}

		return ( is_array_table () || is_object_form () ) && ( clipboard != nullptr )
		    && view->currentIndex ().isValid ();
	}

	bool FormGridController::copy_cell ()
	{
		// EDITOR-18's row / column route is taken FIRST, which is the whole of "a header selection takes precedence
		// over the current cell while it is live". It answers false when no header selection is live, so the cell
		// route below is reached by everything else, unchanged.

		if ( copy_table_selection () )
		{
			return true;
		}

		if ( !cell_clipboard_active () )
		{
			return false;
		}

		if ( is_object_form () )
		{
			return copy_field ();
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
		if ( cut_table_selection () )
		{
			return true;
		}

		if ( !cell_clipboard_active () )
		{
			return false;
		}

		// EDITOR-14: in the object form, cut is copy plus REMOVING THE MEMBER -- deliberately not the table's
		// copy-plus-null. An object member's absence is expressible where an array element's is not, so the two faces
		// diverge here on purpose; what does NOT diverge is that cut places exactly what copy would on the clipboard,
		// so the two gestures never disagree about what was taken.

		if ( is_object_form () )
		{
			if ( !copy_field () )
			{
				return false;
			}

			return remove_current_member ();
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

	//=================================================================================================================
	// The object form's field clipboard (EDITOR-14)
	//
	// COLUMN-SENSITIVE, which the array table's is not: the form has a key column the table has no equivalent of, and
	// a gesture on a key means something different from the same gesture on a value.
	//=================================================================================================================

	bool FormGridController::copy_field ()
	{
		const QModelIndex current = view->currentIndex ();

		// A KEY is a NAME, so it goes through set_plain_text rather than copy_cell -- see the naming rule at the head
		// of ClipboardService.hpp, which is where that reason is stated for every command that copies a name.
		//
		// EDITOR-14's one deliberate divergence from the array table lives here too: cut is copy plus REMOVING the
		// member, where a table cell's cut is copy plus null, because an object member's absence is expressible where
		// an array element's is not.

		if ( current_cell_is_key_column () )
		{
			const QString key = formModel->key_for_row ( current.row () );

			// The lone scalar root has no key (EDITOR-02). Nothing to copy, and a handled no-op rather than a decline,
			// for copy_cell's own reason: falling through would copy the whole selected NODE over the clipboard.

			if ( key.isEmpty () )
			{
				report_field_clipboard ( tr ( "Nothing to copy" ) );

				return true;
			}

			clipboard->set_plain_text ( key );

			report_field_clipboard ( tr ( "Copied key" ) );

			return true;
		}

		JsonNode* const node = formModel->grid_node ( current.row (), JsonFormModel::VALUE_COLUMN );

		if ( node == nullptr )
		{
			report_field_clipboard ( tr ( "Nothing to copy" ) );

			return true;
		}

		clipboard->copy_cell ( *node );

		report_field_clipboard ( tr ( "Copied field" ) );

		return true;
	}

	bool FormGridController::remove_current_member ()
	{
		const QModelIndex current = view->currentIndex ();

		// Removal is the UndoController's ordinary node delete, so cut is one undo step and undoes as one -- and the
		// root guard is enforced where it already lives rather than restated here.

		if ( !formModel->remove_row ( current.row () ) )
		{
			report_field_clipboard ( tr ( "This field cannot be cut" ) );

			return true;
		}

		report_field_clipboard ( current_cell_is_key_column () ? tr ( "Cut key" ) : tr ( "Cut field" ) );

		return true;
	}

	bool FormGridController::paste_field ()
	{
		const QModelIndex current = view->currentIndex ();

		// A paste onto a KEY renames the member, which is why it is gated by SET-05a exactly as the key column's own
		// editor is: with key editing off the key is a label again, and a label cannot be pasted over. The duplicate
		// refusal is UndoController's (VAL-02), reported in the words Rename Key already uses.

		if ( current_cell_is_key_column () )
		{
			if ( !formModel->is_key_editing_allowed () )
			{
				report_field_clipboard ( tr ( "Key editing is switched off" ) );

				return true;
			}

			const QString pasted = clipboard->plain_text ();

			if ( pasted.isEmpty () )
			{
				report_field_clipboard ( tr ( "Nothing to paste" ) );

				return true;
			}

			if ( !formModel->rename_row ( current.row (), pasted ) )
			{
				report_field_clipboard ( tr ( "That key already exists in this object." ) );

				return true;
			}

			report_field_clipboard ( tr ( "Renamed field" ) );

			return true;
		}

		std::unique_ptr<JsonNode> value = clipboard->value ();

		if ( value == nullptr )
		{
			report_field_clipboard ( tr ( "Nothing to paste" ) );

			return true;
		}

		// EDITOR-14 defers to EDITOR-11's conversion matrix, through the SAME decision the table uses -- so the two
		// faces cannot come to disagree about which pastes are legal, and the refusal is worded once.
		//
		// The column list is deliberately EMPTY. A table column's shape check compares the incoming container with the
		// column's other values, and an object's members have no such relationship to one another -- `email` tells you
		// nothing about what `roles` may hold. With nothing to compare against the check is vacuous, which is the
		// correct answer rather than a skipped one, and the jagged flow (SET-05) never arises here at all.

		JsonNode* const target = formModel->grid_node ( current.row (), JsonFormModel::VALUE_COLUMN );

		CellTarget targetKind = CellTarget::Untyped;

		if ( cell_content ( target ) == CellContent::Scalar )
		{
			switch ( target->kind () )
			{
				case JsonKind::String:  targetKind = CellTarget::String;  break;
				case JsonKind::Number:  targetKind = CellTarget::Number;  break;
				case JsonKind::Boolean: targetKind = CellTarget::Boolean; break;
				default:                targetKind = CellTarget::Untyped; break;
			}
		}
		else if ( cell_content ( target ) == CellContent::Container )
		{
			targetKind = ( target->kind () == JsonKind::Object ) ? CellTarget::Object : CellTarget::Array;
		}

		CellPasteDecision decision = plan_cell_paste ( *value, targetKind, {}, false );

		if ( decision.plan == CellPastePlan::NeedsTypeConfirm )
		{
			// EDITOR-11's type override, reaching the form face through the same decision the table uses -- which is
			// the whole point of EDITOR-14 deferring to it. A field is the one surface where the override is most
			// obviously wanted, since a form is where a user would otherwise retype by hand what they had copied.

			if ( !confirm_type_change ( decision.message ) )
			{
				return true;
			}
		}
		else if ( decision.plan != CellPastePlan::Apply )
		{
			// Still refused, and still in a modal: a paste never changes a STRUCTURE silently. What is left here is
			// the container cases, where the source or the target is a container -- see cell_paste_plan.hpp for why
			// those are not the same question as a type mismatch between two scalars.

			show_message_box ( view, MessageKind::Warning, tr ( "Paste" ), decision.message );

			return true;
		}

		if ( !formModel->replace_row_value ( current.row (), std::move ( decision.value ) ) )
		{
			report_field_clipboard ( tr ( "This field cannot be pasted into" ) );

			return true;
		}

		report_field_clipboard ( tr ( "Pasted field" ) );

		return true;
	}

	void FormGridController::report_field_clipboard ( const QString& message )
	{
		if ( status != nullptr )
		{
			status->show_message ( message, config::form::REFUSAL_MESSAGE_TIMEOUT );
		}
	}

	bool FormGridController::confirm_type_change ( const QString& message )
	{
		// NO is the default (EDITOR-11), for the reason the jagged confirmation's is: the destructive answer must be
		// the one the user chooses rather than the one Enter chooses for them. What separates this from the refusal
		// it replaces is only that Yes exists at all.

		const QMessageBox::StandardButton answer = ask_message_box
		(
			view,
			MessageKind::Warning,
			tr ( "Paste" ),
			message,
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No,
			QMessageBox::No
		);

		return answer == QMessageBox::Yes;
	}

	bool FormGridController::paste_external_grid ()
	{
		// EDITOR-24. What a spreadsheet does with the same text, and the reported defect it replaces: three lines from
		// Notepad, or a column of cells from Excel, arrived as ONE cell holding the lot, and the default escaped string
		// display showed it as Four\nFive\nSix\n.
		//
		// ONLY EXTERNAL TEXT. VJE's own copies write plain text too, but it is a rendering -- a string value's line
		// breaks, a copied column's values one per line -- and each of those already has its own paste rule; reading
		// it back as a block would make our own copy paste as something it was not (ClipboardService::external_text).

		const QString text = clipboard->external_text ();

		if ( text.isEmpty () )
		{
			return false;
		}

		const std::optional<std::vector<QStringList>> grid = clipboard_grid::parse ( text );

		if ( !grid.has_value () )
		{
			return false;   // One value: the ordinary cell paste (EDITOR-11) applies unchanged.
		}

		const QModelIndex current = view->currentIndex ();

		if ( !current.isValid () )
		{
			return false;
		}

		const std::vector<QStringList>& rows = grid.value ();

		const int anchorRow    = current.row ();
		const int anchorColumn = current.column ();
		const int blockWidth   = clipboard_grid::width ( rows );
		const int columnCount  = tableModel->columnCount ();

		// A BLOCK WIDER THAN THE COLUMNS LEFT IS REFUSED WHOLE. A spreadsheet has endless columns; this table has the
		// members its elements carry, and a value past the last has no name to be written under. Dropping it would be
		// a paste that quietly did less than it looked like -- the rule EDITOR-18 already applies to a paste that
		// cannot land in full.

		if ( anchorColumn + blockWidth > columnCount )
		{
			refuse_table_command
			(
				tr ( "The pasted block is %n column(s) wide, ", nullptr, blockWidth )
				+ tr ( "and only %n column(s) remain from the selected cell.", nullptr, columnCount - anchorColumn )
			);

			return true;
		}

		const bool jaggedAllowed = ( settings != nullptr )
		                        && settings->value_bool ( settings_keys::FORM_ALLOW_JAGGED_PASTE, false );

		// PLANNED BEFORE ANY OF IT IS APPLIED, Paste Over's rule: one incompatible cell refuses the whole paste, and a
		// type mismatch or a jagged value asks ONCE for the paste. The rows that exist are assignments; the rows past
		// the end are growth, appended as new elements in order.
		//
		// Each field is read as external text always is (EDITOR-11): a JSON value where it is one -- so 42 is a number
		// and true a boolean -- and a string otherwise. An EMPTY field leaves its cell as it was, which is VJE's own
		// rule for an absent cell and what makes a range copied out of VJE come back in exactly; in a new element it
		// is null, as every cell a paste's growth does not supply is.

		std::vector<JsonTableModel::CellAssignment>            assignments;
		std::vector<std::vector<JsonTableModel::PastedMember>> growth;
		bool                                                   needsJaggedConfirm = false;
		bool                                                   needsTypeConfirm   = false;

		const int existingRows = tableModel->element_count ();

		for ( std::size_t rowOffset = 0; rowOffset < rows.size (); ++rowOffset )
		{
			const QStringList& fields = rows [ rowOffset ];
			const int          row    = anchorRow + static_cast<int> ( rowOffset );

			if ( row >= existingRows )
			{
				std::vector<JsonTableModel::PastedMember> members;

				for ( int fieldIndex = 0; fieldIndex < fields.size (); ++fieldIndex )
				{
					if ( fields.at ( fieldIndex ).isEmpty () )
					{
						continue;
					}

					const std::optional<QString> key = tableModel->column_key ( anchorColumn + fieldIndex );

					members.push_back
					(
						{ key.value_or ( QString () ), CellPasteConverter::resolve_clipboard_text ( fields.at ( fieldIndex ) ) }
					);
				}

				// A line with nothing in it still holds its ROW, or every line after it would land one row too high.

				if ( members.empty () )
				{
					members.push_back ( { tableModel->column_key ( anchorColumn ).value_or ( QString () ), JsonNode::make_null () } );
				}

				growth.push_back ( std::move ( members ) );

				continue;
			}

			for ( int fieldIndex = 0; fieldIndex < fields.size (); ++fieldIndex )
			{
				if ( fields.at ( fieldIndex ).isEmpty () )
				{
					continue;
				}

				const int column = anchorColumn + fieldIndex;

				const std::unique_ptr<JsonNode> value = CellPasteConverter::resolve_clipboard_text ( fields.at ( fieldIndex ) );

				CellPasteDecision decision = plan_cell_paste
				(
					*value,
					target_for_cell ( row, column ),
					column_values_excluding ( column, row ),
					jaggedAllowed
				);

				if ( decision.plan == CellPastePlan::Incompatible )
				{
					refuse_table_command ( tr ( "%1 (%2)" ).arg ( decision.message, cell_display_text ( row, column ) ) );

					return true;
				}

				needsJaggedConfirm = needsJaggedConfirm || ( decision.plan == CellPastePlan::NeedsJaggedConfirm );
				needsTypeConfirm   = needsTypeConfirm   || ( decision.plan == CellPastePlan::NeedsTypeConfirm );

				assignments.push_back ( { row, column, std::move ( decision.value ) } );
			}
		}

		if ( needsTypeConfirm )
		{
			const bool confirmed = confirm_type_change
			(
				tr ( "Some of these values cannot be converted to their cells' types. Pasting them will replace "
				     "those cells and change their types. Continue?" )
			);

			if ( !confirmed )
			{
				return true;
			}
		}

		if ( needsJaggedConfirm && !confirm_jagged_paste () )
		{
			return true;
		}

		int applied = 0;
		int grown   = 0;

		{
			// One undo step for the paste, the assignments and the growth together; lazily opened, so a block of
			// nothing but empty fields over existing rows leaves no step behind (lesson Q43).

			UndoController::MacroScope macro ( *tableModel->undo_controller (), QStringLiteral ( "Paste" ) );

			applied = tableModel->apply_cell_values ( assignments, QStringLiteral ( "Paste" ) );

			for ( std::vector<JsonTableModel::PastedMember>& members : growth )
			{
				if ( tableModel->append_pasted_element ( members ) == EditOutcome::Applied )
				{
					++grown;
				}
			}
		}

		// The block starts where the user is, so there is nothing to reveal -- except that a paste onto the placeholder
		// row has turned it into a real element, and the current cell goes to that element as a single-cell paste
		// onto it does. After the macro has closed, since the table hears of the new rows only then.

		if ( ( anchorRow >= existingRows ) && ( grown > 0 ) )
		{
			applyingSelection = true;

			view->setCurrentIndex ( view->model ()->index ( anchorRow, anchorColumn ) );

			applyingSelection = false;
		}

		QString message = tr ( "Pasted %n cell(s)", nullptr, applied );

		if ( grown > 0 )
		{
			message += tr ( " -- %n element(s) added", nullptr, grown );
		}

		report_table_clipboard ( message );

		return true;
	}

	bool FormGridController::paste_cell ()
	{
		if ( paste_table_selection () )
		{
			return true;
		}

		if ( !cell_clipboard_active () || ( clipboard == nullptr ) )
		{
			return false;
		}

		// A TABLE SELECTION IS NOT A CELL VALUE, and left to fall through it does not merely fail -- it succeeds
		// wrongly. ClipboardService::value falls back to the clipboard's plain TEXT when it finds no single-value
		// format, and a column's plain text is its values one per line, so pasting a copied column onto a cell
		// produced one string with newlines in it. That is our own format being read as though it came from another
		// application (reported after the 15h build).
		//
		// Refused rather than reinterpreted, and the message names the gesture that WOULD work, because the user has
		// copied something perfectly pasteable and has only aimed it at the wrong target.

		if ( !clipboard->table_selection ().is_empty () )
		{
			refuse_table_command
			(
				tr ( "The clipboard holds a table row or column. Select a row or a column from its header to paste it." )
			);

			return true;
		}

		if ( is_object_form () )
		{
			return paste_field ();
		}

		if ( paste_external_grid () )
		{
			return true;
		}

		std::unique_ptr<JsonNode> value = clipboard->value ();

		if ( value == nullptr )
		{
			return false;   // Nothing on the clipboard to paste.
		}

		const QModelIndex current = view->currentIndex ();
		const int         row     = current.row ();
		const int         column  = current.column ();

		const CellTarget                   target       = target_for_cell ( row, column );
		const std::vector<const JsonNode*> columnValues = column_values_excluding ( column, row );

		const bool jaggedAllowed = ( settings != nullptr )
		                        && settings->value_bool ( settings_keys::FORM_ALLOW_JAGGED_PASTE, false );

		CellPasteDecision decision = plan_cell_paste ( *value, target, columnValues, jaggedAllowed );

		switch ( decision.plan )
		{
			case CellPastePlan::Incompatible:
			{
				// EDITOR-11's refusal, now reached only by a container on one side or the other: a paste never
				// changes a structure silently, and there is no override to offer (cell_paste_plan.hpp).

				show_message_box ( view, MessageKind::Warning, tr ( "Paste" ), decision.message );

				return true;
			}

			case CellPastePlan::NeedsTypeConfirm:
			{
				// The matrix had no conversion for the pairing, so the paste is OFFERED rather than refused
				// (EDITOR-11, revised 2026-08-20). decision.value already holds the source as it stands.

				if ( !confirm_type_change ( decision.message ) )
				{
					return true;
				}

				break;
			}

			case CellPastePlan::NeedsJaggedConfirm:
			{
				// SET-05 is on: warn that continuing makes the array jagged, and paste only on confirm (EDITOR-11).

				const QMessageBox::StandardButton answer = ask_message_box
				(
					view,
					MessageKind::Warning,
					tr ( "Paste" ),
					tr ( "This value does not match the array's structure. Pasting it will make the array structurally "
					     "heterogeneous (\"jagged\"). Continue?" ),
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::No,
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
