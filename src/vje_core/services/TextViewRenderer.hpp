//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TextViewRenderer -- the read-only Text View rendering (EDITOR-06 / SET-06), pure text output the user copies into
//   other applications. It renders:
//
//     - an OBJECT selection as a key-value listing: one line per member, "key <sep> value", with the separator
//       optionally aligned down a column, child-object / child-array rows individually includable, and an optional
//       Markdown list or Markdown Key/Value table re-rendering;
//     - a SCALAR selection as its textual form on one line;
//     - an ARRAY selection as a table in one of eight styles (Academic, Compact -- the default, Columnar,
//       Spreadsheet, Minimal, Markdown, CSV, TSV). Columns are the union of member keys (array of objects) in
//       first-encountered order, or a single unheadered column (scalar array).
//
//   The output never RENDERS Markdown -- Markdown styles emit Markdown SOURCE. There is no trailing newline.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/services/json_escapes.hpp>

#include <QString>

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// The Text View rendering options (SET-06). Defaults match the spec: separators aligned, ":" separator, both
	// container rows included, no Markdown list style, Compact table style.
	//-----------------------------------------------------------------------------------------------------------------

	enum class MarkdownListStyle
	{
		None,               // Plain "key <sep> value" form rows.
		List,               // "- **key**: value" Markdown bullets (Align / separator ignored).
		Table               // A Markdown "Key / Value" table (Align / separator ignored).
	};

	enum class TableStyle
	{
		Academic,           // Horizontal rules above/below the header and at the foot; no borders.
		Compact,            // Full box border with a header rule (the default).
		Columnar,           // Column separators and a header rule; no per-row rules.
		Spreadsheet,        // Column separators and a rule between every row.
		Minimal,            // No borders; header, a blank line, then rows.
		Markdown,           // A Markdown pipe table (source, never rendered).
		Csv,                // RFC 4180 comma-separated values.
		Tsv                 // Tab-separated values (no quoting).
	};

	struct TextViewProfile
	{
		bool              alignNameSeparators = true;
		QString           nameSeparator       = QStringLiteral ( ":" );
		bool              includeObjectNames  = true;
		bool              includeArrayNames   = true;
		MarkdownListStyle markdownListStyle   = MarkdownListStyle::None;
		TableStyle        tableStyle          = TableStyle::Compact;

		// How a string value's control characters are shown (SET-03). Decoded here -- the identity -- deliberately, and
		// NOT the setting's own default of Escaped: this member default belongs to vje_core, and what the application
		// offers is a UI preference stated once by settings_profiles. The same reasoning kept tableStyle at Compact
		// after SET-06's default became Columnar.

		StringDisplay stringDisplay = StringDisplay::Decoded;

		// The column to wrap long values at, with a hanging indent under the value column (SET-05 / SET-06). 0 disables
		// it, which is what keeps every rendering that predates this option byte-for-byte unchanged.
		//
		// It is a WIDTH IN CHARACTERS, supplied by the view from its viewport, because the renderer aligns by counting
		// characters and the fixed-width font makes the two exact (architecture.md section 4.6). Only the key-value
		// listing and the Markdown LIST wrap: CSV, TSV and the Markdown table are machine formats where a wrapped
		// record is corrupt rather than untidy, and the five box table styles are out of scope for v2.0.

		int wrapColumns = 0;

		// Blank lines inserted BETWEEN key-value entries (SET-06). 0 is the identity, which is what leaves every
		// rendering that predates the option byte-for-byte unchanged.
		//
		// Between, never after: no trailing blank line, which would break the renderer's "no trailing newline" contract
		// the first time someone set it. It applies to the key-value listing alone -- a blank line is appearance there
		// and MEANING everywhere else, turning a tight Markdown list loose, ending a Markdown table, corrupting a CSV
		// record, and cutting through a box-drawn table's borders.

		int blankLinesBetweenFields = 0;
	};

	//*****************************************************************************************************************
	// Class: TextViewRenderer
	//*****************************************************************************************************************

	class TextViewRenderer
	{
	public:

		static QString render ( const JsonNode& node, const TextViewProfile& profile );

		// Does a rendering of THIS node under THIS profile wrap its long values (given a non-zero wrapColumns)?
		//
		// The rule is already implicit in render()'s dispatch and in which branches consult wrapColumns, and it is
		// stated here because a caller can genuinely need to know it in advance: the print path (FILE-12) wraps what
		// can be wrapped and CLIPS what cannot, since a CSV or TSV record broken across two lines is a corrupt file
		// rather than an untidy one, a Markdown table stops being a table, and a box-drawing table loses its borders.
		// Answering it by measuring the output instead would confuse "this style does not wrap" with "this one line
		// could not be wrapped", and the second is a line that must NOT be clipped.

		static bool wraps_long_values ( const JsonNode& node, const TextViewProfile& profile );
	};
}
