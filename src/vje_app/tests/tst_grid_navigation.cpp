//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for grid_navigation -- the current-cell movement math shared by the object form
//   (EDITOR-02) and the array table (EDITOR-03).
//
//   Two things here are worth more than the arithmetic they check. First, the LANDABILITY rule: nothing in this file
//   passes a value, because every cell is landable regardless of what it holds -- container, null placeholder, and
//   ragged-missing cells included. A future change that made the highlight skip a null cell would have to add a
//   parameter to be tested at all, which is the point.
//
//   Second, the BOTTOM EDGE. A downward move off the last row clamps, and is_edge_blocked() reports it -- that clamp is
//   the trigger EDITOR-12's provisional-row growth hangs off in Phase 9, so it is pinned now rather than discovered to
//   have drifted then.
//
//   Headless: pure functions, no Qt widgets, no model.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/grid_navigation.hpp"

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	// A 3-row, 4-column grid: big enough that every edge is distinct from every other.

	constexpr int ROW_COUNT    = 3;
	constexpr int COLUMN_COUNT = 4;

	GridPosition move_from ( int row, int column, GridMove move )
	{
		return next_position ( GridPosition { row, column }, move, ROW_COUNT, COLUMN_COUNT );
	}
}

class TestGridNavigation : public QObject
{
	Q_OBJECT

private slots:

	// Interior movement.

	void arrows_move_one_cell ();
	void tab_moves_in_reading_order ();
	void absolute_moves_reach_the_grid_corners ();

	// Edges.

	void arrows_clamp_at_every_edge ();
	void the_bottom_edge_is_reported_as_blocked ();
	void tab_wraps_across_rows_and_clamps_at_the_ends ();

	// Degenerate input.

	void an_empty_grid_has_no_position ();
	void a_position_outside_the_grid_is_refused ();
};

//---------------------------------------------------------------------------------------------------------------------
// Interior movement
//---------------------------------------------------------------------------------------------------------------------

void TestGridNavigation::arrows_move_one_cell ()
{
	QCOMPARE ( move_from ( 1, 1, GridMove::Up ),    ( GridPosition { 0, 1 } ) );
	QCOMPARE ( move_from ( 1, 1, GridMove::Down ),  ( GridPosition { 2, 1 } ) );
	QCOMPARE ( move_from ( 1, 1, GridMove::Left ),  ( GridPosition { 1, 0 } ) );
	QCOMPARE ( move_from ( 1, 1, GridMove::Right ), ( GridPosition { 1, 2 } ) );
}

void TestGridNavigation::tab_moves_in_reading_order ()
{
	QCOMPARE ( move_from ( 1, 1, GridMove::NextCell ),     ( GridPosition { 1, 2 } ) );
	QCOMPARE ( move_from ( 1, 1, GridMove::PreviousCell ), ( GridPosition { 1, 0 } ) );
}

void TestGridNavigation::absolute_moves_reach_the_grid_corners ()
{
	QCOMPARE ( move_from ( 1, 2, GridMove::RowStart ),  ( GridPosition { 1, 0 } ) );
	QCOMPARE ( move_from ( 1, 2, GridMove::RowEnd ),    ( GridPosition { 1, COLUMN_COUNT - 1 } ) );
	QCOMPARE ( move_from ( 1, 2, GridMove::GridStart ), ( GridPosition { 0, 0 } ) );
	QCOMPARE ( move_from ( 1, 2, GridMove::GridEnd ),   ( GridPosition { ROW_COUNT - 1, COLUMN_COUNT - 1 } ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Edges
//---------------------------------------------------------------------------------------------------------------------

void TestGridNavigation::arrows_clamp_at_every_edge ()
{
	// EDITOR-03: "the highlight stays put at the table's edges". Staying put is the specified behaviour, not a
	// degenerate case -- which is why it is asserted on all four sides rather than assumed symmetric.

	QCOMPARE ( move_from ( 0, 1, GridMove::Up ),    ( GridPosition { 0, 1 } ) );
	QCOMPARE ( move_from ( 1, 0, GridMove::Left ),  ( GridPosition { 1, 0 } ) );

	QCOMPARE ( move_from ( ROW_COUNT - 1,    1, GridMove::Down ),  ( GridPosition { ROW_COUNT - 1, 1 } ) );
	QCOMPARE ( move_from ( 1, COLUMN_COUNT - 1, GridMove::Right ), ( GridPosition { 1, COLUMN_COUNT - 1 } ) );
}

void TestGridNavigation::the_bottom_edge_is_reported_as_blocked ()
{
	// The one edge that will stop being a clamp: EDITOR-12 grows a provisional row here instead (Phase 9). The
	// controller recognizes the moment by exactly this predicate, so it is pinned now.

	const GridPosition lastRowCell { ROW_COUNT - 1, 1 };

	QVERIFY ( is_edge_blocked ( lastRowCell, GridMove::Down, ROW_COUNT, COLUMN_COUNT ) );

	// And it is genuinely specific to that edge -- a row up, the same move is free.

	QVERIFY ( !is_edge_blocked ( GridPosition { ROW_COUNT - 2, 1 }, GridMove::Down, ROW_COUNT, COLUMN_COUNT ) );
}

void TestGridNavigation::tab_wraps_across_rows_and_clamps_at_the_ends ()
{
	// Tab is reading order over the whole grid, not per-row movement.

	QCOMPARE ( move_from ( 0, COLUMN_COUNT - 1, GridMove::NextCell ),     ( GridPosition { 1, 0 } ) );
	QCOMPARE ( move_from ( 1, 0,                GridMove::PreviousCell ), ( GridPosition { 0, COLUMN_COUNT - 1 } ) );

	// The two ends of the grid have nowhere to wrap to.

	QCOMPARE
	(
		move_from ( ROW_COUNT - 1, COLUMN_COUNT - 1, GridMove::NextCell ),
		( GridPosition { ROW_COUNT - 1, COLUMN_COUNT - 1 } )
	);

	QCOMPARE ( move_from ( 0, 0, GridMove::PreviousCell ), ( GridPosition { 0, 0 } ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Degenerate input
//---------------------------------------------------------------------------------------------------------------------

void TestGridNavigation::an_empty_grid_has_no_position ()
{
	// An empty array's table still exists (EDITOR-12 gives it a provisional row in Phase 9), so "no current cell" has
	// to be a representable answer rather than an assertion failure.

	QVERIFY ( !next_position ( GridPosition { 0, 0 }, GridMove::Down, 0, 1 ).is_valid () );
	QVERIFY ( !next_position ( GridPosition { 0, 0 }, GridMove::Down, 3, 0 ).is_valid () );
}

void TestGridNavigation::a_position_outside_the_grid_is_refused ()
{
	// A stale coordinate -- the shape a controller holds for one instant after rows are removed under it.

	QVERIFY ( !next_position ( GridPosition { ROW_COUNT, 0 }, GridMove::Up,   ROW_COUNT, COLUMN_COUNT ).is_valid () );
	QVERIFY ( !next_position ( GridPosition { -1, 0 },        GridMove::Down, ROW_COUNT, COLUMN_COUNT ).is_valid () );
}

QTEST_APPLESS_MAIN ( TestGridNavigation )

#include "tst_grid_navigation.moc"
