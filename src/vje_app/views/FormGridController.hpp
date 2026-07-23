//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
//   hand, and the two specs are in fact ONE keyboard model with exactly two written divergences, both carried here as
//   policy rather than as separate code paths:
//
//     - Left / Right inside an open editor navigate in the table and stay caret keys in the form (EDITOR-02), because
//       form values are long where table cells are short.
//     - A form field writes its focus back to the selection service with SelectionOrigin::FormField; a table cell
//       deliberately does NOT (EDITOR-04) -- in-place cell editing must not drag the tree around.
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

class QModelIndex;
class QTableView;

namespace vje
{
	class IGridProjection;
	class JsonCellDelegate;
	class SelectionService;

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
		// not one of these four is shared behaviour and belongs in the code below, not here.

		struct Policy
		{
			bool arrowKeysNavigateInEditor = true;    // Table: spreadsheet arrows. Form: Left / Right are caret keys.
			bool writesSelectionBack       = false;   // Form: yes (EDITOR-04). Table: deliberately not.

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

		FormGridController
		(
			QTableView*       view,
			IGridProjection*  projection,
			SelectionService* selection,
			const Policy&     policy,
			QObject*          parent = nullptr
		);

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
		// Data Members
		//=============================================================================================================

	private:

		QTableView*       view;             // Non-owning; the widget this drives.
		IGridProjection*  projection;       // Non-owning; the model, asked only what it projects where.
		SelectionService* selection;        // Non-owning; may be null.

		Policy policy;

		JsonCellDelegate* cellDelegate = nullptr;   // Parented to this controller.

		// Breaks the selection feedback loop while an inbound selection is being applied, exactly as
		// TreeViewPane's own guard does.

		bool applyingSelection = false;
	};
}
