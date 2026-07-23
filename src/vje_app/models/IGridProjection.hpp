//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   IGridProjection -- the two questions a Form View grid controller has to ask its model, and the only ones: what
//   node is this cell showing, and what is that node's JSON Pointer. Both JsonFormModel and JsonTableModel implement
//   it, which is what lets ONE controller drive the object form and the array table (see FormGridController).
//
//   It is a plain abstract class rather than a QObject: the models are already QAbstractTableModels, and adding a
//   second QObject base would be both illegal and unnecessary -- nothing here signals.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/grid_navigation.hpp"

#include <vje_core/document/JsonPointer.hpp>

namespace vje
{
	class JsonNode;

	//*****************************************************************************************************************
	// Class: IGridProjection
	//*****************************************************************************************************************

	class IGridProjection
	{
		//=============================================================================================================
		// Constructors / Destructor
		//=============================================================================================================

	public:

		virtual ~IGridProjection () = default;

		//=============================================================================================================
		// Cell Addressing
		//=============================================================================================================

	public:

		// The node a cell stands for. nullptr means the cell has no value behind it -- a ragged array's missing member,
		// or an out-of-range coordinate. Note "stands for" rather than "displays": the object form's LABEL column
		// answers with the row's value, so a gesture on a label acts on the field it names.

		virtual JsonNode* grid_node ( int row, int column ) const = 0;

		// That node's JSON Pointer. For a missing cell it is the pointer the member would occupy.

		virtual JsonPointer grid_pointer ( int row, int column ) const = 0;

		// The cell a pointer names, or an invalid position when the pointer is outside this grid.

		virtual GridPosition grid_cell ( const JsonPointer& pointer ) const = 0;

		// The cell whose EDITOR should open when this cell is activated. The identity for a table cell; the row's value
		// column for the object form, so activating a label edits the field it labels.

		virtual GridPosition grid_edit_cell ( int row, int column ) const = 0;
	};
}
