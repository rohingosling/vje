//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for tree_drop_plan (EDIT-10) -- what a drag-and-drop reorder actually does, headlessly.
//
//   IT CARRIES MOST OF THE PHASE'S DRAG HALF. QDrag::exec blocks on a nested event loop no offscreen test can drive,
//   so a real drag is manual-smoke territory; everything a drop DECIDES was pulled out into this function precisely so
//   that it does not have to be.
//
//   THE MOVES ARE CHECKED BY REPLAYING THEM, not by matching a list of index pairs. A plan is only correct if applying
//   it in order produces the arrangement asked for, and asserting the pairs would pin one particular route to that
//   arrangement -- so a better route would fail a passing test while a plan that is right about every pair and wrong
//   about their ORDER would sail through. apply_plan below is JsonNode::move_child's semantics on a list of labels.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/tree_drop_plan.hpp"

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	//-----------------------------------------------------------------------------------------------------------------
	// The container's children, as their original positions, before anything moves.
	//-----------------------------------------------------------------------------------------------------------------

	QList<int> identity ( int siblingCount )
	{
		QList<int> order;

		for ( int index = 0; index < siblingCount; ++index )
		{
			order.append ( index );
		}

		return order;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Apply a plan the way the document will: each move lifts a child out and puts it back, in the order given.
	// QList::move is JsonNode::move_child's semantics, which is what makes this a stand-in rather than a re-derivation.
	//-----------------------------------------------------------------------------------------------------------------

	QList<int> apply_plan ( int siblingCount, const QList<TreeDropMove>& moves )
	{
		QList<int> order = identity ( siblingCount );

		for ( const TreeDropMove& move : moves )
		{
			order.move ( move.fromIndex, move.toIndex );
		}

		return order;
	}
}

class TestTreeDropPlan : public QObject
{
	Q_OBJECT

private slots:

	void a_single_node_moves_up_to_the_drop_position ();
	void a_single_node_moves_down_to_the_drop_position ();
	void a_drop_where_the_node_already_is_moves_nothing ();
	void a_drop_just_below_the_node_itself_moves_nothing ();
	void a_contiguous_run_lands_together_and_keeps_its_order ();
	void a_non_contiguous_run_lands_contiguous_and_in_document_order ();
	void a_run_moving_down_lands_after_the_children_it_passed ();
	void the_whole_selection_dropped_at_its_own_position_moves_nothing ();
	void the_landing_indices_are_where_the_run_actually_ends_up ();
	void out_of_range_input_produces_no_plan_at_all ();
	void every_drop_position_produces_a_replayable_plan ();
};

//=======================================================================================================================
// One node
//=======================================================================================================================

void TestTreeDropPlan::a_single_node_moves_up_to_the_drop_position ()
{
	const QList<TreeDropMove> moves = plan_tree_drop ( 4, { 2 }, 0 );

	QCOMPARE ( apply_plan ( 4, moves ), QList<int> ( { 2, 0, 1, 3 } ) );

	// One move, not a shuffle of everything after it: a plan that produced the right arrangement by rewriting the
	// whole container would put three no-op commands on the undo stack inside the macro.

	QCOMPARE ( moves.size (), 1 );
}

void TestTreeDropPlan::a_single_node_moves_down_to_the_drop_position ()
{
	// Drop index 4 means "before the child now at 4", i.e. after child 3 -- and child 0 is below it, so the run lands
	// at position 3 once it has been lifted out. That subtraction is the whole of the coordinate translation.

	const QList<TreeDropMove> moves = plan_tree_drop ( 5, { 0 }, 4 );

	QCOMPARE ( apply_plan ( 5, moves ), QList<int> ( { 1, 2, 3, 0, 4 } ) );
}

//=======================================================================================================================
// The no-op -- EDIT-10's one reachable reorder no-op
//=======================================================================================================================

void TestTreeDropPlan::a_drop_where_the_node_already_is_moves_nothing ()
{
	QVERIFY ( plan_tree_drop ( 4, { 2 }, 2 ).isEmpty () );
}

void TestTreeDropPlan::a_drop_just_below_the_node_itself_moves_nothing ()
{
	// The OTHER spelling of the same non-move, and the one a user actually produces: releasing over the lower half of
	// the row being dragged gives drop index 3 for child 2. Both have to be no-ops, or a drag that visibly went
	// nowhere would still dirty the document and add an undo step.

	QVERIFY ( plan_tree_drop ( 4, { 2 }, 3 ).isEmpty () );
}

void TestTreeDropPlan::the_whole_selection_dropped_at_its_own_position_moves_nothing ()
{
	QVERIFY ( plan_tree_drop ( 6, { 2, 3, 4 }, 2 ).isEmpty () );
	QVERIFY ( plan_tree_drop ( 6, { 2, 3, 4 }, 5 ).isEmpty () );
}

//=======================================================================================================================
// Several nodes
//=======================================================================================================================

void TestTreeDropPlan::a_contiguous_run_lands_together_and_keeps_its_order ()
{
	const QList<TreeDropMove> moves = plan_tree_drop ( 6, { 3, 4 }, 1 );

	QCOMPARE ( apply_plan ( 6, moves ), QList<int> ( { 0, 3, 4, 1, 2, 5 } ) );
}

void TestTreeDropPlan::a_non_contiguous_run_lands_contiguous_and_in_document_order ()
{
	// EDIT-10 allows a Ctrl-built selection with gaps in it and lands it as ONE run. The order inside the run is
	// document order rather than the order the user clicked, which is what TREE-09 already guarantees of the set.

	const QList<TreeDropMove> moves = plan_tree_drop ( 5, { 1, 3 }, 0 );

	QCOMPARE ( apply_plan ( 5, moves ), QList<int> ( { 1, 3, 0, 2, 4 } ) );
}

void TestTreeDropPlan::a_run_moving_down_lands_after_the_children_it_passed ()
{
	const QList<TreeDropMove> moves = plan_tree_drop ( 5, { 0, 1 }, 4 );

	QCOMPARE ( apply_plan ( 5, moves ), QList<int> ( { 2, 3, 0, 1, 4 } ) );
}

//=======================================================================================================================
// Where the run ends up
//=======================================================================================================================

void TestTreeDropPlan::the_landing_indices_are_where_the_run_actually_ends_up ()
{
	// The claim is that dropped_run_indices agrees with the plan it accompanies -- which is the property MainWindow
	// depends on when it re-selects the moved nodes without re-reading the document. Checked by replaying the plan
	// and looking at what is standing at each reported position, rather than by restating the arithmetic.

	const int        siblingCount = 6;
	const QList<int> dragged      = { 1, 4 };
	const int        dropIndex    = 0;

	const QList<int> landing = dropped_run_indices ( siblingCount, dragged, dropIndex );
	const QList<int> after   = apply_plan ( siblingCount, plan_tree_drop ( siblingCount, dragged, dropIndex ) );

	QCOMPARE ( landing.size (), dragged.size () );

	for ( int step = 0; step < landing.size (); ++step )
	{
		QCOMPARE ( after [ landing [ step ] ], dragged [ step ] );
	}
}

//=======================================================================================================================
// Ill-formed input
//=======================================================================================================================

void TestTreeDropPlan::out_of_range_input_produces_no_plan_at_all ()
{
	// A PARTIAL plan is the failure worth refusing: half a reorder is an arrangement nobody asked for, and it would
	// arrive inside a macro claiming to be one undo step. Every one of these can reach apply_drop from a tree that was
	// rebuilt while a drag was in flight.

	QVERIFY ( plan_tree_drop ( 4, {},        1 ).isEmpty () );   // Nothing dragged.
	QVERIFY ( plan_tree_drop ( 0, { 0 },     0 ).isEmpty () );   // No siblings.
	QVERIFY ( plan_tree_drop ( 4, { 4 },     1 ).isEmpty () );   // Dragged index past the end.
	QVERIFY ( plan_tree_drop ( 4, { -1 },    1 ).isEmpty () );   // Dragged index below zero.
	QVERIFY ( plan_tree_drop ( 4, { 2, 1 },  0 ).isEmpty () );   // Not ascending.
	QVERIFY ( plan_tree_drop ( 4, { 1, 1 },  0 ).isEmpty () );   // Repeated.
	QVERIFY ( plan_tree_drop ( 4, { 1 },     5 ).isEmpty () );   // Drop past one-past-the-end.
	QVERIFY ( plan_tree_drop ( 4, { 1 },    -1 ).isEmpty () );   // Drop below zero.

	QVERIFY ( dropped_run_indices ( 4, { 4 }, 1 ).isEmpty () );
}

//=======================================================================================================================
// The property, over every position
//=======================================================================================================================

void TestTreeDropPlan::every_drop_position_produces_a_replayable_plan ()
{
	// The generalisation of every case above: for a fixed selection, EVERY legal drop position must produce a plan
	// whose replay leaves the dragged children contiguous, in document order, at the reported landing indices, with
	// the others in their original relative order. A case per position would be seven near-copies; the property is
	// one statement and it is the one that would catch an off-by-one at a boundary the examples happened to miss.

	const int        siblingCount = 7;
	const QList<int> dragged      = { 1, 2, 5 };

	for ( int dropIndex = 0; dropIndex <= siblingCount; ++dropIndex )
	{
		const QList<int> landing = dropped_run_indices ( siblingCount, dragged, dropIndex );
		const QList<int> after   = apply_plan ( siblingCount, plan_tree_drop ( siblingCount, dragged, dropIndex ) );

		QCOMPARE ( after.size (), siblingCount );
		QCOMPARE ( landing.size (), dragged.size () );

		// Contiguous, and holding the dragged children in document order.

		for ( int step = 0; step < landing.size (); ++step )
		{
			QCOMPARE ( landing [ step ], landing [ 0 ] + step );
			QCOMPARE ( after [ landing [ step ] ], dragged [ step ] );
		}

		// The children that stayed kept their relative order, which is the half a reorder must not disturb.

		QList<int> stayed;

		for ( const int child : after )
		{
			if ( !dragged.contains ( child ) )
			{
				stayed.append ( child );
			}
		}

		QList<int> sortedStayed = stayed;

		std::sort ( sortedStayed.begin (), sortedStayed.end () );

		QCOMPARE ( stayed, sortedStayed );
	}
}

QTEST_APPLESS_MAIN ( TestTreeDropPlan )

#include "tst_tree_drop_plan.moc"
