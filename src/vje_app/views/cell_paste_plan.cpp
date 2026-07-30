//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   cell_paste_plan implementation. See the header for the composition it expresses.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/cell_paste_plan.hpp"

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/JsonShapeComparer.hpp>

#include <QObject>

namespace vje
{
	CellPasteDecision plan_cell_paste
	(
		const JsonNode&                     source,
		CellTarget                          target,
		const std::vector<const JsonNode*>& columnValues,
		bool                                jaggedAllowed
	)
	{
		CellPasteDecision decision;

		CellPasteOutcome outcome = CellPasteConverter::resolve_paste ( source, target );

		switch ( outcome.resolution )
		{
			case PasteResolution::Incompatible:
			{
				decision.plan    = CellPastePlan::Incompatible;
				decision.message = outcome.message;

				return decision;
			}

			case PasteResolution::Converted:
			{
				decision.plan  = CellPastePlan::Apply;
				decision.value = std::move ( outcome.value );

				return decision;
			}

			case PasteResolution::NeedsShapeCheck:
			{
				// A container source into a matching / untyped target: shape-compatible pastes go straight in; a
				// mismatch is refused unless SET-05 lets it through as a jagged paste (EDITOR-11).

				if ( JsonShapeComparer::compatible_with_column ( *outcome.value, columnValues ) )
				{
					decision.plan  = CellPastePlan::Apply;
					decision.value = std::move ( outcome.value );

					return decision;
				}

				if ( jaggedAllowed )
				{
					decision.plan  = CellPastePlan::NeedsJaggedConfirm;
					decision.value = std::move ( outcome.value );

					return decision;
				}

				decision.plan    = CellPastePlan::Incompatible;
				decision.message = QObject::tr
				(
					"The pasted structure does not match this column's shape. Enable \"Allow jagged-array paste\" in "
					"Settings to paste it anyway."
				);

				return decision;
			}
		}

		return decision;
	}
}
