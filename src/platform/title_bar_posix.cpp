//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   title_bar, POSIX -- the window manager draws the title bar and offers an application nothing to set (STYLE-19,
//   LD-9), so nothing is applied and nothing is owned.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <platform/title_bar.hpp>

namespace vje::platform
{
	bool apply_title_bar_colour ( quintptr nativeWindow, std::optional<std::uint32_t> captionRgb )
	{
		Q_UNUSED ( nativeWindow )
		Q_UNUSED ( captionRgb )

		return false;
	}

	bool user_owns_title_bar_colour ()
	{
		return false;
	}
}
