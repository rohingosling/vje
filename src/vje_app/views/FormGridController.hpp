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

#include "views/grid_navigation.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QObject>
#include <QPoint>

#include <memory>

class QModelIndex;
class QTableView;

namespace vje
{
	class ClipboardService;
	class IGridProjection;
	class JsonCellDelegate;
	class JsonNode;
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

			int  contextMenuColumn   = -1;            // Form: the key column (EDITOR-02); -1 offers no menu.
		};

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The view must already have its model set; the controller installs its own delegate on it. The selection
		// service may be null, which simply disables the write-back (the form the projection tests use).

		// The cell clipboard (EDITOR-11) and provisional-row growth (EDITOR-12) are the ARRAY TABLE's alone, so they are
		// gathered in one optional collaborator set: pass a non-null tableModel (the same object as projection, typed
		// concretely) plus the clipboard / settings / status for the table controller, and leave them null for the
		// object form. The form thereby gets none of this behaviour, which is exactly OQ-2's deferral.

		struct TableSupport
		{
			JsonTableModel*   model     = nullptr;   // The provisional-capable projection (== projection for the table).
			ClipboardService* clipboard = nullptr;
			SettingsStore*    settings  = nullptr;   // SET-05 "Allow jagged-array paste".
			StatusService*    status    = nullptr;   // The copy / cut / paste confirmations.
		};

		// tableSupport is empty for the object form (its members default to null); the array table passes its clipboard /
		// settings / status / model. It is not defaulted here because a nested-type default argument cannot use the
		// struct's own member initializers before the enclosing class is complete -- FormView always passes one.

		FormGridController
		(
			QTableView*         view,
			IGridProjection*    projection,
			SelectionService*   selection,
			const Policy&       policy,
			const TableSupport& tableSupport,
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

		// Is a cell clipboard command applicable right now -- the array table with a current cell? Used for the
		// disabled-not-hidden enablement of Edit > Cut / Copy / Paste.

		bool cell_clipboard_active () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// A {...} / [...] cell was activated (EDITOR-05). Emitted from the event loop rather than from the gesture, so
		// the consumer is free to re-present the pane.

		void drill_in_requested ( const JsonPointer& pointer );

		// The node context menu was asked for on a key label (EDITOR-02, TREE-06's action set).

		void context_menu_requested ( const JsonPointer& pointer, const QPoint& globalPosition );

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
		ClipboardService* clipboard  = nullptr;
		SettingsStore*    settings   = nullptr;
		StatusService*    status     = nullptr;

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
