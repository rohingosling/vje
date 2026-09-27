//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   title_bar -- the title bar's colour, the one thing about it VJE sets here (STYLE-19, LD-9). The window manager
//   still draws the title bar; VJE draws none.
//
//   WHETHER IT IS DARK OR LIGHT IS NOT SET HERE, though Windows would allow it: Qt derives each window's dark flag from
//   the application's PALETTE, not from the OS, and re-derives it on every palette change -- measured (Qt 6.10.1): with
//   the OS dark and VJE's light palette applied, a new window's flag is light; and a flag set here was put back at the
//   next palette change. VJE's palette is its theme, so the flag is already VJE's, and it has one owner.
//
//   WINDOWS. The colour is a per-window DWM attribute, which Windows 11 accepts and Windows 10 refuses -- so "Windows 10
//   keeps its own colour" needs no version check: the call does not take, and the result says so. Measured on Windows
//   11 build 26200 (Qt 6.10.1): a set colour applies to the active and inactive window alike and survives a hide and
//   re-show; it cannot be READ back (DwmGetWindowAttribute answers E_INVALIDARG).
//
//   LINUX. The window manager draws the title bar and exposes no such setting, so nothing is applied and nothing is
//   owned by the user.
//
//   WHAT COLOUR TO ASK FOR IS NOT DECIDED HERE: that is vje_app's title_bar_caption, pure and tested headlessly.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QtGlobal>

#include <cstdint>
#include <optional>

namespace vje::platform
{
	// Colours one native window's title bar (a QWidget's winId ()), and answers whether the platform took it. captionRgb
	// is 0xRRGGBB, or none to hand the colour back to the platform -- which is how a window VJE coloured returns to the
	// user's accent when they turn it on.

	bool apply_title_bar_colour ( quintptr nativeWindow, std::optional<std::uint32_t> captionRgb );

	// True when the user has chosen their own title-bar colour, which STYLE-19 leaves alone: Windows' "Show accent colour
	// on title bars and window borders", or high contrast. Read afresh on every call rather than cached, so a change
	// reaches the next window shown.

	bool user_owns_title_bar_colour ();
}
