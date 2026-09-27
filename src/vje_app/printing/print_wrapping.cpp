//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   print_wrapping implementation. See the header for why this hangs from the line's own indentation where
//   TextViewRenderer hangs from the value column.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "printing/print_wrapping.hpp"

#include <QChar>
#include <QLatin1Char>
#include <QStringList>

namespace vje
{
	namespace
	{
		// Below this many characters a wrap produces a column of syllables rather than a paragraph, so a line whose own
		// indent has eaten the width is left long instead. The same number, and the same reasoning, as
		// TextViewRenderer's own minimum -- deliberately restated rather than shared, because that one is a property of
		// vje_core's renderer and this one is a property of the printed page.

		constexpr int MINIMUM_AVAILABLE = 8;

		int leading_whitespace_width ( const QString& line )
		{
			int width = 0;

			while ( ( width < line.size () ) && line.at ( width ).isSpace () )
			{
				++width;
			}

			return width;
		}

		// A line that ends in whitespace survives into the print as visible nothing, and an emitted line that turns out
		// to be pure indent reads as a blank where the reader expects text -- the same discipline TextViewRenderer
		// keeps at its own break points.

		QString right_trimmed ( QString line )
		{
			while ( !line.isEmpty () && line.endsWith ( QLatin1Char ( ' ' ) ) )
			{
				line.chop ( 1 );
			}

			return line;
		}
	}

	QString wrap_preformatted ( const QString& text, int columns )
	{
		if ( columns <= 0 )
		{
			return text;
		}

		QStringList wrapped;

		for ( const QString& line : text.split ( QLatin1Char ( '\n' ) ) )
		{
			// The line's own indent is what its continuations hang from, so it is also what decides whether there is
			// room left to wrap into at all.

			const int indent    = leading_whitespace_width ( line );
			const int available = columns - indent;

			if ( ( line.size () <= columns ) || ( available < MINIMUM_AVAILABLE ) )
			{
				wrapped << line;

				continue;
			}

			const QString continuation = line.left ( indent );

			int start = 0;

			while ( start < line.size () )
			{
				const bool isFirstLine = ( start == 0 );

				// The first emitted line IS the original text and already carries the indent; every one after it
				// carries a copy of it, and so has that much less width for the text itself.

				const QString prefix     = isFirstLine ? QString () : continuation;
				const int     lineBudget = isFirstLine ? columns : available;

				if ( ( line.size () - start ) <= lineBudget )
				{
					wrapped << right_trimmed ( prefix + line.mid ( start ) );

					break;
				}

				// Prefer the last space that still fits. Searching backwards from the break point is what makes a
				// broken word the exception rather than the rule. On the FIRST line the search must not reach into the
				// leading whitespace, or the break lands inside the indent and emits a line of nothing.

				const int minimumBreak = isFirstLine ? indent : start;

				int breakAt = line.lastIndexOf ( QLatin1Char ( ' ' ), start + lineBudget );

				if ( breakAt <= minimumBreak )
				{
					// No usable space -- a single token wider than the line. Broken at the margin, because the
					// alternative is a line that runs off the paper and takes the reader's place with it.

					breakAt = start + lineBudget;

					wrapped << right_trimmed ( prefix + line.mid ( start, lineBudget ) );
				}
				else
				{
					wrapped << right_trimmed ( prefix + line.mid ( start, breakAt - start ) );

					++breakAt;                                     // Consume the space the break was taken at.
				}

				start = breakAt;
			}
		}

		return wrapped.join ( QLatin1Char ( '\n' ) );
	}
}
