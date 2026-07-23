//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

#include <QAbstractTableModel>
#include <QString>
#include <QStringList>

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

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		JsonDocument*   document;                               // Non-owning.
		UndoController* undo;                                   // Non-owning; may be null (read-only projection).

		JsonPointer arrayPointer;                               // What is projected; meaningful while arrayNode is set.
		JsonNode*   arrayNode = nullptr;                        // Non-owning; null when nothing is presented.

		// The shadow row list. Held as ADDRESSES ONLY and never dereferenced during a diff: after a removal it still
		// holds a pointer to a destroyed node, and reading through it would be undefined.

		std::vector<JsonNode*> rowNodes;

		QStringList columnKeys;                                 // Per-member columns; empty in single-value mode.
		bool        objectMode = false;
	};
}
