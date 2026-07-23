//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
	};

	//*****************************************************************************************************************
	// Class: TextViewRenderer
	//*****************************************************************************************************************

	class TextViewRenderer
	{
	public:

		static QString render ( const JsonNode& node, const TextViewProfile& profile );
	};
}
