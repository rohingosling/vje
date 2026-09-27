//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonTableModel -- the QAbstractTableModel projecting an ARRAY node as the Form View's table (EDITOR-03). One row per
//   element; cells edit in place through the undo stack.
//
//   THE COLUMN PROJECTION, AND THE ONE RULE THE SPEC LEAVES TO US. An array whose elements are ALL objects projects one
//   column per member key -- the union of every element's keys in first-encountered order, so a ragged element simply
//   shows empty (MISSING) cells for the members it lacks (EDITOR-03). Any other array -- scalars, or a mix of kinds --
//   projects a SINGLE value column.
//
//   The mixed case is the rule the spec does not state, and the choice is deliberate: an array holding both objects and
//   scalars has no honest column set (the scalars belong to no key), so it renders single-column with the object
//   elements showing as "{...}", which stays landable, drillable, and truthful. Document > Normalize Array Elements
//   (EDIT-11) is the repair that makes it a real table. Note this is a DIFFERENT condition from EDITOR-11's "ragged":
//   ragged means objects with differing key sets, which the key union already handles as a proper table.
//
//   INCREMENTAL UPDATES. Rows are diffed by JsonNode ADDRESS against a shadow vector, exactly as JsonTreeModel
//   diffs its projection and for the same reason: JsonDocument reports a change after the fact and names only the
//   container, so there is nothing left to bracket a beginRemoveRows around. Diffing by identity is what lets a row
//   append emit beginInsertRows on the live model rather than a reset -- which is what keeps column widths, the current
//   cell, and the scroll position through an edit (EDITOR-03's "a cell commit refreshes values in the existing table
//   rather than rebuilding it"). A value edit is patched to the single cell it touched and nothing else.
//
//   The two changes that genuinely cannot be patched -- the projected node being replaced wholesale, and the column
//   mode flipping between single-value and per-key -- reset the model, because in both cases every cell means something
//   different afterwards.
//
//   WHAT IS NOT HERE. The cell clipboard (EDITOR-11) and provisional-row growth (EDITOR-12) are Phase 9. The model is
//   shaped for them -- MISSING cells already carry the pointer their member WOULD occupy -- but holds none of that
//   behaviour, which belongs to FormTableController.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "models/IGridProjection.hpp"
#include "views/grid_navigation.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/editing/UndoController.hpp>            // EditOutcome, returned by sort_by_column (EDIT-15).
#include <vje_core/services/json_escapes.hpp>

#include <QAbstractTableModel>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace vje
{
	class UndoController;

	//*****************************************************************************************************************
	// Class: JsonTableModel
	//*****************************************************************************************************************

	class JsonTableModel : public QAbstractTableModel, public IGridProjection
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The undo controller may be null, which yields a READ-ONLY projection (every setData is refused). That is the
		// form a test uses when it is asserting the projection rather than the edit path.

		JsonTableModel ( JsonDocument* document, UndoController* undo = nullptr, QObject* parent = nullptr );

		//=============================================================================================================
		// QAbstractTableModel
		//=============================================================================================================

	public:

		int rowCount    ( const QModelIndex& parent = QModelIndex () ) const override;
		int columnCount ( const QModelIndex& parent = QModelIndex () ) const override;

		QVariant      data       ( const QModelIndex& index, int role = Qt::DisplayRole ) const override;
		QVariant      headerData ( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const override;
		Qt::ItemFlags flags      ( const QModelIndex& index ) const override;

		bool setData ( const QModelIndex& index, const QVariant& value, int role = Qt::EditRole ) override;

		//=============================================================================================================
		// Presentation
		//=============================================================================================================

	public:

		// Project the array the pointer names. A pointer that does not resolve, or resolves to something other than an
		// array, presents nothing (an empty table) rather than failing -- the Form View decides what to show instead.

		void present ( const JsonPointer& arrayPointer );

		void clear_presentation ();

		bool               is_presenting    () const;
		const JsonPointer& presented_pointer () const;

		// Whether the projection is per-member-key (true) or a single value column (false). See the header note.

		bool is_object_table () const;

		// The member key a column projects, or nullopt in single-value mode, where the column IS the elements. An
		// optional rather than the header text because "" is a legal JSON member key, so an empty string cannot double
		// as the "no key" sentinel -- which is the same reason json_order::sort_permutation takes one (EDIT-15).
		//
		// Out of range yields nullopt, which a caller must not confuse with single-value mode; every caller here asks
		// about a column the header just reported a click on.

		std::optional<QString> column_key ( int column ) const;

		// The column's NAME, which is not the same question as its member key (EDIT-16). In object mode the two agree;
		// in single-value mode the column has no member key but IS named -- after the array, reusing section 2.12's
		// CSV-header rule, so `roles` copies as `roles` rather than as a fixed placeholder.

		QString column_name ( int column ) const;

		// The array this table is projecting. Exposed so the header route can ask plan_paste_target the same question
		// the tree route asks it -- EDIT-16 requires one answer, and two callers computing it is how that stops being
		// true. nullptr when nothing is presented.

		const JsonNode* presented_array () const;

		//=============================================================================================================
		// Provisional row (EDITOR-12)
		//
		// A view-only trailing empty row the CONTROLLER grows on a bottom-edge downward move and abandons on any leave.
		// The document, dirty state, and undo stack are untouched until a value is committed into one of its cells, at
		// which point the row MATERIALIZES as one add-element command. The model owns the row's display and the
		// materialize primitive; the FormGridController owns when it grows and when it is abandoned.
		//=============================================================================================================

	public:

		int  element_count () const;                             // The real (document-backed) row count.

		void set_provisional_row  ( bool active );               // Add / remove the trailing view-only row.
		bool has_provisional_row  () const;
		bool is_provisional_row   ( int row ) const;             // Is this the trailing provisional row?

		// Materialize the provisional row as element [element_count()] -- an object carrying value under the given
		// column's key plus null for every other column (object table), or value itself (single-value table). One
		// undoable add-element command; the provisional row is removed and the real row inserted in its place. The
		// caller restores the current cell afterwards. Returns false if there is no provisional row or the add is
		// rejected.

		bool materialize_provisional ( int column, std::unique_ptr<JsonNode> value );

		//=============================================================================================================
		// Cell value application (EDITOR-11 paste, EDITOR-12 typed entry)
		//
		// Apply a resolved value to an existing (non-provisional) cell: a MISSING cell creates the member at the
		// column-order position, and any other cell replaces its value wholesale. One undoable command. The value is
		// already resolved / converted by the caller (the CellPasteConverter matrix, or the typed-entry JSON-literal
		// rule); this is only the write. text is the undo-step label, supplied by the gesture ("Paste", "Edit Cell",
		// "Cut Cell") so the Edit menu describes what actually happened rather than the funnel it went through.
		//=============================================================================================================

	public:

		bool apply_cell_value ( int row, int column, std::unique_ptr<JsonNode> value, const QString& text );

		// EDIT-15: order the PRESENTED array by one of its columns, as one undo step. The model rather than the
		// controller, for the reason apply_cell_value is here -- it is the table's edit funnel, and it is the only
		// piece that knows which member key a column projects and which array the table is presenting.
		//
		// Returns the outcome so the caller can report it (VAL-05); the model itself reports nothing, exactly as
		// apply_cell_value does not. Rejected on a read-only projection or a column that names nothing.

		EditOutcome sort_by_column ( int column, Qt::SortOrder order );

		//=============================================================================================================
		// Row and column operations (EDITOR-18). Here rather than in the controller for sort_by_column's reason: the
		// model is the table's edit funnel, and it is the only piece that knows which array is presented, which member
		// key a column projects, and which element a row is.
		//=============================================================================================================

	public:

		// One resolved value bound for one cell, for the multi-cell paste below. The value is owned and consumed.

		struct CellAssignment
		{
			int                       row    = -1;
			int                       column = -1;
			std::unique_ptr<JsonNode> value;
		};

		// EDITOR-18: apply several already-resolved cell values as ONE undo step. The values must have been through
		// EDITOR-11's conversion matrix already -- this applies, it does not decide, exactly as apply_cell_value does.
		//
		// Returns how many assignments reached the document. An empty assignment list pushes nothing at all, which is
		// what the lazy MacroScope is for.

		int apply_cell_values ( std::vector<CellAssignment>& assignments, const QString& text );

		// EDITOR-18's GROWTH primitives (revised 2026-08-18: a paste extends its target rather than truncating).
		//
		// One member bound for a paste, carrying the key the SOURCE column had. Growth needs a name for the value and
		// only the source has one -- an appended element has to be keyed with something, and "the column it came
		// from" is the only answer that reproduces what was copied.

		struct PastedMember
		{
			QString                   key;                    // Null for a keyless (scalar-table) source.
			std::unique_ptr<JsonNode> value;
		};

		// Append an element built from these members, filling every column the table ALREADY has and the members do
		// not supply with `null` -- so a paste that grows the array leaves it uniform rather than ragged, which is
		// EDIT-11's fill value applied at the moment the element is created rather than afterwards.
		//
		// A single member with a null key appends the bare VALUE as the element, which is what a scalar array grows by.

		EditOutcome append_pasted_element ( std::vector<PastedMember>& members );

		// EDITOR-18's INSERT (2026-09-23): the element a pasted row becomes, built BY KEY and not yet placed -- so the
		// controller can shape-check it before anything changes, and the insert below cannot disagree with the check.
		//
		// On an array of objects: every column the table has, as `null`, then each pasted member written over the
		// one it names; a name the table does not have is appended and listed in newKeys, since every other element
		// will need it too. On a single-value table: the bare value where the source was single-value (keyed false,
		// its one cell being its element), and an object of the pasted members where it was not. Values are CLONED --
		// the members are still wanted for the insert. Null when the source holds nothing to build from.

		std::unique_ptr<JsonNode> build_pasted_element
		(
			const std::vector<PastedMember>& members,
			bool                             keyed,
			QStringList*                     newKeys
		) const;

		// Insert that element IN FRONT OF row -- the provisional row's index means the end -- and give every other
		// object element the new keys as `null`, so the columns the row brought exist across the whole array. One
		// undo step. The provisional row is dropped first, as materialize_provisional drops it.

		EditOutcome insert_pasted_element ( int row, std::unique_ptr<JsonNode> element, const QStringList& newKeys );

		// The column projecting this member key, or -1. Object mode only; a single-value table has no member keys.

		int column_index ( const QString& key ) const;

		// The stack these operations push onto. Exposed ONLY so a caller whose gesture spans several of them can open
		// one MacroScope over the lot -- EDITOR-18's paste both assigns and grows, and "one undo step" is a property
		// of the paste rather than of either half. May be null (the read-only projection a projection test uses).

		UndoController* undo_controller () const;

		// Create a member on one element and `null` under the same key on every OTHER element, so a column a paste
		// introduces exists across the whole array. The second half is what keeps "new cells are null" true of columns
		// as well as of rows.

		EditOutcome create_pasted_member ( int row, const QString& key, std::unique_ptr<JsonNode> value );

		// EDITOR-18: remove a row's ELEMENT. One undo step; Rejected for the provisional row, which is not an element.

		EditOutcome delete_row ( int row );

		// EDITOR-18: remove a column's member from EVERY element of the array, as one undo step.
		//
		// Rejected on a single-value (scalar) table, which has no member to remove: emptying the array is a delete of
		// every ROW wearing this command's name. Unchanged when no element carries the member.

		EditOutcome delete_column ( int column );

		// EDITOR-19: set every cell of a column to `null`, leaving the column itself in place. One undo step.
		//
		// It ACCEPTS the single-value (scalar) table that delete_column refuses, and the divergence follows from what
		// each command does rather than from a rule about scalar arrays: a delete removes a member and there is none,
		// while a clear writes `null` and there is somewhere to write it -- [ "a", "b" ] becomes [ null, null ], which
		// is still an array of two elements.
		//
		// A cell the element LACKS stays absent. Filling it would be Normalize (EDIT-11) wearing this command's name,
		// and it would erase the distinction section 2.12 keeps between "does not have that member" and "has it, and
		// it is null". Unchanged when every cell that exists is already null.

		EditOutcome clear_column ( int column );

		// EDITOR-22: set every cell of a row to `null`, leaving the element itself in place. One undo step.
		//
		// clear_column's rules, turned through a right angle: a member the element LACKS stays absent (a clear is not
		// a Normalize), an already-null cell is skipped, and a single-value table's one cell is the ELEMENT, so
		// [ "a", "b" ] clears row 1 to [ "a", null ]. A container cell -- a {...} member, or a single-value table's
		// nested element -- becomes `null` like any other, which is what "clear" means for a cell that shows a
		// placeholder rather than a value.
		//
		// Rejected for the provisional row and for a row out of range, neither being an element. Unchanged when every
		// cell that exists is already null -- which includes an empty object, whose cells are all absent.

		EditOutcome clear_row ( int row );

		// EDITOR-21: rename a column's member key on EVERY element that carries it, as one undo step.
		//
		// THE WHOLE RENAME IS PLANNED BEFORE ANY OF IT IS APPLIED, which is EDITOR-18's rule reached from a different
		// command: a rename that renamed twenty elements and then met an element already carrying the new key would
		// leave the array half-renamed under a single undo step, and the column would be two columns.
		//
		// Rejected on a single-value (scalar) table, which has no member to rename; Rejected where any element
		// already carries the new key, unless SET-03a permits the duplicate. Unchanged where the name is the one it
		// already has, and where no element carries the column at all.

		EditOutcome rename_column ( int column, const QString& newKey );

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// A value was committed into a PROVISIONAL cell through the editor (typed entry). The materialize is DEFERRED
		// off this signal, because an editor is still open on the row that is about to be removed (the controller runs
		// it queued). Paste, which happens with no editor open, materializes synchronously and does not emit this.

		void provisional_commit_pending ( int column, const QString& text );

		// EDIT-15: the presented array changed through a DOCUMENT edit, so any claim about the order it is in -- the
		// header's sort marker -- can no longer be made safely. Deliberately not emitted for the view-only provisional
		// row, which adds no element and changes no order, and deliberately not derived by the controller from
		// modelReset / rowsInserted, which the provisional row also produces.

		void presented_array_changed ();

		// UNDO-05: a change landed AT or INSIDE one element -- its row. Raised for the pointer the document announced,
		// so a change spanning several elements (a column, a sort, an insert or removal) names the ARRAY and raises
		// nothing here; rows inserted or removed are the model's own rowsInserted / rowsRemoved.

		void element_changed ( int row );

		//=============================================================================================================
		// Cell Addressing
		//=============================================================================================================

	public:

		// The node a cell projects; nullptr for a MISSING cell (a ragged element's absent member) or an out-of-range
		// coordinate. Callers distinguish the two through cell_content() on the result plus an index validity check.

		JsonNode* node_for_cell ( int row, int column ) const;

		// The JSON Pointer naming a cell. For a MISSING cell this is the pointer the member WOULD occupy, which is what
		// makes a create-the-member paste addressable in Phase 9 (EDITOR-11). A root pointer is returned for an
		// out-of-range coordinate.

		JsonPointer pointer_for_cell ( int row, int column ) const;

		// The pointer naming a ROW's element -- the element itself, not any cell inside it. EDITOR-17's drill-in and
		// EDITOR-18's row commands both act on the element, which in single-value mode is the same node cell (row, 0)
		// projects and in object mode is its parent. A root pointer for an out-of-range or provisional row.

		JsonPointer element_pointer ( int row ) const;

		// The row's element itself. In single-value mode this is the node cell ( row, 0 ) projects; in object mode it
		// is that cell's parent. nullptr for the provisional row and for an out-of-range one.

		JsonNode* element_node ( int row ) const;

		// The cell a pointer names, or an invalid position when the pointer is outside this table. Used to move the
		// current cell onto the element a tree selection picked (EDITOR-04) and to patch a single cell on a value edit.

		GridPosition cell_for_pointer ( const JsonPointer& pointer ) const;

		//=============================================================================================================
		// IGridProjection -- the controller-facing spelling of the four accessors above. A table cell edits itself, so
		// grid_edit_cell is the identity here (the object form is where it is not).
		//=============================================================================================================

	public:

		JsonNode*    grid_node      ( int row, int column ) const override;
		JsonPointer  grid_pointer   ( int row, int column ) const override;
		GridPosition grid_cell      ( const JsonPointer& pointer ) const override;
		GridPosition grid_edit_cell ( int row, int column ) const override;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_document_reset ();
		void handle_node_changed   ( const JsonPointer& pointer, DocumentChange change );

		//=============================================================================================================
		// Helpers -- projection
		//=============================================================================================================

	private:

		void rebuild ();                                        // Reset and re-derive rows + columns from the node.
		void resync  ();                                        // Patch rows + columns incrementally.

		void capture_rows    ();                                // Refill the shadow row vector from the node.
		QStringList derive_columns () const;                    // The key union, or empty for single-value mode.
		bool        derive_object_mode () const;                // Every element an object, and at least one element.

		bool resync_columns ();                                 // Diff columnKeys; false if it needs a full reset.
		void resync_rows    ();                                 // Diff rowNodes by identity.

		// The cell a descendant pointer belongs to, walking up until it lands on one. Used so a deep value change still
		// repaints the top-level container cell that contains it.

		GridPosition enclosing_cell ( const JsonPointer& pointer ) const;

		bool covers ( const JsonPointer& pointer ) const;       // Is pointer the array itself or inside it?

		//=============================================================================================================
		// Helpers -- editing
		//=============================================================================================================

	private:

		bool commit_cell ( JsonNode* target, const JsonPointer& pointer, const QVariant& value );

		// EDITOR-12: build the element a provisional-row commit materializes -- an object with value under the committed
		// column's key and null for every other column (object table), or value itself (single-value table).

		std::unique_ptr<JsonNode> build_new_element ( int column, std::unique_ptr<JsonNode> value ) const;

		// EDITOR-11: create the absent member of a ragged element, inserted after the nearest preceding column the
		// element already has (so the column order is respected).

		bool create_missing_member ( int row, int column, std::unique_ptr<JsonNode> value, const QString& text );

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// How string values are shown and typed (SET-03). Pushed in by the view, which owns the settings; the model
		// holds it because the model is what produces the display text, the edit text, and the commit -- three answers
		// that have to agree, and would not if each read the setting for itself.

		void set_string_display ( StringDisplay mode );

		// The notation in force, for the one caller that has to interpret a typed entry without going through setData:
		// FormGridController's provisional row, which reads the text while the editor is still open (EDITOR-12).

		StringDisplay string_display () const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		JsonDocument*   document;                               // Non-owning.
		UndoController* undo;                                   // Non-owning; may be null (read-only projection).

		// The SET-03 notation. Decoded is the identity, which is what a model constructed without a view gets -- a test
		// then sees the raw characters unless it says otherwise.

		StringDisplay stringDisplay = StringDisplay::Decoded;


		JsonPointer arrayPointer;                               // What is projected; meaningful while arrayNode is set.
		JsonNode*   arrayNode = nullptr;                        // Non-owning; null when nothing is presented.

		// The shadow row list. Held as ADDRESSES ONLY and never dereferenced during a diff: after a removal it still
		// holds a pointer to a destroyed node, and reading through it would be undefined.

		std::vector<JsonNode*> rowNodes;

		QStringList columnKeys;                                 // Per-member columns; empty in single-value mode.
		bool        objectMode = false;

		// EDITOR-12: whether the view-only trailing provisional row is currently shown. Never a document row -- rowCount
		// adds one for it, and every cell of it projects as MISSING (landable, editable, empty).

		bool provisionalActive = false;
	};
}
