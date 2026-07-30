//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   card_surface implementation. See card_surface.hpp for the rule and why the border is a distance rather than a
//   colour.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/card_surface.hpp"

#include "AppConfig.hpp"
#include "style/tone.hpp"

#include <QPalette>

namespace vje
{
	QColor card_surface ( const QPalette& palette )
	{
		// Base, not a derived tone: this IS the surface the tree and the grids already fill themselves with, and a
		// second opinion about it would show as a seam wherever the content did not cover the card exactly.
		//
		// Normalized to RGB for the reason tab_surface states: QColor::operator== compares the colour SPEC as well as
		// the value, so an Hsl-spec colour and the Rgb-spec pixel it paints compare unequal while printing the same
		// hex. Anything comparing this against a rendered pixel would fail with "#ffffff != #ffffff" (lesson Q19).

		return palette.color ( QPalette::Base ).toRgb ();
	}

	QColor card_border ( const QPalette& palette )
	{
		// Window, not Base: the border's job is to close the card against the BACKDROP framing the pair of cards
		// (STYLE-03's margin), which is the edge it has to be visible across. Taking the distance from the content
		// instead would tune it against a surface it never touches.

		const QColor backdrop = palette.color ( QPalette::Window );

		return contrasting_tone ( backdrop, config::card::BORDER_CONTRAST ).toRgb ();
	}
}
