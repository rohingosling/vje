//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   cell_paste_plan -- the pure composition of the array-table cell paste (EDITOR-11): CellPasteConverter's conversion
//   matrix, then JsonShapeComparer's structural check for a container source, then the SET-05 jagged-array decision.
//   It answers ONE question -- given a resolved source value, a target cell kind, the column's other values, and
//   whether jagged pastes are allowed, what should happen? -- and it answers it without a widget, so the whole paste
//   policy is pinned by a headless test rather than reasoned about behind a message box (which is all the controller
//   adds on top).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/services/CellPasteConverter.hpp>

#include <QString>

#include <memory>
#include <vector>

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// What the controller should do with a paste:
	//   - Apply:              value holds the node to place in the cell.
	//   - NeedsJaggedConfirm: value holds the node, but placing it makes the array jagged -- warn and, on confirm,
	//                         apply value (SET-05 is on).
	//   - Incompatible:       message describes why (the incompatible-data-type message box); value is null.
	//-----------------------------------------------------------------------------------------------------------------

	enum class CellPastePlan
	{
		Apply,
		NeedsJaggedConfirm,
		Incompatible
	};

	struct CellPasteDecision
	{
		CellPastePlan             plan = CellPastePlan::Incompatible;
		std::unique_ptr<JsonNode> value;
		QString                   message;
	};

	// columnValues are the column's OTHER cell values (the target cell excluded); null and missing cells may be passed
	// as nullptr or a null node and are ignored by the shape check (null is a wildcard). jaggedAllowed is SET-05's
	// "Allow jagged-array paste".

	CellPasteDecision plan_cell_paste
	(
		const JsonNode&                     source,
		CellTarget                          target,
		const std::vector<const JsonNode*>& columnValues,
		bool                                jaggedAllowed
	);
}
