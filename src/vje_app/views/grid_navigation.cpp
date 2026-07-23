//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   grid_navigation implementation. See the header for the landability rule and the edge behaviour it encodes.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/grid_navigation.hpp"

namespace vje
{
	//=================================================================================================================
	// GridPosition
	//=================================================================================================================

	bool GridPosition::is_valid () const
	{
		return ( row >= 0 ) && ( column >= 0 );
	}

	bool GridPosition::operator== ( const GridPosition& other ) const
	{
		return ( row == other.row ) && ( column == other.column );
	}

	bool GridPosition::operator!= ( const GridPosition& other ) const
	{
		return !( *this == other );
	}

	//=================================================================================================================
	// Movement
	//=================================================================================================================

	GridPosition next_position ( GridPosition current, GridMove move, int rowCount, int columnCount )
	{
		// An empty grid has nowhere to be, and a position outside the grid is a caller bug rather than an edge case --
		// both answer "no current cell" so a controller never acts on a coordinate it cannot address.

		const bool gridIsEmpty = ( rowCount <= 0 ) || ( columnCount <= 0 );

		if ( gridIsEmpty )
		{
			return GridPosition {};
		}

		const bool currentIsOutside = !current.is_valid ()      ||
		                              ( current.row    >= rowCount ) ||
		                              ( current.column >= columnCount );

		if ( currentIsOutside )
		{
			return GridPosition {};
		}

		GridPosition next = current;

		switch ( move )
		{
			// -- Arrows: clamp at every edge. The highlight staying put IS the specified behaviour (EDITOR-03), not a
			//            fallback -- it is what makes the bottom edge a detectable event for EDITOR-12's row growth.

			case GridMove::Up:
			{
				next.row = ( current.row > 0 ) ? ( current.row - 1 ) : current.row;

				break;
			}

			case GridMove::Down:
			{
				next.row = ( current.row < ( rowCount - 1 ) ) ? ( current.row + 1 ) : current.row;

				break;
			}

			case GridMove::Left:
			{
				next.column = ( current.column > 0 ) ? ( current.column - 1 ) : current.column;

				break;
			}

			case GridMove::Right:
			{
				next.column = ( current.column < ( columnCount - 1 ) ) ? ( current.column + 1 ) : current.column;

				break;
			}

			// -- Tab: reading order across the whole grid, wrapping at the row boundary and clamping at the two ends.

			case GridMove::NextCell:
			{
				const bool atRowEnd = ( current.column == ( columnCount - 1 ) );

				if ( !atRowEnd )
				{
					next.column = current.column + 1;
				}
				else if ( current.row < ( rowCount - 1 ) )
				{
					next.row    = current.row + 1;
					next.column = 0;
				}

				break;
			}

			case GridMove::PreviousCell:
			{
				const bool atRowStart = ( current.column == 0 );

				if ( !atRowStart )
				{
					next.column = current.column - 1;
				}
				else if ( current.row > 0 )
				{
					next.row    = current.row - 1;
					next.column = columnCount - 1;
				}

				break;
			}

			// -- Absolute moves.

			case GridMove::RowStart:
			{
				next.column = 0;

				break;
			}

			case GridMove::RowEnd:
			{
				next.column = columnCount - 1;

				break;
			}

			case GridMove::GridStart:
			{
				next.row    = 0;
				next.column = 0;

				break;
			}

			case GridMove::GridEnd:
			{
				next.row    = rowCount - 1;
				next.column = columnCount - 1;

				break;
			}
		}

		return next;
	}

	bool is_edge_blocked ( GridPosition current, GridMove move, int rowCount, int columnCount )
	{
		const GridPosition next = next_position ( current, move, rowCount, columnCount );

		return next.is_valid () && ( next == current );
	}
}
