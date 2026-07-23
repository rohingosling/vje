//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

		const QString tableStyle = settings->value_string
		(
			settings_keys::TEXT_TABLE_STYLE,
			settings_values::TABLE_STYLE_COMPACT
		);

		if      ( tableStyle == settings_values::TABLE_STYLE_ACADEMIC )    { profile.tableStyle = TableStyle::Academic; }
		else if ( tableStyle == settings_values::TABLE_STYLE_COLUMNAR )    { profile.tableStyle = TableStyle::Columnar; }
		else if ( tableStyle == settings_values::TABLE_STYLE_SPREADSHEET ) { profile.tableStyle = TableStyle::Spreadsheet; }
		else if ( tableStyle == settings_values::TABLE_STYLE_MINIMAL )     { profile.tableStyle = TableStyle::Minimal; }
		else if ( tableStyle == settings_values::TABLE_STYLE_MARKDOWN )    { profile.tableStyle = TableStyle::Markdown; }
		else if ( tableStyle == settings_values::TABLE_STYLE_CSV )         { profile.tableStyle = TableStyle::Csv; }
		else if ( tableStyle == settings_values::TABLE_STYLE_TSV )         { profile.tableStyle = TableStyle::Tsv; }

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
}
