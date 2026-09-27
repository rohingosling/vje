//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tree_drop_plan -- what an EDIT-10 drag-and-drop reorder actually DOES, as a pure function over positions.
//
//   WHAT A DROP CONTAINS is Qt's: a QDrag, a drop indicator, and a landing row the view computes from the mouse. What
//   a drop DECIDES is here -- given how many siblings there are, which of them are being dragged, and where the run is
//   to land, which children move and in what order. That split is Phase 11's (FindController / FindBar) and Phase 12's
//   (XmlImportController / XmlImportDialog) a third time, and it is what makes this half testable at all: QDrag::exec
//   blocks on a nested event loop no offscreen test can drive, exactly as QMenu::exec does.
//
//   THE MOVES ARE A SEQUENCE, NOT A SET, and they are applied in the order returned. Each one renumbers the siblings
//   it passes, so a plan expressed any other way would be stale after its own first step -- the positional-pointer
//   trap (Q22) arriving one layer up. That is also why the moves name INDICES rather than nodes: an index is what
//   JsonNode::move_child and UndoController::move_child take, and what stays meaningful between two steps of the run.
//
//   AN EMPTY PLAN IS A LEGITIMATE ANSWER, not a failure. It is a drop that landed where the selection already was --
//   which is the one reorder no-op the application can reach, since whether one exists depends on the drop TARGET and
//   no enablement can pre-empt it (EDIT-10). The caller reports it; nothing is pushed and the document stays clean.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QList>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// One step of a reorder: lift the child at fromIndex out and put it back at toIndex, where toIndex is its position
	// in the container AFTER the lift (JsonNode::move_child's convention, and MoveNodeCommand's).
	//-----------------------------------------------------------------------------------------------------------------

	struct TreeDropMove
	{
		int fromIndex = 0;
		int toIndex   = 0;

		bool operator == ( const TreeDropMove& other ) const;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The reorder a drop asks for.
	//
	//   siblingCount   How many children the parent has. Every index below is within it.
	//   draggedIndices The children being dragged, ascending and distinct. May be NON-CONTIGUOUS: a Ctrl-built
	//                  selection is allowed to have gaps, and it lands as one contiguous run (EDIT-10).
	//   dropIndex      Where the run is to land, in the parent's CURRENT coordinates: the run goes immediately before
	//                  the child now at dropIndex, and siblingCount means "after the last child". This is the number
	//                  Qt's drop indicator names -- above row N is N, below row N is N + 1 -- so the caller passes on
	//                  what the view told it rather than converting into a second coordinate system.
	//
	// Returns the moves to apply in order, or an empty list when the arrangement is already the one asked for.
	// Out-of-range input returns an empty list rather than a partial plan: half a reorder is worse than none.
	//-----------------------------------------------------------------------------------------------------------------

	QList<TreeDropMove> plan_tree_drop ( int siblingCount, const QList<int>& draggedIndices, int dropIndex );

	//-----------------------------------------------------------------------------------------------------------------
	// Where the dragged run ends up, as the indices it occupies once the plan has been applied -- contiguous, ascending,
	// and in the same relative order the run was dragged in.
	//
	// The caller needs this to leave the moved nodes SELECTED (EDIT-10), and it is derived here rather than measured
	// afterwards because an array element's pointer is positional: reading the selection back off the document after
	// the moves would be asking the same question the plan has already answered, in a coordinate system that has just
	// changed underneath it.
	//-----------------------------------------------------------------------------------------------------------------

	QList<int> dropped_run_indices ( int siblingCount, const QList<int>& draggedIndices, int dropIndex );
}
