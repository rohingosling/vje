//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   theme_colour -- a user's colour carried between the two themes by inverting its HSL lightness (SET-14a), and the CSS
//   hexadecimal form the Settings dialog speaks it in. Pure, and headlessly tested.
//
//   THE INVERSION IS DONE IN WHOLE UNITS, AND THAT IS WHAT MAKES IT EXACT. HSL lightness is the mean of a colour's
//   largest and smallest channel, and hue and saturation depend only on the channels' distances from each other and on
//   the chroma, which L -> 1 - L leaves alone. So inverting the lightness moves every channel by the SAME amount,
//   255 - max - min -- a shift in whole units, with no conversion to HSL and back and so no rounding. The shifted
//   colour's max and min are 255 - min and 255 - max, so a second inversion shifts by exactly the negative of the
//   first: switching the theme twice returns the colour the user chose, to the unit, however many times it is done.
//   A round trip through QColor's HSL accessors would lose a unit here and there, and the colour would creep.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QColor>
#include <QString>

#include <optional>

namespace vje
{
	// The colour with its HSL lightness inverted (L becomes 1 - L), hue and saturation kept. Its own inverse.

	QColor inverted_lightness ( const QColor& colour );

	// A colour stated for the Dark theme, as it shows in the theme that is in effect: itself in Dark, its lightness
	// inverted in Light. Also the way back -- a colour stated for the theme in effect, as the Dark theme's -- since the
	// inversion undoes itself.

	QColor colour_for_theme ( const QColor& darkColour, bool dark );

	// A CSS hexadecimal colour as a user types it: "#RRGGBB" or "#RGB", the '#' optional, either case, surrounding space
	// ignored. Nothing for anything else, an incomplete value included.

	std::optional<QColor> parse_hex_colour ( const QString& text );

	// The "#RRGGBB" form, in upper case -- the one form VJE writes, to the text box and to the store.

	QString hex_colour ( const QColor& colour );
}
