//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   clipboard_grid -- the block of cells a piece of text from ANOTHER APPLICATION describes (EDITOR-24).
//
//   A spreadsheet copies a range as one line per row with a TAB between cells, and a text editor copies several lines
//   as lines. Pasted onto an array-table cell, that text used to arrive whole in the one cell, line breaks and all --
//   which the default escaped string display then showed as `Four\nFive\nSix\n`. EDITOR-24 reads it as a GRID
//   instead, and this is the reading, pure and headless so every rule below is pinned without a clipboard.
//
//   THE RULES, each for a reason:
//
//     - TEXT THAT IS ONE JSON VALUE IS NOT A GRID. `{ "a": 1 }` spread over three lines of a text editor is one object,
//       exactly as a single-line paste of it is (EDITOR-11's external-text rule), and splitting it would paste three
//       fragments of nonsense. Checked FIRST, on the whole text.
//     - ONE TRAILING LINE BREAK IS DROPPED. Excel ends every copy with one -- a single copied cell included -- and a
//       text editor usually does, so it marks the end of the last row rather than beginning an empty one.
//     - TEXT WITH NO TAB AND NO LINE BREAK LEFT IS NOT A GRID, so the ordinary single-value paste is untouched. Text
//       that had only its trailing break is a one-cell grid, which is how a single cell copied from Excel stops
//       arriving with the break still on it.
//     - A FIELD MAY BE QUOTED, as Excel quotes a cell holding a tab, a line break or a quote: `"` opens a quoted field
//       ONLY at the field's start, `""` inside it is one quote, and the closing quote ends it. A quote anywhere else is
//       a character -- `5" pipe` is text, not the start of a field that swallows the next line. And if a quoted field
//       never closes, the text was not quoted at all, so it is read again with quoting off rather than losing the rest
//       of the paste to one stray mark.
//     - AN EMPTY FIELD IS KEPT, as an empty string. What it means is the paste's to decide (EDITOR-24: the target cell
//       is left as it is, VJE's own rule for an absent cell), and it has to reach the paste to hold a row's columns in
//       place.
//
//   Rows may differ in length -- a text editor's lines carry however many tabs they carry -- and are returned as they
//   are; the block is as wide as its widest row.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace vje
{
	namespace clipboard_grid
	{
		// The rows of the block the text describes, each a list of fields, or nullopt where the text is ONE value and
		// the ordinary single-value paste applies.

		std::optional<std::vector<QStringList>> parse ( const QString& text );

		// The block's width: its widest row.

		int width ( const std::vector<QStringList>& rows );
	}
}
