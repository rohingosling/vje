//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tooltip_text implementation. See tooltip_text.hpp for why a tooltip needs a formatter at all.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/tooltip_text.hpp"

#include "AppConfig.hpp"

#include <QStringList>
#include <QTextDocument>

namespace vje
{
	QString tooltip_text ( const QString& text )
	{
		if ( text.isEmpty () )
		{
			return QString ();
		}

		const int lineLength   = config::tooltip::LINE_LENGTH;
		const int maximumLines = config::tooltip::MAXIMUM_LINES;

		// Bound the work before doing any of it. A wrapped line consumes at most lineLength + 1 characters of the input
		// (the one is the space a break drops), so nothing past this many can reach the screen -- and a tooltip is asked
		// for on a value that may be megabytes long.

		const int  readLimit      = maximumLines * ( lineLength + 1 );
		const bool inputTruncated = ( text.size () > readLimit );

		QString source = text.left ( readLimit );

		source.remove ( QLatin1Char ( '\r' ) );

		// Wrap each line the text already has, at the last space that fits, or -- in a run with no space, a base64 blob
		// or a long path -- at the measure itself, since a run the width of the screen is the thing being prevented.

		QStringList lines;

		for ( const QString& paragraph : source.split ( QLatin1Char ( '\n' ) ) )
		{
			QString remaining = paragraph;

			while ( ( remaining.size () > lineLength ) && ( lines.size () <= maximumLines ) )
			{
				const int breakAt = remaining.lastIndexOf ( QLatin1Char ( ' ' ), lineLength );

				if ( breakAt > 0 )
				{
					lines.append ( remaining.left ( breakAt ) );

					remaining = remaining.mid ( breakAt + 1 );
				}
				else
				{
					lines.append ( remaining.left ( lineLength ) );

					remaining = remaining.mid ( lineLength );
				}
			}

			lines.append ( remaining );

			if ( lines.size () > maximumLines )
			{
				break;
			}
		}

		// Capped, and SAID to be capped: the last line kept ends in an ellipsis, inside the measure.

		const bool truncated = inputTruncated || ( lines.size () > maximumLines );

		if ( lines.size () > maximumLines )
		{
			lines = lines.mid ( 0, maximumLines );
		}

		if ( truncated )
		{
			QString& last = lines.last ();

			if ( last.size () >= lineLength )
			{
				last.truncate ( lineLength - 1 );
			}

			last.append ( QChar ( 0x2026 ) );
		}

		const QString wrapped = lines.join ( QLatin1Char ( '\n' ) );

		// Qt decides between plain and rich text by sniffing, with this same function. Text it would take for markup is
		// escaped into rich text that SHOWS the markup, preformatted so the breaks above survive. Everything else stays
		// plain, which Qt shows exactly as given.

		if ( !Qt::mightBeRichText ( wrapped ) )
		{
			return wrapped;
		}

		return QStringLiteral ( "<p style=\"white-space:pre\">" ) + wrapped.toHtmlEscaped () + QStringLiteral ( "</p>" );
	}
}
