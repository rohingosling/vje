//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FormGridController -- the current-cell, activation, and drill-in behaviour layered on top of a QTableView for BOTH
//   Form View grids: the object form (EDITOR-02) and the array table (EDITOR-03). This is the "FormTableController" the
//   development plan names, generalized by one step.
//
//   WHY ONE CONTROLLER AND NOT TWO. EDITOR-02 states the requirement as "form / table parity": a field and a cell must
//   use the same presentation and the same interaction model. Two controllers would make that parity a promise kept by
//   hand, and the two specs are in fact ONE keyboard model with exactly one written divergence, carried here as policy
//   rather than as a separate code path:
//
//     - A form field writes its focus back to the selection service with SelectionOrigin::FormField; a table cell
//       deliberately does NOT (EDITOR-04) -- in-place cell editing must not drag the tree around.
//
//   There were TWO divergences until 2026-07-28. The other was "Left / Right inside an open editor navigate in the
//   table and stay caret keys in the form", and it is gone along with the flag that selected between them: while an
//   editor is open the horizontal arrows belong to the TEXT in both faces (spec EDITOR-02 / EDITOR-03, lesson D12).
//
//   Two further policy values express the object form's two-column shape without leaking it into the table: the form
//   LANDS on its value column when a node is presented (a starting point, not a restriction -- Left / Right reach the
//   key column, which is editable in its own right, EDIT-02), and its right-click menu is offered on the KEY column.
//
//   WHAT QTableView ALREADY DOES, AND IS LEFT TO DO. Arrow movement of the current cell, edge clamping, F2, and
//   type-to-replace are QTableView's own edit triggers and cursor movement -- correct as shipped, and matching
//   grid_navigation.hpp, which exists to state the same rules where a headless test can reach them. Tab is deliberately
//   TAKEN from the grid (setTabKeyNavigation(false)): it belongs to the workspace, moving the keyboard between panes
//   (NAV-04), and the arrow keys carry cell traversal alone. What this class adds is the part Qt has no notion of:
//   Enter as an ACTIVATION key rather than a navigation key, drill-in, activation asking the model whether the cell
//   edits before asking whether the node is a container, and the post-commit movement the delegate announces.
//
//   REENTRANCY. A drill-in changes what the pane presents, which would re-enter the table inside its own event handler.
//   It is therefore deferred onto the event loop (EDITOR-03 in as many words: "the drill-in re-present is deferred off the
//   gesture"). A current-cell move is not structural and stays synchronous.
//
//   WHAT IS NOT HERE. Cell cut / copy / paste (EDITOR-11) and provisional-row growth (EDITOR-12) are Phase 9. The
//   bottom-edge move that grows a row is already detectable -- grid_navigation reports the clamp -- but nothing acts
//   on it yet.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "controllers/edit_reporting.hpp"                // EditCommand / EditOutcome, carried by edit_reported.
#include "services/ClipboardService.hpp"                 // TableSelectionRef, the shape a selected row/column copies as.
#include "views/cell_paste_plan.hpp"                     // CellTarget, for the per-cell paste helpers.
#include "views/grid_navigation.hpp"
#include "views/GridHeaderView.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QObject>
#include <QPoint>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QKeyEvent;
class QModelIndex;
class QTableView;

class QMenu;

namespace vje
{
	class IDialogService;
	class ClipboardService;
	class IGridProjection;
	class JsonCellDelegate;
	class JsonNode;
	class JsonFormModel;
	class JsonTableModel;
	class SelectionService;
	class SettingsStore;
	class StatusService;

	//*****************************************************************************************************************
	// Class: FormGridController
	//*****************************************************************************************************************

	class FormGridController : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Types
		//=============================================================================================================

	public:

		// The written differences between EDITOR-02's form and EDITOR-03's table, and nothing else. Anything that is
		// not one of these is shared behaviour and belongs in the code below, not here.
		//
		// arrowKeysNavigateInEditor was a fourth entry and is gone (2026-07-28): the two grids now read Left / Right
		// the same way inside an open editor -- as caret keys -- so there is no longer a difference to carry.

		struct Policy
		{
			bool writesSelectionBack = false;   // Form: yes (EDITOR-04). Table: deliberately not.

			// Where the highlight LANDS when a node is first presented; -1 means the first column. It is a starting
			// point and nothing more -- both grids let the arrow keys reach every column from there, including the
			// object form's key column (EDITOR-02).

			int  landingColumn       = -1;            // Form: the value column, since that is what the user edits.

			int  contextMenuColumn   = -1;            // Form: the key column (EDITOR-02); -1 offers no NODE menu.

			// EDITOR-20: the column whose right-click offers the FIELD clipboard instead. The object form's value
			// column; -1 everywhere else, which is what keeps the array table's cells unchanged -- their right-click
			// has never offered anything and the column header's menu is where a table selection is acted on.

			int  fieldMenuColumn     = -1;
		};

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The view must already have its model set; the controller installs its own delegate on it. The selection
		// service may be null, which simply disables the write-back (the form the projection tests use).

		// The collaborators beyond the grid itself. Until Phase 15 this was TableSupport and the whole set was the ARRAY
		// TABLE's alone -- "the form thereby gets none of this behaviour, which is exactly OQ-2's deferral". OQ-2 is now
		// closed (EDITOR-13/14/15), so the split moved: the three SERVICES serve both faces, and what stays face-specific
		// is which concrete model is present.
		//
		// Exactly one of tableModel / formModel is non-null, and it is the same object as `projection`, typed concretely
		// so the face-specific paths can ask it what the interface does not expose -- the table's provisional rows,
		// missing cells and element count; the form's keys and members. is_array_table() / is_object_form() are the two
		// questions the rest of the class asks.

		struct GridSupport
		{
			JsonTableModel*   tableModel = nullptr;   // The array table's projection, or null for the object form.
			JsonFormModel*    formModel  = nullptr;   // The object form's projection, or null for the array table.
			ClipboardService* clipboard  = nullptr;
			SettingsStore*    settings   = nullptr;   // SET-05 "Allow jagged-array paste"; SET-05a "Allow key editing".
			StatusService*    status     = nullptr;   // The copy / cut / paste confirmations.

			// The array table's two interactive headers (EDITOR-16 / EDITOR-17 / EDIT-15), null for the object form,
			// whose headers are hidden. The controller connects them itself rather than the view doing it, because
			// the two gestures they raise -- select and sort -- share one piece of state with the cell behaviour
			// already here, and splitting the wiring would split that state's owner from half its writers.

			GridHeaderView*   columnHeader = nullptr;
			GridHeaderView*   rowHeader    = nullptr;

			// EDITOR-21's prompt. Null simply disables Rename Column, which is what every projection-only test wants
			// and what SET-01a's Restore Defaults button already does without a seam -- a command that silently
			// declined would be a dead menu row, so it is absent instead.

			IDialogService*   dialogs = nullptr;
		};

		// Both faces pass a GridSupport naming their own concrete model plus the three shared services. It is not
		// defaulted here because a nested-type default argument cannot use the struct's own member initializers before
		// the enclosing class is complete -- FormView always passes one.

		FormGridController
		(
			QTableView*         view,
			IGridProjection*    projection,
			SelectionService*   selection,
			const Policy&       policy,
			const GridSupport&  support,
			QObject*            parent = nullptr
		);

		// Declared (not implicit) so the unique_ptr<JsonNode> member is destroyed where JsonNode is complete.

		~FormGridController () override;

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		JsonCellDelegate* delegate () const;

		// The pointer of the current cell; a root pointer when there is no current cell.

		JsonPointer current_pointer () const;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// Move the highlight onto the cell a pointer names, WITHOUT writing the move back to the selection service --
		// this is how an inbound selection is applied, and echoing it would be a feedback loop.

		void set_current_pointer ( const JsonPointer& pointer );

		// Put the highlight on the first cell, used when a node is presented with no field of its own to focus.

		void select_first_cell ();

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public slots:

		// Hand the editing caret to the current cell (EDITOR-04's "Edit on" hand-over from the tree). A container cell
		// drills in instead, and a read-only cell does nothing.

		void activate_editing ();

		//=============================================================================================================
		// Cell clipboard (EDITOR-11) -- the ARRAY TABLE only; a no-op returning false for the object form.
		//
		// Each acts on the current cell (the caller guarantees no cell editor is open -- an open editor is a QLineEdit /
		// QComboBox that takes the keystroke first). Each returns true when it OWNED the gesture -- including a handled
		// no-op on a missing cell, and a shown error / warning message box -- and false only when the cell clipboard
		// does not apply at all (the object form, no current cell), so the caller can fall back to the node clipboard.
		//=============================================================================================================

	public:

		bool cut_cell   ();
		bool copy_cell  ();
		bool paste_cell ();

		// EDITOR-18's Delete, which the other three do not need a member of their own for: Cut / Copy / Paste already
		// funnel through the three above, where the row / column route is taken first. Delete has no cell-level
		// meaning at all, so it exists only when a header selection is live and answers false otherwise -- which is
		// what lets Document > Delete Node fall through to the tree exactly as it did before.

		bool delete_cell ();

		// Would delete_cell act right now -- is a header selection live on the array table? Drives Document > Delete
		// Node's enablement while the editor pane holds the keyboard. Without it that command is enabled by the TREE's
		// selection alone, which refuses the root -- so a ROOT array's selected row could not be deleted from the menu
		// or the toolbar, the one route that reaches delete_cell through the command.

		bool cell_delete_active () const;

		// Is a cell clipboard command applicable right now -- the array table with a current cell? Used for the
		// disabled-not-hidden enablement of Edit > Cut / Copy / Paste.

		bool cell_clipboard_active () const;

		//=============================================================================================================
		// Header selection and sorting (EDITOR-16 / EDITOR-17 / EDIT-15) -- the ARRAY TABLE only.
		//
		// The header selection and the current-cell highlight are two selections of which exactly one is live, which
		// is what makes the clipboard's routing answerable: with a header selection live the four commands act on the
		// row or the column, and with none live they act on the current cell exactly as they did before.
		//
		// "Exactly one" is a FIELD here rather than a predicate derived from a QItemSelection, and GridHeaderView.hpp
		// carries the three reasons why.
		//=============================================================================================================

	public:

		const HeaderSelection& header_selection () const;

		// Everything that ends a header selection funnels through here: a cell click, an arrow-key move (both of which
		// arrive as currentChanged), a re-present, and a document reset.

		void clear_header_selection ();

		// The keyboard route to the same two selections (EDITOR-16, NFR-05). Public so a test can reach them without
		// synthesizing a header click, and so FormView can offer them from a menu later without a second spelling.

		void select_current_row    ();
		void select_current_column ();

		// EDITOR-19's menu, split from showing it because QMenu::exec blocks and an offscreen test cannot drive a
		// popup -- TreeViewPane's context menu is split the same way, for the same reason. It reads the header
		// selection rather than taking a column, so the menu and its commands cannot come to disagree about which
		// column the gesture was about.

		void populate_column_menu ( QMenu& menu );

		// EDITOR-22's menu, the row index's counterpart of the column menu and split from showing it for the same
		// reason. Two groups -- the clipboard, then the two commands about the row itself -- and no rename, a row
		// having no name to change.

		void populate_row_menu ( QMenu& menu );

		// WHICH menu a right-click on a cell raises, as its own question.
		//
		// Split out for the reason populate_* is split from show_*, and more sharply: QMenu::exec spins a nested event
		// loop that an offscreen test cannot drive at all -- the popup opens and never closes -- so a routing claim
		// asserted by delivering the gesture would hang the suite rather than fail it. The gesture is the widget's; the
		// RULE is this.

		enum class CellMenu
		{
			None,       // No menu at all -- the array table's cells, whose commands live on the column header.
			Node,       // EDITOR-02's node menu, built by MainWindow from the shared NodeContextActions.
			Field       // EDITOR-20's field clipboard, built here.
		};

		CellMenu menu_for_cell ( const QModelIndex& cell ) const;

		// EDITOR-20's field menu, split from showing it for populate_column_menu's reason: QMenu::exec blocks, so the
		// contents and the order are only assertable if building them is a separate call.

		void populate_field_menu ( QMenu& menu );

		// EDITOR-19's other command: set every cell of the selected column to `null`, the column itself surviving.
		// Deliberately NOT among the four clipboard commands above -- nothing is taken and nothing is placed, and
		// their gate additionally requires a clipboard, which would leave this one silently dead without one.
		//
		// Public for the reason select_current_row is: the gesture that reaches it is a menu item inside a blocking
		// QMenu::exec, so the state is only assertable by calling the command the item calls.

		bool clear_table_column ();

		// EDITOR-22's Clear Contents: set every cell of the selected row to `null`, the element itself surviving. The
		// row-shaped counterpart of clear_table_column, public for its reason.

		bool clear_table_row ();

		// EDITOR-18's PASTE OVER (2026-09-23): the selected row or column is OVERWRITTEN cell by cell, growing where the
		// source is longer -- which is what Paste did until Paste became an insert. Reached from both header menus and
		// by Ctrl+Shift+V; public for clear_table_column's reason.

		bool paste_over_table_selection ();

		// EDITOR-21: prompt for a name and rename the column's member key on every element. Public for
		// clear_table_column's reason -- the gesture reaches it from inside a blocking QMenu::exec.

		bool rename_table_column ();

		// EDIT-15's gesture. The first toggle on a column sorts ascending, the next descending, and so on; toggling a
		// DIFFERENT column starts again at ascending, since the direction describes an ordering of that column and
		// carrying it across would state a choice the user did not make about the new one.

		void toggle_sort ( int column );

		//=============================================================================================================
		// Presentation lifecycle
		//=============================================================================================================

	public:

		// The pane is now presenting a different container. Both the header selection and the sort marker describe an
		// action taken on the array that was presented, so both end here.

		void reset_header_state ();

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// A {...} / [...] cell was activated (EDITOR-05). Emitted from the event loop rather than from the gesture, so
		// the consumer is free to re-present the pane.

		void drill_in_requested ( const JsonPointer& pointer );

		// The node context menu was asked for on a key label (EDITOR-02, TREE-06's action set).

		void context_menu_requested ( const JsonPointer& pointer, const QPoint& globalPosition );

		// EDIT-15 / EDITOR-18: a structural edit ran on the presented array -- a sort, a row delete, a column delete.
		// Carried OUT rather than reported here, because VAL-05's channel rule (status bar always, plus a modal on a
		// refusal) is stated once in controllers/edit_reporting, and a view that reached for a status bar of its own
		// would be the second place that rule lives.
		//
		// The clipboard's own three (copy, cut, paste) are deliberately NOT routed through it, for the reason
		// EditCommand omits Cut and Copy: their message is about what reached the clipboard rather than about an
		// EditOutcome, and "Copied column, 3 values not used" is not a sentence that rule can make.

		void edit_reported ( EditCommand command, EditOutcome outcome, const JsonPointer& target );

		// EDITOR-16: a header selection began, moved, or ended. Raised for the CHANGE rather than by the gesture that
		// caused it, so a route added later -- another menu, another key -- announces itself without being told to.
		//
		// Its consumer is MainWindow's Delete enablement (cell_delete_active). No other input the enablement already
		// listens to moves when a row is selected: the keyboard focus, the selection, the document, the undo stack and
		// the clipboard are all exactly where they were.

		void header_selection_changed ();

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_editing_moved   ( GridMove move );
		void handle_double_clicked  ( const QModelIndex& index );
		void handle_current_changed ( const QModelIndex& current, const QModelIndex& previous );
		void handle_context_menu    ( const QPoint& position );

		// A value was typed into a provisional cell (EDITOR-12). The materialize is deferred off this because an editor
		// is still open on the row about to be removed.

		void handle_provisional_commit ( int column, const QString& text );

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void activate ( const QModelIndex& index );

		// Does a gesture on this cell open an editor, as opposed to drilling in or doing nothing? Answered by the
		// model's flags(), so each projection states its own rule in one place.

		bool edits_in_place ( const QModelIndex& index ) const;   // Enter / double-click: drill in, or open the editor.

		void request_drill_in ( const JsonPointer& pointer );

		//=============================================================================================================
		// Provisional-row lifecycle (EDITOR-12) -- table only.
		//=============================================================================================================

	private:

		// Grow a provisional row and land the highlight on it, unless one already exists.

		void grow_provisional_row ( int column );

		// The deferred completion of a typed-entry commit into a provisional cell: materialize the element in place,
		// then either grow a fresh provisional (a downward advance) or land on the new real row.

		void finish_provisional_materialize ();

		// Abandon a still-empty provisional row when the highlight has left it (any move off it, EDITOR-12).

		void abandon_provisional_if_off_row ( const QModelIndex& current );

		// The array-table current cell's paste target kind, and the column's other values for the shape check.

		bool is_array_table () const;                             // tableModel != nullptr.
		bool is_object_form () const;                             // formModel  != nullptr.

		void handle_provisional_key ( const QString& key );      // EDITOR-15's deferred materialize.

		// EDITOR-14: the object form's clipboard is COLUMN-SENSITIVE, which the array table's is not -- a key cell and a
		// value cell copy and paste different things. These three answer for the form face alone.

		bool current_cell_is_key_column () const;

		bool copy_field  ();                                      // Key column -> the key as PLAIN TEXT only.
		bool paste_field ();                                      // Key column -> rename (SET-05a); value -> the matrix.

		// EDITOR-24: text from ANOTHER APPLICATION that holds several values -- a spreadsheet range, lines from a text
		// editor -- pasted onto an array-table cell fills a BLOCK from that cell, down and to the right, rather than
		// landing in the one cell. False when the clipboard holds no such text, and paste_cell carries on as before.

		bool paste_external_grid ();
		bool remove_current_member ();                            // Cut's second half: one undoable member removal.

		void report_field_clipboard ( const QString& message );

		// EDITOR-11's type override (revised 2026-08-20). The matrix had no conversion for the pairing, so the paste
		// is OFFERED rather than refused: `message` states the mismatch and its consequence, and a Yes applies the
		// source as it stands. All three paste routes ask through here so the button set, the default and the title
		// are stated once -- a confirmation defaulting to Yes in one place and No in another is how a user learns to
		// stop reading them.

		bool confirm_type_change ( const QString& message );

		//=============================================================================================================
		// The row and column clipboard (EDITOR-18) -- the ARRAY TABLE only, and only while a header selection is live.
		//
		// A row is an ELEMENT and a column is a MEMBER ACROSS ELEMENTS, which is the asymmetry the whole family turns
		// on: it decides what reaches the clipboard, what an external target receives, and what a delete removes.
		//=============================================================================================================

	private:

		bool table_selection_active () const;                    // A live header selection on the array table.

		bool copy_table_selection   ();
		bool cut_table_selection    ();
		bool paste_table_selection  ();                           // INSERTS in front of the selection (2026-09-23).
		bool delete_table_selection ();

		// The two halves of Paste, one per shape. Each consumes the source it is given.

		bool insert_table_row    ( TableSelectionValue& source );
		bool insert_table_column ( TableSelectionValue& source );

		// The refusals both pastes share -- nothing of a table's on the clipboard, or the wrong shape for the selection.
		// True when the paste is refused, having said so.

		bool refuses_table_paste_source ( const TableSelectionValue& source );

		// SET-05's jagged question, asked once for a paste. True to go ahead.

		bool confirm_jagged_paste ();

		// After a row or column is removed, select the one that took its place -- or the one before it where the last
		// was removed, or nothing where none remain.

		void select_replacement ( HeaderSelectionKind kind, int removedIndex );

		// The selection's cells in order, each carrying the member key of the column it came from and a null value
		// where the element LACKS that member. The provisional row is never among them: it is not an element, so it
		// has no cells to copy.

		std::vector<TableSelectionRef> selected_cells () const;

		// The ( row, column ) coordinates the same selection names, in the same order, for the paste.

		std::vector<std::pair<int, int>> selected_cell_positions () const;

		QString selected_cells_plain_text () const;

		// The paste matrix's target column for a cell (EDITOR-11), and the column's OTHER values for the shape check.
		// Both were inline in paste_cell until EDITOR-18 needed them per cell down a row or a column.

		CellTarget                   target_for_cell           ( int row, int column ) const;
		std::vector<const JsonNode*> column_values_excluding   ( int column, int row ) const;

		// What the selection is called in a message: "row 3", or "column \"status\"".

		QString selection_display_text () const;
		QString cell_display_text ( int row, int column ) const;

		void report_table_clipboard ( const QString& message );

		// EDITOR-18 / VAL-05: a refusal is the one outcome the user cannot see for themselves, so it reaches BOTH
		// channels -- the modal, and the status bar where DiagnosticLog taps.

		void refuse_table_command ( const QString& message );

		// EDITOR-18's GROWTH (revised 2026-08-18): a value the source carries and the target has no cell for. Held as
		// a key and a value rather than as a coordinate, because the coordinate is what does not exist yet.

		struct PastedGrowth
		{
			QString                   key;
			std::unique_ptr<JsonNode> value;
		};

		// Create the cells the growth names, and return how many were made. A COLUMN grows the array by appending
		// elements; a ROW grows the element by adding members, and every other element gains the same key as `null`
		// so the array stays uniform. Both run inside the caller's macro.

		int apply_pasted_growth ( bool pastingRow, std::vector<PastedGrowth>& growth );

		//=============================================================================================================
		// Header selection and sorting -- helpers.
		//=============================================================================================================

	private:

		void set_header_selection ( HeaderSelectionKind kind, int index );

		void refresh_header_selection_paint ();                   // Both headers plus the cells the selection covers.

		// The header's context-menu gesture: select the column, then offer the menu about it. Selecting FIRST is what
		// a spreadsheet does and is what makes the menu's commands need no column of their own.

		void show_column_menu ( int section, const QPoint& globalPosition );

		// EDITOR-22: the same gesture on a row index. Selects the row exactly as a left click there does, then offers
		// the row menu.

		void show_row_menu ( int row, const QPoint& globalPosition );

		// Cut, Copy, Paste and Paste Over, the group both header menus open with.

		void add_table_clipboard_items ( QMenu& menu );

		// EDITOR-18's Delete key. With a header selection live, Delete removes the row or the column; with none, it is
		// left alone, so a current cell's Delete stays exactly what it was.

		bool handle_delete_key ( const QKeyEvent& keyEvent );

		// Paste Over's key, Ctrl+Shift+V, answered while a row or column is selected and left alone otherwise.

		bool handle_paste_over_key ( const QKeyEvent& keyEvent );

		// UNDO-05 / EDITOR-18: scroll a row into view vertically, leaving the horizontal position where the user had it.
		// Does nothing when the row is already on screen.

		void reveal_row ( int row );

		// reveal_row on the next turn of the event loop, once the caller's change has been announced -- see the
		// implementation for the one case where it would otherwise not have been.

		void reveal_row_later ( int row );

		// UNDO-05: record a row an undo or redo acted on, keeping the first; revealed once the replay has finished.

		void note_replayed_row ( int row );

		// EDITOR-17: a double click on a row index drills into that ELEMENT (EDITOR-05's gesture, second target).

		void handle_row_index_activated ( int row );

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QTableView*       view;             // Non-owning; the widget this drives.
		IGridProjection*  projection;       // Non-owning; the model, asked only what it projects where.
		SelectionService* selection;        // Non-owning; may be null.

		Policy policy;

		// The array-table extras (EDITOR-11 / 12); all null for the object form.

		JsonTableModel*   tableModel = nullptr;
		JsonFormModel*    formModel  = nullptr;

		// EDITOR-16 / EDIT-15. The headers are non-owning (the QTableView owns them); the selection is owned here and
		// read by both headers and by JsonCellDelegate, which is what makes one field the single answer to "what does
		// a clipboard gesture act on".

		GridHeaderView* columnHeader = nullptr;
		GridHeaderView* rowHeader    = nullptr;

		HeaderSelection headerSelection;

		int           sortedColumn = -1;                          // -1 when the array is not known to be ordered.
		Qt::SortOrder sortedOrder  = Qt::AscendingOrder;

		// UNDO-05: the first row an undo or redo acted on, held until the replay has finished; -1 when there is none.

		int           rowToReveal  = -1;
		ClipboardService* clipboard  = nullptr;
		SettingsStore*    settings   = nullptr;
		StatusService*    status     = nullptr;
		IDialogService*   dialogs    = nullptr;                    // EDITOR-21 prompt; null disables Rename Column.

		JsonCellDelegate* cellDelegate = nullptr;   // Parented to this controller.

		// Breaks the selection feedback loop while an inbound selection is being applied, exactly as
		// TreeViewPane's own guard does. Also suppresses the provisional-row abandon while the controller is itself
		// moving the current cell (growing, materializing).

		bool applyingSelection = false;

		// The provisional-row commit is deferred (EDITOR-12): the value and column are stashed here between the
		// typed-entry commit and finish_provisional_materialize(), and growAfterMaterialize records whether the commit
		// was a downward advance (so a fresh provisional grows) or a plain commit (so the highlight lands on the new
		// real row).

		bool                      materializePending   = false;
		bool                      growAfterMaterialize = false;
		int                       pendingProvisionalColumn = -1;
		std::unique_ptr<JsonNode> pendingProvisionalValue;
	};
}
