//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include "AppConfig.hpp"

#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/TextViewRenderer.hpp>

#include <QColor>

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

	// May an existing object key be renamed (SET-05 / EDIT-02)? Default Yes -- the editor is fully editable unless the
	// user asks otherwise, and a null store is a first run.
	//
	// Governs BOTH routes to a rename, which is the whole point of asking it in one place: the Form View's in-place key
	// column, and the Document > Rename Key command (F2, the toolbar, and the node context menu that the tree and the
	// Form View share). Gating only the first would leave Rename Key sitting in the Form View's own context menu, still
	// renaming, while the cell beside it refused to open.

	bool key_editing_allowed ( const SettingsStore* settings );

	// SET-03a. May an EDIT create a second member with a key its object already carries? Default No, which is VAL-02
	// as it has always stood. Read here rather than at the two consumers so the default is stated once -- the value is
	// PUSHED into UndoController (vje_core must not acquire a store) and read directly by the Code View, which holds
	// one already.

	bool duplicate_keys_allowed ( const SettingsStore* settings );

	// How a string value's characters are shown and typed (SET-03). ONE setting for the Form View and the Text View, so
	// the two tabs cannot disagree about the same node (EDITOR-06) -- which is exactly why it is read here and not at
	// either view.
	//
	// Default Escaped, and note that this differs from TextViewProfile::stringDisplay's own member default of Decoded:
	// that one belongs to vje_core and is the identity, while THIS is the application's preference. Stating the
	// preference at the reader rather than by editing the core struct is the same rule SET-06's Columnar default
	// follows.

	StringDisplay string_display_mode ( const SettingsStore* settings );

	// Does a long value wrap rather than being elided (SET-05 / SET-06)? Two settings, one per view, because the two
	// answer for different amounts of screen: default No for both.

	bool wrap_strings_in_form_view ( const SettingsStore* settings );
	bool wrap_strings_in_text_view ( const SettingsStore* settings );

	// SET-10 / FILE-12: does a printed page draw the hairlines separating its header and footer from the body? A null
	// store is a first run, which is the default -- they are drawn.

	bool print_page_rules ( const SettingsStore* settings );

	// SET-15 / FILE-15: should VJE's entries be in Windows Explorer's menus? Default No -- a fresh installation writes
	// nothing to the registry until the user asks. Asked by ExplorerIntegrationSync, which keeps the registry in step;
	// on a platform without the feature the answer is read by nobody, since the dialog never offers the row.

	bool explorer_integration_enabled ( const SettingsStore* settings );

	// SET-12 / STYLE-05: the application's corner-radius discipline -- Fluent rounds both the panes and the controls,
	// Classic squares both and leaves every control the base style's own shape. Both are defensible and the preference
	// splits, so it is the user's. A null store, an absent key or an unrecognized spelling all yield
	// config::appearance::DEFAULT_INTERFACE_STYLE.
	//
	// TOTAL AND SIDE-EFFECT-FREE, deliberately: it answers correctly whether or not migrate_interface_style below has
	// run, which is what lets the migration be a one-time housekeeping call rather than a precondition of drawing.

	config::appearance::InterfaceStyle interface_style ( const SettingsStore* settings );

	// SET-14 / TREE-10: which rows carry the unsaved-change dot -- every changed node and its ancestors, or the file node
	// alone. A null store, an absent key or an unrecognized spelling all yield config::tree::DEFAULT_CHANGE_MARK_SCOPE.
	// Pushed into the tree model by the window, never read at paint time (NFR-03).

	config::tree::ChangeMarkScope change_mark_scope ( const SettingsStore* settings );

	// SET-14a: the dot's colour as stored -- the DARK theme's colour. A null store, an absent key or a value that is not
	// a hexadecimal colour all yield config::tree::DEFAULT_CHANGE_MARK_COLOUR_DARK.

	QColor stored_change_mark_colour ( const SettingsStore* settings );

	// ... and as it shows in the theme in effect: itself in Dark, its HSL lightness inverted in Light (theme_colour).
	// Pushed into the tree by the window at start-up, on a change of the setting, and on every theme change.

	QColor change_mark_colour ( const SettingsStore* settings, bool darkTheme );

	// The one-time move off SET-03's superseded "Rounded pane corners" key -- Yes to Fluent, No to Classic, absent to
	// the default. Called once from the composition root; idempotent, so calling it again costs a lookup and does
	// nothing. This is the ONLY thing that writes or removes that key.

	void migrate_interface_style ( SettingsStore* settings );

	// Which of the two committed icon trees IconLibrary reads (the Debug group). A null store, an absent key, or an
	// unrecognized spelling all yield config::icons::DEFAULT_ICON_SOURCE -- which is the same tolerance every reader
	// above has, and matters more here than most: the setting is developer tooling that a release build does not
	// present, so a settings file carrying a stale value must not decide what a user's icons come from.

	// Drop settings this build no longer has a reader for, so they do not sit unread in every existing settings file.
	// Called once from the composition root, beside migrate_interface_style.

	void migrate_retired_settings ( SettingsStore* settings );
}
