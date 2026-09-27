//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   title_bar_surface -- the colour STYLE-19 asks of a title bar: a step darker than the window below it, on both
//   themes, unless the user has chosen their own.
//
//   A DISTANCE, like every other piece of chrome here (card_surface, dialog_surface, tab_surface): a fixed step below
//   QPalette::Window, so one number serves both themes. Darker on BOTH, which is why it goes through style/tone's
//   darker_tone rather than contrasting_tone.
//
//   WHETHER THE TITLE BAR IS DARK OR LIGHT is not here: Qt derives it from the window's palette, which is VJE's theme
//   (platform/title_bar.hpp says what was measured).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QColor>

#include <optional>

class QPalette;

namespace vje
{
	// The title bar's colour: config::title_bar::DARKER_STEP below the palette's window surface.

	QColor title_bar_colour ( const QPalette& palette );

	// What to ask the platform for: title_bar_colour, or none when the user owns the colour -- Windows' accent colour on
	// title bars, or high contrast -- so the platform's own colour comes back rather than VJE's grey.

	std::optional<QColor> title_bar_caption ( const QPalette& palette, bool userOwnsColour );
}
