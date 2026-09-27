//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   dialog_surface implementation. See the header for why every colour here is a distance rather than a value.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/dialog_surface.hpp"

#include "AppConfig.hpp"
#include "style/tone.hpp"

#include <QPalette>

namespace vje
{
	QColor dialog_rule ( const QPalette& palette )
	{
		// Window, because that is what a dialog is drawn on -- the same surface the rule has to be visible against.

		return contrasting_tone ( palette.color ( QPalette::Window ), config::dialog::RULE_CONTRAST );
	}

	QColor dialog_group_frame ( const QPalette& palette )
	{
		return contrasting_tone ( palette.color ( QPalette::Window ), config::dialog::GROUP_FRAME_CONTRAST );
	}

	QColor dialog_dimmed_prose ( const QPalette& palette )
	{
		return contrasting_tone ( palette.color ( QPalette::Window ), config::dialog::DIMMED_PROSE_CONTRAST );
	}
}
