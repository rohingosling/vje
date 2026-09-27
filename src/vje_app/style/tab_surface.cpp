//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tab_surface / band_surface implementation. See the header for the rule and for why Fusion's own tab shading could
//   not be steered into it by any palette value.
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
	namespace
	{
		// Which side of the scale the content sits on -- the same question the tone rule answers for its direction,
		// asked here to pick the theme's value set (the two strips are tuned separately, AppConfig.hpp).

		bool content_is_dark ( const QColor& content )
		{
			return content.lightness () < 128;
		}
	}

	int tab_surface_contrast ( bool contentIsDark, bool selected, bool paneFocused )
	{
		// The unselected tabs are a STATIC FIELD: focus does not move them (the 2026-07-24 rule).

		if ( !selected )
		{
			return contentIsDark ? config::editor::DARK_TAB_UNSELECTED_CONTRAST
			                     : config::editor::LIGHT_TAB_UNSELECTED_CONTRAST;
		}

		if ( paneFocused )
		{
			return contentIsDark ? config::editor::DARK_TAB_SELECTED_FOCUSED_CONTRAST
			                     : config::editor::LIGHT_TAB_SELECTED_FOCUSED_CONTRAST;
		}

		return contentIsDark ? config::editor::DARK_TAB_SELECTED_UNFOCUSED_CONTRAST
		                     : config::editor::LIGHT_TAB_SELECTED_UNFOCUSED_CONTRAST;
	}

	int band_surface_contrast ( bool paneFocused )
	{
		return paneFocused ? config::editor::BAND_FOCUSED_CONTRAST : config::editor::BAND_UNFOCUSED_CONTRAST;
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

		const QColor content = palette.color ( QPalette::Base );

		return contrasting_tone ( content, tab_surface_contrast ( content_is_dark ( content ), selected, paneFocused ) ).toRgb ();
	}

	QColor band_surface ( const QPalette& palette, bool paneFocused )
	{
		const QColor content = palette.color ( QPalette::Base );

		return contrasting_tone ( content, band_surface_contrast ( paneFocused ) ).toRgb ();
	}
}
