//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   title_bar_surface implementation. See the header.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/title_bar_surface.hpp"

#include "AppConfig.hpp"
#include "style/tone.hpp"

#include <QPalette>

namespace vje
{
	QColor title_bar_colour ( const QPalette& palette )
	{
		// Window, because the title bar sits directly above what is drawn on it: the menu bar, and a dialog's content.

		return darker_tone ( palette.color ( QPalette::Window ), config::title_bar::DARKER_STEP );
	}

	std::optional<QColor> title_bar_caption ( const QPalette& palette, bool userOwnsColour )
	{
		if ( userOwnsColour )
		{
			return std::nullopt;
		}

		return title_bar_colour ( palette );
	}
}
