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
#include "services/SettingsStore.hpp"

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

	bool rounded_pane_corners ( const SettingsStore* settings )
	{
		// A null store is a first run, which is the default. Unlike the readers around it this one does NOT restate its
		// default as a literal: config::card::ROUNDED_TOP_CORNERS_DEFAULT is named here and by the schema, so the two
		// statements the drift guard exists to watch are the same statement.

		return ( settings == nullptr )
		     || settings->value_bool ( settings_keys::ROUNDED_PANE_CORNERS, config::card::ROUNDED_TOP_CORNERS_DEFAULT );
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

	config::icons::IconSource icon_source ( const SettingsStore* settings )
	{
		if ( settings == nullptr )
		{
			return config::icons::DEFAULT_ICON_SOURCE;
		}

		const QString stored = settings->value_string ( settings_keys::DEBUG_ICON_SOURCE, QString () );

		if ( stored == settings_values::ICON_SOURCE_SVG )
		{
			return config::icons::IconSource::Svg;
		}

		if ( stored == settings_values::ICON_SOURCE_PNG )
		{
			return config::icons::IconSource::Png;
		}

		// Absent or unrecognized. Stated as a fall-through rather than as an else on the second branch, so adding a
		// third source later cannot accidentally make it the fallback.

		return config::icons::DEFAULT_ICON_SOURCE;
	}
}
