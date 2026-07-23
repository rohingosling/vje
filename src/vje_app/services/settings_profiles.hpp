//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   settings_profiles -- the ONE place a stored settings value becomes a vje_core rendering profile.
//
//   WHY THIS EXISTS AT ALL. SET-07's four format settings are not four independent preferences; together they are the
//   DOCUMENT FORMAT PROFILE, and FILE-03 / EDITOR-07 make a byte-for-byte claim about it: "what the Code View shows is
//   what File > Save writes". Two call sites each reading the same four keys would keep that claim by coincidence --
//   the day one of them gained a fifth key, or clamped indentSize differently, the two would part company silently and
//   the only symptom would be a diff nobody expected after a save. There is therefore exactly one reader, and both the
//   Code View and the save path take the profile FROM it.
//
//   The Text View profile (SET-06) is here for the same reason in miniature: the Text View reads it, and the Settings
//   dialog (Phase 10) will write the same keys, so the spelling of every stored value lives in settings_values rather
//   than at a read site.
//
//   A null store yields the documented defaults, which is what lets a view be constructed without settings in a test.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/TextViewRenderer.hpp>

namespace vje
{
	class SettingsStore;

	// The document format profile (SET-07 / FILE-03): the Code View's displayed text AND File > Save output.

	FormatProfile document_format_profile ( const SettingsStore* settings );

	// The Text View rendering options (SET-06 / EDITOR-06).

	TextViewProfile text_view_profile ( const SettingsStore* settings );

	// Is a single click in the tree the given view's activation gesture (EDITOR-04)? editOnKey is settings_keys::
	// FORM_EDIT_ON or CODE_EDIT_ON; both default to Double click, so passive tree navigation never takes the keyboard.

	bool hands_over_caret_on_click ( const SettingsStore* settings, const QString& editOnKey );
}
