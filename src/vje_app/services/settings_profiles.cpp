//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   settings_profiles implementation. See the header for why there is exactly one reader of the format keys.
//
//   Every read is TOLERANT in the same way SettingsStore is: an unrecognized stored string falls back to the documented
//   default rather than to an arbitrary enumerator. A hand-edited settings file is a supported input (the file is
//   deliberately human-readable), so "codeView.braceStyle": "allman" -- wrong case, plausibly typed -- must not decide
//   the format of every file the user subsequently saves.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/settings_profiles.hpp"

#include "AppConfig.hpp"
#include "style/theme_colour.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <algorithm>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// SET-07's stated bounds for the indent size. Clamped rather than rejected: a stored 0 is meaningless but is
		// still a request for "as little as possible", and the formatter would otherwise emit no indentation at all.
		//-------------------------------------------------------------------------------------------------------------

		constexpr int MINIMUM_INDENT_SIZE = 1;
		constexpr int MAXIMUM_INDENT_SIZE = 8;

		// SET-06's blank-line range. Stated here as well as in the schema, and clamped rather than trusted: the schema
		// bounds what the DIALOG can produce, and a hand-edited settings file is not bound by anything.

		constexpr int MINIMUM_BLANK_LINES = 0;
		constexpr int MAXIMUM_BLANK_LINES = 10;

		//-------------------------------------------------------------------------------------------------------------
		// SET-12's migration mapping, in one place because both the reader and the migration need it and they must not
		// answer differently: the reader consults it every time the new key is absent, and the migration consults it
		// once to write the new key.
		//
		// The three cases are distinct and all three matter. A stored Yes maps to Fluent and a stored No to Classic --
		// the two the migration exists for. An ABSENT legacy key maps to the DEFAULT rather than to either, because a
		// user who never opened that setting expressed no preference and must land where a fresh install lands.
		//-------------------------------------------------------------------------------------------------------------

		config::appearance::InterfaceStyle legacy_interface_style ( const SettingsStore* settings )
		{
			if ( ( settings == nullptr ) || !settings->contains ( settings_keys::LEGACY_ROUNDED_PANE_CORNERS ) )
			{
				return config::appearance::DEFAULT_INTERFACE_STYLE;
			}

			// The fallback here is UNREACHABLE -- contains() has just been established -- and it is deliberately the
			// value that is NOT the default. Two reasons, and the second was found by a neutered build. It cannot
			// restate the default, or changing DEFAULT_INTERFACE_STYLE would leave this line quietly disagreeing with
			// it. And because it disagrees with the default, deleting the contains() guard above CHANGES THE ANSWER
			// for an absent key, so the case that guards lesson D8 fails -- where a fallback of true coincided with
			// the default and the guard could be removed with every test still green.

			return settings->value_bool ( settings_keys::LEGACY_ROUNDED_PANE_CORNERS, false )
				? config::appearance::InterfaceStyle::Fluent
				: config::appearance::InterfaceStyle::Classic;
		}
	}

	FormatProfile document_format_profile ( const SettingsStore* settings )
	{
		FormatProfile profile;   // Constructed at the SET-07 defaults: 2 spaces, Allman, separators not aligned.

		if ( settings == nullptr )
		{
			return profile;
		}

		const QString indentKind = settings->value_string
		(
			settings_keys::CODE_INDENT_KIND,
			settings_values::INDENT_SPACES
		);

		profile.indent = ( indentKind == settings_values::INDENT_TABS ) ? IndentKind::Tabs : IndentKind::Spaces;

		profile.indentSize = std::clamp
		(
			settings->value_int ( settings_keys::CODE_INDENT_SIZE, profile.indentSize ),
			MINIMUM_INDENT_SIZE,
			MAXIMUM_INDENT_SIZE
		);

		const QString braceStyle = settings->value_string
		(
			settings_keys::CODE_BRACE_STYLE,
			settings_values::BRACE_STYLE_ALLMAN
		);

		profile.braceStyle = ( braceStyle == settings_values::BRACE_STYLE_K_AND_R ) ? BraceStyle::KAndR
		                                                                           : BraceStyle::Allman;

		profile.alignNameSeparators = settings->value_bool
		(
			settings_keys::CODE_ALIGN_SEPARATORS,
			profile.alignNameSeparators
		);

		return profile;
	}

	TextViewProfile text_view_profile ( const SettingsStore* settings )
	{
		TextViewProfile profile;   // Constructed at the SET-06 defaults: aligned, ":", both container rows, Compact.

		// SET-03 first, and BEFORE the null-store return -- string_display_mode answers Escaped for a null store, while
		// TextViewProfile's own member default is Decoded (vje_core's identity). Returning early left the Text View on
		// Decoded while the Form View, which asks string_display_mode directly, was on Escaped: the same node rendered
		// two ways on a first run, which is exactly what the one-setting rule forbids (2026-07-28 review).

		profile.stringDisplay = string_display_mode ( settings );

		if ( settings == nullptr )
		{
			return profile;
		}

		profile.alignNameSeparators = settings->value_bool ( settings_keys::TEXT_ALIGN_SEPARATORS, profile.alignNameSeparators );
		profile.includeObjectNames  = settings->value_bool ( settings_keys::TEXT_INCLUDE_OBJECTS,  profile.includeObjectNames );
		profile.includeArrayNames   = settings->value_bool ( settings_keys::TEXT_INCLUDE_ARRAYS,   profile.includeArrayNames );

		// SET-06 bounds the separator at 1-3 characters. An EMPTY stored value is the one that has to be refused rather
		// than clamped: it would render "name  Bob", which reads as a formatting bug rather than as a choice.

		const QString separator = settings->value_string ( settings_keys::TEXT_NAME_SEPARATOR, profile.nameSeparator );

		if ( !separator.isEmpty () && ( separator.length () <= 3 ) )
		{
			profile.nameSeparator = separator;
		}

		const QString markdownStyle = settings->value_string
		(
			settings_keys::TEXT_MARKDOWN_STYLE,
			settings_values::MARKDOWN_STYLE_NONE
		);

		if ( markdownStyle == settings_values::MARKDOWN_STYLE_LIST )
		{
			profile.markdownListStyle = MarkdownListStyle::List;
		}
		else if ( markdownStyle == settings_values::MARKDOWN_STYLE_TABLE )
		{
			profile.markdownListStyle = MarkdownListStyle::Table;
		}

		// SET-06's default is Columnar. Note this is the APPLICATION's default, not the renderer's: TextViewProfile's own
		// member default is vje_core's business and deliberately left alone, which is why every value below is mapped
		// explicitly rather than letting an unmatched one fall through to it.

		const QString tableStyle = settings->value_string
		(
			settings_keys::TEXT_TABLE_STYLE,
			settings_values::TABLE_STYLE_COLUMNAR
		);

		if      ( tableStyle == settings_values::TABLE_STYLE_COMPACT )     { profile.tableStyle = TableStyle::Compact; }
		else if ( tableStyle == settings_values::TABLE_STYLE_ACADEMIC )    { profile.tableStyle = TableStyle::Academic; }
		else if ( tableStyle == settings_values::TABLE_STYLE_COLUMNAR )    { profile.tableStyle = TableStyle::Columnar; }
		else if ( tableStyle == settings_values::TABLE_STYLE_SPREADSHEET ) { profile.tableStyle = TableStyle::Spreadsheet; }
		else if ( tableStyle == settings_values::TABLE_STYLE_MINIMAL )     { profile.tableStyle = TableStyle::Minimal; }
		else if ( tableStyle == settings_values::TABLE_STYLE_MARKDOWN )    { profile.tableStyle = TableStyle::Markdown; }
		else if ( tableStyle == settings_values::TABLE_STYLE_CSV )         { profile.tableStyle = TableStyle::Csv; }
		else if ( tableStyle == settings_values::TABLE_STYLE_TSV )         { profile.tableStyle = TableStyle::Tsv; }
		else                                                              { profile.tableStyle = TableStyle::Columnar; }

		profile.blankLinesBetweenFields = std::clamp
		(
			settings->value_int ( settings_keys::TEXT_BLANK_LINES, profile.blankLinesBetweenFields ),
			MINIMUM_BLANK_LINES,
			MAXIMUM_BLANK_LINES
		);

		return profile;
	}

	bool hands_over_caret_on_click ( const SettingsStore* settings, const QString& editOnKey )
	{
		// The default is DOUBLE CLICK for both views. A single click in the tree presents (and, in the Code View,
		// scrolls) and stops there; the caret changes hands on the double click, which is also what moves the keyboard
		// out of the tree. Single click remains available for anyone who prefers the editor to open on the first click.

		if ( settings == nullptr )
		{
			return false;
		}

		const QString editOn = settings->value_string ( editOnKey, settings_values::EDIT_ON_DOUBLE_CLICK );

		return editOn == settings_values::EDIT_ON_SINGLE_CLICK;
	}

	StringDisplay string_display_mode ( const SettingsStore* settings )
	{
		// SET-03's default is Escaped -- the only mode that is both lossless and unambiguous, and the notation the
		// editor falls back to anyway, so at-rest and editing agree unless the user says otherwise. A null store is a
		// first run, which is that default.
		//
		// Every value is mapped explicitly, and an unrecognized one lands on Escaped rather than falling through to
		// TextViewProfile's member default -- that default is vje_core's identity transform, not this preference.

		if ( settings == nullptr )
		{
			return StringDisplay::Escaped;
		}

		const QString mode = settings->value_string
		(
			settings_keys::STRING_DISPLAY,
			settings_values::STRING_DISPLAY_ESCAPED
		);

		if ( mode == settings_values::STRING_DISPLAY_DECODED )   { return StringDisplay::Decoded; }
		if ( mode == settings_values::STRING_DISPLAY_FLATTENED ) { return StringDisplay::Flattened; }

		return StringDisplay::Escaped;
	}

	bool wrap_strings_in_form_view ( const SettingsStore* settings )
	{
		return ( settings != nullptr ) && settings->value_bool ( settings_keys::FORM_WRAP_STRINGS, false );
	}

	bool wrap_strings_in_text_view ( const SettingsStore* settings )
	{
		return ( settings != nullptr ) && settings->value_bool ( settings_keys::TEXT_WRAP_STRINGS, false );
	}

	bool print_page_rules ( const SettingsStore* settings )
	{
		// A null store is a first run, which is the default -- the rules are drawn. Stated here as the one reader; the
		// schema states the same default for the dialog, and tst_settings_schema fails if the two ever disagree.

		return ( settings == nullptr ) || settings->value_bool ( settings_keys::PRINT_PAGE_RULES, true );
	}

	bool explorer_integration_enabled ( const SettingsStore* settings )
	{
		// A null store is a first run, which is the default -- nothing registered. The schema states the same default,
		// and tst_settings_schema fails if the two ever disagree.

		return ( settings != nullptr ) && settings->value_bool ( settings_keys::EXPLORER_INTEGRATION, false );
	}

	config::appearance::InterfaceStyle interface_style ( const SettingsStore* settings )
	{
		// A null store is a first run, which is the default. Unlike most readers here this one does NOT restate its
		// default as a literal: config::appearance::DEFAULT_INTERFACE_STYLE is named here and by the schema, so the two
		// statements the drift guard exists to watch are the same statement.

		if ( settings == nullptr )
		{
			return config::appearance::DEFAULT_INTERFACE_STYLE;
		}

		// The superseded key is answered HERE as well as by migrate_interface_style below, and the redundancy is the
		// point: this reader is then total and side-effect-free, so it gives the right answer whether or not the
		// migration has run -- on a store opened read-only, in a test that never calls it, and in the window between
		// construction and the composition root's one migrating call. The migration's job is only to stop the legacy
		// key being consulted forever.

		if ( !settings->contains ( settings_keys::INTERFACE_STYLE ) )
		{
			return legacy_interface_style ( settings );
		}

		// Anything unrecognized falls to the default rather than to Classic, so a hand-edited settings file with a
		// typo in it presents the application as it is specified to look (STYLE-01..05) rather than as it looks with
		// every override switched off.

		const QString stored = settings->value_string ( settings_keys::INTERFACE_STYLE, QString () );

		if ( stored == settings_values::INTERFACE_STYLE_CLASSIC ) { return config::appearance::InterfaceStyle::Classic; }
		if ( stored == settings_values::INTERFACE_STYLE_FLUENT )  { return config::appearance::InterfaceStyle::Fluent;  }

		return config::appearance::DEFAULT_INTERFACE_STYLE;
	}

	config::tree::ChangeMarkScope change_mark_scope ( const SettingsStore* settings )
	{
		// Named rather than restated, as interface_style's default is: config::tree::DEFAULT_CHANGE_MARK_SCOPE is the
		// one statement, shared with the schema. An unrecognized spelling falls to it too.

		if ( settings == nullptr )
		{
			return config::tree::DEFAULT_CHANGE_MARK_SCOPE;
		}

		const QString stored = settings->value_string ( settings_keys::MARK_UNSAVED_CHANGES, QString () );

		if ( stored == settings_values::CHANGE_MARKS_CHANGED_NODES_AND_ANCESTORS ) { return config::tree::ChangeMarkScope::ChangedNodesAndAncestors; }
		if ( stored == settings_values::CHANGE_MARKS_FILE_NODE_ONLY )              { return config::tree::ChangeMarkScope::FileNodeOnly;             }

		return config::tree::DEFAULT_CHANGE_MARK_SCOPE;
	}

	QColor stored_change_mark_colour ( const SettingsStore* settings )
	{
		const QColor fallback = *parse_hex_colour ( QString::fromLatin1 ( config::tree::DEFAULT_CHANGE_MARK_COLOUR_DARK ) );

		if ( settings == nullptr )
		{
			return fallback;
		}

		// A hand-edited file's typo falls to the default rather than to black, which is what an invalid QColor paints.

		return parse_hex_colour ( settings->value_string ( settings_keys::CHANGE_MARK_COLOUR, QString () ) ).value_or ( fallback );
	}

	QColor change_mark_colour ( const SettingsStore* settings, bool darkTheme )
	{
		return colour_for_theme ( stored_change_mark_colour ( settings ), darkTheme );
	}

	void migrate_interface_style ( SettingsStore* settings )
	{
		// The one-time move off SET-03's "Rounded pane corners" (Phase 10.5's toolbar.visible.* pattern). Idempotent by
		// construction: it writes only when the new key is absent, and removes the old key unconditionally, so a second
		// run has nothing to find.
		//
		// An ABSENT legacy key is not a false one (lesson D8): a user who never touched the toggle has neither key, and
		// must land on the default rather than on Classic. That is why the mapping goes through legacy_interface_style,
		// which asks contains() rather than reading with a default.

		if ( settings == nullptr )
		{
			return;
		}

		if ( !settings->contains ( settings_keys::INTERFACE_STYLE ) && settings->contains ( settings_keys::LEGACY_ROUNDED_PANE_CORNERS ) )
		{
			const config::appearance::InterfaceStyle migrated = legacy_interface_style ( settings );

			settings->set_string
			(
				settings_keys::INTERFACE_STYLE,
				( migrated == config::appearance::InterfaceStyle::Classic )
					? settings_values::INTERFACE_STYLE_CLASSIC
					: settings_values::INTERFACE_STYLE_FLUENT
			);
		}

		settings->remove ( settings_keys::LEGACY_ROUNDED_PANE_CORNERS );
	}

	bool key_editing_allowed ( const SettingsStore* settings )
	{
		// A null store is a first run, which is the default -- keys edit. Stated here as the one reader; the schema
		// states the same default for the dialog, and tst_settings_schema fails if the two ever disagree.

		if ( settings == nullptr )
		{
			return true;
		}

		return settings->value_bool ( settings_keys::FORM_ALLOW_KEY_EDITING, true );
	}

	bool duplicate_keys_allowed ( const SettingsStore* settings )
	{
		// A null store is a first run, and the default is FALSE -- VAL-02 as it has always stood. The unreachable
		// fallback is given the same value as the default deliberately here and NOT for the reason lesson D20 warns
		// about: there is no `contains()` guard above it to be neutered, so the two are one statement rather than two
		// that could silently agree.

		if ( settings == nullptr )
		{
			return false;
		}

		return settings->value_bool ( settings_keys::ALLOW_DUPLICATE_KEYS, false );
	}

	void migrate_retired_settings ( SettingsStore* settings )
	{
		if ( settings == nullptr )
		{
			return;
		}

		// debug.iconSource chose between the two committed icon TREES while both held the same artwork in two formats
		// (Phase 14.5). SET-13 made the format a consequence of the family -- Classic is raster, Fluent is vector --
		// so the setting has nothing left to choose and is removed rather than left to sit unread in every existing
		// settings file.
		//
		// Deliberately NOT folded into migrate_interface_style, whose name would then be a lie about what it does.

		settings->remove ( QStringLiteral ( "debug.iconSource" ) );
	}
}
