//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   code_folding implementation. See the header for why a fold is derived from the text index rather than from a parse
//   of its own, and why it is named by pointer.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/code_folding.hpp"

#include <QHash>

#include <algorithm>

namespace vje
{
	QList<FoldRegion> fold_regions ( const PointerSpanIndex& index )
	{
		// Keyed by marker line so the one-marker-per-line rule is a comparison at insert time rather than a second pass.

		QHash<int, FoldRegion> byOpenLine;

		for ( auto entry = index.constBegin (); entry != index.constEnd (); ++entry )
		{
			const LineSpan& span = entry.value ();

			// Nothing strictly between the brackets: a scalar, an inline container, or one the scan did not finish.

			if ( ( span.lastLine - span.openLine ) < 2 )
			{
				continue;
			}

			const FoldRegion candidate { entry.key (), span.openLine, span.lastLine };

			auto existing = byOpenLine.find ( span.openLine );

			if ( existing == byOpenLine.end () )
			{
				byOpenLine.insert ( span.openLine, candidate );

				continue;
			}

			// The outer region keeps the line: the one that closes later, or -- where both brackets share lines too, as in
			// "[ {" ... "} ]" -- the shorter pointer, which is the ancestor.

			const bool outer = ( candidate.closeLine > existing->closeLine )
			                || ( ( candidate.closeLine == existing->closeLine ) && ( candidate.pointer.size () < existing->pointer.size () ) );

			if ( outer )
			{
				*existing = candidate;
			}
		}

		QList<FoldRegion> regions = byOpenLine.values ();

		std::sort
		(
			regions.begin (),
			regions.end (),
			[] ( const FoldRegion& left, const FoldRegion& right ) { return left.openLine < right.openLine; }
		);

		return regions;
	}

	QList<bool> visible_lines ( int lineCount, const QList<FoldRegion>& regions, const QSet<QString>& folded )
	{
		// A difference array: each fold adds one to the depth over its interior, and a line is visible where the depth
		// is zero. Linear in lines plus regions, whatever the nesting -- this runs over the whole document on every
		// toggle, so a per-region walk over its interior would be quadratic on a deeply folded file.

		QList<int> depthChange ( std::max ( 0, lineCount ) + 2, 0 );

		for ( const FoldRegion& region : regions )
		{
			if ( !folded.contains ( region.pointer ) )
			{
				continue;
			}

			const int first = region.openLine  + 1;
			const int last  = region.closeLine - 1;

			if ( ( first > last ) || ( first > lineCount ) || ( last < 1 ) )
			{
				continue;
			}

			depthChange [ std::max ( first, 1 ) ]             += 1;
			depthChange [ std::min ( last, lineCount ) + 1 ]  -= 1;
		}

		QList<bool> visible;

		visible.reserve ( std::max ( 0, lineCount ) );

		int depth = 0;

		for ( int line = 1; line <= lineCount; ++line )
		{
			depth += depthChange [ line ];

			visible.append ( depth == 0 );
		}

		return visible;
	}

	QStringList folds_hiding_line ( const QList<FoldRegion>& regions, const QSet<QString>& folded, int line )
	{
		// regions is in openLine order, and of two nested regions the outer opens first -- so collecting in order gives
		// outermost first without a sort.

		QStringList hiding;

		for ( const FoldRegion& region : regions )
		{
			if ( ( line > region.openLine ) && ( line < region.closeLine ) && folded.contains ( region.pointer ) )
			{
				hiding.append ( region.pointer );
			}
		}

		return hiding;
	}

	int innermost_region_at_line ( const QList<FoldRegion>& regions, const QSet<QString>& folded, int line, bool wantFolded )
	{
		int best      = -1;
		int bestWidth = 0;

		for ( int index = 0; index < regions.size (); ++index )
		{
			const FoldRegion& region = regions [ index ];

			if ( ( line < region.openLine ) || ( line > region.closeLine ) )
			{
				continue;
			}

			if ( folded.contains ( region.pointer ) != wantFolded )
			{
				continue;
			}

			const int width = region.closeLine - region.openLine;

			if ( ( best < 0 ) || ( width < bestWidth ) )
			{
				best      = index;
				bestWidth = width;
			}
		}

		return best;
	}
}
