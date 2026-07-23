//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   tab_surface implementation. See the header for why Fusion's own tab shading could not be steered into this by any
//   palette value.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/tab_surface.hpp"

#include "AppConfig.hpp"
#include "style/tone.hpp"

#include <QPalette>

namespace vje
{
	int tab_surface_contrast ( bool selected, bool paneFocused )
	{
		if ( paneFocused )
		{
			return selected ? config::editor::TAB_SELECTED_FOCUSED_CONTRAST
			                : config::editor::TAB_UNSELECTED_FOCUSED_CONTRAST;
		}

		return selected ? config::editor::TAB_SELECTED_UNFOCUSED_CONTRAST
		                : config::editor::TAB_UNSELECTED_UNFOCUSED_CONTRAST;
	}

	QColor tab_surface ( const QPalette& palette, bool selected, bool paneFocused )
	{
		// Base, not Window: the reference is the CONTENT the chrome sits beside -- the grid, the code text, the tree --
		// which is what the user is comparing the strip against. Window is the frame around the pair of cards and would
		// answer a different question.
		//
		// NORMALIZED TO RGB, which is not cosmetic. contrasting_tone builds its result with QColor::fromHsl, and
		// QColor::operator== compares the SPEC as well as the value -- so an Hsl-spec colour and the Rgb-spec pixel it
		// paints compare as different while printing the identical hex string. Anything comparing this against a
		// rendered pixel would fail with "#dedede != #dedede", which is a genuinely confusing hour.

		return contrasting_tone ( palette.color ( QPalette::Base ), tab_surface_contrast ( selected, paneFocused ) ).toRgb ();
	}
}
