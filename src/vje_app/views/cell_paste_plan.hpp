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
	//   - NeedsTypeConfirm:   value holds the SOURCE as it stands, the matrix having no conversion for the pairing --
	//                         warn that the cell's type changes and, on confirm, apply value (EDITOR-11, revised).
	//   - Incompatible:       message describes why (a refusal with no override); value is null.
	//
	// WHY THE TYPE MISMATCH IS A QUESTION AND THE SHAPE MISMATCH IS STILL A REFUSAL. Until 2026-08-20 a pairing the
	// matrix had no conversion for was simply refused, and the case that made that untenable is the ordinary one:
	// overwriting a column of numbers with a column of strings is a thing users do deliberately, and a modal that
	// says only "no" leaves them to retype by hand what they had already copied. So the matrix's verdict became the
	// REASON in a warning rather than the end of the gesture.
	//
	// It is offered where BOTH SIDES ARE SCALARS, and nowhere else. A scalar over a scalar replaces a value with a
	// value. A scalar over a CONTAINER destroys a subtree -- a delete wearing a paste's name, which is the argument
	// EDIT-09 already makes for why "convert to object" is not offered -- and a container over a SCALAR creates
	// structure in a column that has none, which is the jagged question SET-05 exists to answer and would be
	// answered twice, differently, if this route also let it through. Both keep the refusal they had.
	//-----------------------------------------------------------------------------------------------------------------

	enum class CellPastePlan
	{
		Apply,
		NeedsJaggedConfirm,
		NeedsTypeConfirm,
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
