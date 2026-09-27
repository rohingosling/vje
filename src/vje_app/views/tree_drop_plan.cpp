//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tree_drop_plan implementation. See the header for what the split buys and why the moves are ordered.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/tree_drop_plan.hpp"

#include <algorithm>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// Is the input a well-formed question? Ascending, distinct, in range, and a drop position the parent has.
		//
		// Checked rather than assumed because the caller derives all of it from a live view -- a selection model and a
		// drop indicator -- and a tree that has just been rebuilt under a drag can hand over indices that no longer
		// name anything.
		//-------------------------------------------------------------------------------------------------------------

		bool input_is_sound ( int siblingCount, const QList<int>& draggedIndices, int dropIndex )
		{
			if ( ( siblingCount <= 0 ) || draggedIndices.isEmpty () )
			{
				return false;
			}

			if ( ( dropIndex < 0 ) || ( dropIndex > siblingCount ) )
			{
				return false;
			}

			int previous = -1;

			for ( const int index : draggedIndices )
			{
				if ( ( index <= previous ) || ( index < 0 ) || ( index >= siblingCount ) )
				{
					return false;
				}

				previous = index;
			}

			return true;
		}

		//-------------------------------------------------------------------------------------------------------------
		// The arrangement the drop asks for, as the original indices in their new order.
		//
		// The rule is one sentence: the children that are NOT being dragged keep their relative order, and the dragged
		// run is inserted among them at the point the drop named. "The point the drop named" has to be translated out
		// of the parent's current coordinates, because the dragged children are about to leave -- so a drop at index
		// 7 with two dragged children below it lands at position 5 once they are gone.
		//-------------------------------------------------------------------------------------------------------------

		QList<int> arrangement_after_drop ( int siblingCount, const QList<int>& draggedIndices, int dropIndex )
		{
			QList<int> stayed;

			stayed.reserve ( siblingCount - draggedIndices.size () );

			for ( int index = 0; index < siblingCount; ++index )
			{
				if ( !draggedIndices.contains ( index ) )
				{
					stayed.append ( index );
				}
			}

			int draggedBelowDrop = 0;

			for ( const int index : draggedIndices )
			{
				if ( index < dropIndex )
				{
					++draggedBelowDrop;
				}
			}

			const int runStart = dropIndex - draggedBelowDrop;

			QList<int> arrangement = stayed.mid ( 0, runStart );

			arrangement.append ( draggedIndices );
			arrangement.append ( stayed.mid ( runStart ) );

			return arrangement;
		}
	}

	//=================================================================================================================
	// TreeDropMove
	//=================================================================================================================

	bool TreeDropMove::operator == ( const TreeDropMove& other ) const
	{
		return ( fromIndex == other.fromIndex ) && ( toIndex == other.toIndex );
	}

	//=================================================================================================================
	// Planning
	//=================================================================================================================

	QList<TreeDropMove> plan_tree_drop ( int siblingCount, const QList<int>& draggedIndices, int dropIndex )
	{
		QList<TreeDropMove> moves;

		if ( !input_is_sound ( siblingCount, draggedIndices, dropIndex ) )
		{
			return moves;
		}

		const QList<int> wanted = arrangement_after_drop ( siblingCount, draggedIndices, dropIndex );

		// Turn "here is the arrangement I want" into "here are the single moves that produce it", by walking the
		// positions in order and bringing whatever belongs at each one up to it. Everything before the position being
		// filled is already correct, so each move only ever reaches FORWARD -- which is what makes the loop terminate
		// and what keeps a move from disturbing a position already settled.
		//
		// The result is emitted rather than counted: a drop that changes nothing settles every position without
		// emitting anything, which is EDIT-10's no-op falling out of the algorithm instead of being special-cased.

		QList<int> current;

		current.reserve ( siblingCount );

		for ( int index = 0; index < siblingCount; ++index )
		{
			current.append ( index );
		}

		for ( int position = 0; position < siblingCount; ++position )
		{
			if ( current [ position ] == wanted [ position ] )
			{
				continue;
			}

			const int from = static_cast<int> ( std::find ( current.begin () + position, current.end (), wanted [ position ] ) - current.begin () );

			moves.append ( TreeDropMove { from, position } );

			current.move ( from, position );
		}

		return moves;
	}

	QList<int> dropped_run_indices ( int siblingCount, const QList<int>& draggedIndices, int dropIndex )
	{
		QList<int> landing;

		if ( !input_is_sound ( siblingCount, draggedIndices, dropIndex ) )
		{
			return landing;
		}

		const QList<int> wanted = arrangement_after_drop ( siblingCount, draggedIndices, dropIndex );

		landing.reserve ( draggedIndices.size () );

		for ( int position = 0; position < wanted.size (); ++position )
		{
			if ( draggedIndices.contains ( wanted [ position ] ) )
			{
				landing.append ( position );
			}
		}

		return landing;
	}
}
