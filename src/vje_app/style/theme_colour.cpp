//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   theme_colour implementation. See theme_colour.hpp for why the inversion is a shift rather than an HSL round trip.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/theme_colour.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace vje
{
	QColor inverted_lightness ( const QColor& colour )
	{
		const QColor rgb = colour.toRgb ();

		const int red   = rgb.red ();
		const int green = rgb.green ();
		const int blue  = rgb.blue ();

		const int largest  = std::max ( { red, green, blue } );
		const int smallest = std::min ( { red, green, blue } );

		// L = ( max + min ) / 510, so 1 - L is reached by moving every channel by 255 - max - min. The new largest is
		// 255 - min and the new smallest 255 - max, both within range, and every channel lies between them.

		const int shift = 255 - largest - smallest;

		return QColor ( red + shift, green + shift, blue + shift, rgb.alpha () );
	}

	QColor colour_for_theme ( const QColor& darkColour, bool dark )
	{
		return dark ? darkColour.toRgb () : inverted_lightness ( darkColour );
	}

	std::optional<QColor> parse_hex_colour ( const QString& text )
	{
		static const QRegularExpression pattern ( QStringLiteral ( "^#?([0-9A-Fa-f]{3}|[0-9A-Fa-f]{6})$" ) );

		const QRegularExpressionMatch match = pattern.match ( text.trimmed () );

		if ( !match.hasMatch () )
		{
			return std::nullopt;
		}

		QString digits = match.captured ( 1 );

		// CSS's short form doubles each digit: #ABC is #AABBCC.

		if ( digits.size () == 3 )
		{
			const QString shortForm = digits;

			digits.clear ();

			for ( const QChar digit : shortForm )
			{
				digits += digit;
				digits += digit;
			}
		}

		return QColor ( QLatin1Char ( '#' ) + digits );
	}

	QString hex_colour ( const QColor& colour )
	{
		return colour.toRgb ().name ( QColor::HexRgb ).toUpper ();
	}
}
