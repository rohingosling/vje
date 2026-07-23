//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   grid_navigation -- the pure current-cell movement math shared by the array table (EDITOR-03) and the object form
//   (EDITOR-02). No Qt widgets, no model: given where the highlight is and how big the grid is, work out where it goes.
//   That is deliberate -- the spreadsheet keyboard model is the single most behaviour-dense part of Phase 7, and this
//   is the half of it that can be pinned by a headless test rather than by eye.
//
//   THE LANDABILITY RULE, STATED ONCE. Every cell is landable. Container cells ({...} / [...]), null placeholder cells,
//   and missing (ragged) cells all take the highlight exactly like a scalar (EDITOR-03) -- because each of them is a
//   paste target or a typed-entry target, and a highlight that skipped them would make those cells unreachable from the
//   keyboard. Landability and EDITABILITY are therefore different questions, and only the second one looks at the cell's
//   content (see cell_presentation.hpp). Nothing in this file inspects a value, which is what enforces that.
//
//   EDGE BEHAVIOUR. Arrows clamp: the highlight stays put at every edge. The reading-order moves (NextCell /
//   PreviousCell) wrap across the row boundary and clamp at the two ends of the grid; since NAV-04 took Tab from the
//   grid they are reachable only from INSIDE an open editor, where Tab still commits and advances (EDITOR-03). The one exception in the spec -- a downward move off the
//   last row growing a provisional row (EDITOR-12) -- is NOT here: it changes the document projection rather than the
//   position, so it belongs to the controller, and lands in Phase 9. This file reports the clamp; the controller may
//   choose to grow instead.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// A current-cell movement. The arrow set plus Tab's reading-order pair (EDITOR-03).
	//-----------------------------------------------------------------------------------------------------------------

	enum class GridMove
	{
		Up,
		Down,
		Left,
		Right,
		NextCell,        // Tab: right, wrapping to the start of the next row.
		PreviousCell,    // Shift+Tab: left, wrapping to the end of the previous row.
		RowStart,        // Home.
		RowEnd,          // End.
		GridStart,       // Ctrl+Home.
		GridEnd          // Ctrl+End.
	};

	//-----------------------------------------------------------------------------------------------------------------
	// A cell coordinate. Negative in either axis means "no current cell", which is the state an empty grid is in.
	//-----------------------------------------------------------------------------------------------------------------

	struct GridPosition
	{
		int row    = -1;
		int column = -1;

		bool is_valid () const;

		bool operator== ( const GridPosition& other ) const;
		bool operator!= ( const GridPosition& other ) const;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Where the highlight goes. Returns current unchanged when the move is blocked by an edge, so a caller can detect
	// "the move did nothing" by comparing against what it passed in -- which is exactly how the bottom-edge row growth
	// of EDITOR-12 will recognize its trigger without this file knowing anything about provisional rows.
	//
	// An out-of-range current position, or an empty grid, yields an invalid position.
	//-----------------------------------------------------------------------------------------------------------------

	GridPosition next_position ( GridPosition current, GridMove move, int rowCount, int columnCount );

	// Whether the move is blocked at its edge -- next_position would return current unchanged. Named separately because
	// "nothing happened" and "moved back to where it started" are the same value but different intents at the call site.

	bool is_edge_blocked ( GridPosition current, GridMove move, int rowCount, int columnCount );
}
