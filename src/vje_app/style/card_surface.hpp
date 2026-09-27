//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   card_surface -- the two colours a workspace card is drawn from (STYLE-01/02): the opaque surface the card's content
//   is read on, and the one-pixel border that closes it against the window backdrop.
//
//   THEY ARE FUNCTIONS OF THE PALETTE, NOT A PAIR PER THEME, for the reason tone.hpp exists: a border chosen by eye on
//   one theme is either invisible or shouting on the other. The border is stated as a DISTANCE from the backdrop it
//   separates the card from, applied away from whichever end of the lightness scale that backdrop is nearer, so one
//   number covers Light and Dark and cannot be silently inert on either.
//
//   THE SURFACE IS QPalette::Base RATHER THAN A DERIVED TONE, and that is deliberate. STYLE-01 asks that dense content
//   be read on a surface rather than on the bare window backdrop, and Base IS that surface -- it is what the tree view
//   and both grids already fill themselves with. Deriving a card colour beside it would put a second opinion about the
//   content surface into the application, visible as a seam wherever the content did not cover the card exactly.
//
//   Kept apart from the widget that paints them (views/Card) so the rule can be checked without a window, which is the
//   same split tab_surface and ShadedTabBar already stand in.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QColor>

class QPalette;

namespace vje
{
	// The opaque fill a card's content sits on (STYLE-01).

	QColor card_surface ( const QPalette& palette );

	// The one-pixel border closing the card against the window backdrop (STYLE-02). Derived from the BACKDROP rather
	// than from the surface, because that is the edge it has to be visible across.

	QColor card_border ( const QPalette& palette );
}
