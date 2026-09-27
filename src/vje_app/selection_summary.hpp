//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   selection_summary -- what the status bar's NODE INFO pane reads (spec section 2.8), as a pure function.
//
//   WHY IT IS NOT JUST A BRANCH IN MainWindow. It is a stated rule with three cases -- nothing selected, one node, more
//   than one (TREE-09) -- and there is no MainWindow harness to check any of them through. Pulled out here it is an
//   ordinary headless assertion, which is the move window_title, printing/page_furniture, views/toolbar_plan and
//   controllers/edit_reporting each make for the same reason: the DECISION is testable even where the widget is not.
//
//   THE COUNT REPLACES THE TYPE rather than joining it. With several nodes selected, "object - 5 members" describes the
//   primary alone, which is less than what a multi-node command would act on -- so a reader would be told the type of
//   one node while the command acted on four. Naming the count instead is the one thing that makes a multiple selection
//   legible, and it is exactly the state in which the type is misleading.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

namespace vje
{
	class JsonNode;

	// The Node info pane's text for a selection of `selectedCount` nodes whose PRIMARY resolves to `primaryNode`
	// (null when it resolves to nothing, e.g. no document).
	//
	//   ( object with 5 members, 1 ) -> "object - 5 members"
	//   ( any node,              4 ) -> "4 nodes selected"
	//   ( nullptr,               0 ) -> ""
	//
	// The count is answered FIRST and without consulting the node, so a multiple selection stays legible even where the
	// primary momentarily does not resolve.

	QString selection_summary ( const JsonNode* primaryNode, int selectedCount );
}
