//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   print_wrapping -- wrapping preformatted text to a printed page, with each continuation line under its own line's
//   indentation (FILE-12).
//
//   WHY THIS EXISTS SEPARATELY FROM TextViewRenderer's WRAPPING. Both wrap at a character count and both use a hanging
//   indent, but they hang from different things and neither can serve the other. The Text View's continuations line up
//   under the VALUE COLUMN, because the thing being continued is a value that began after "key : " -- that indent is a
//   property of the entry and only the renderer that composed the entry knows it. The Code View's continuations line up
//   under the LINE'S OWN LEADING WHITESPACE, because what a JSON line means is read off its indentation, and a
//   continuation that started at column 0 would read as a sibling at the document root.
//
//   So this is the general rule -- "a wrapped line continues where its line began" -- and it is pure, which is what
//   lets the CodeView's whole printed layout be stated in a headless case.
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
	// Wrap every line of text at columns characters, breaking at the last space that fits and indenting each
	// continuation to its line's own leading whitespace.
	//
	// columns <= 0 returns the text unchanged, which is what "the caller does not know the page width" means. A line
	// whose own indent leaves too little room to wrap into is also left alone, for the reason TextViewRenderer states
	// at its own minimum: below a handful of characters a wrap produces a column of syllables rather than a paragraph,
	// and an over-long line is the better of the two.
	//
	// A word longer than the available width is broken at the margin rather than allowed to overrun it -- an
	// unbreakable base64 value must not push the whole page sideways.

	QString wrap_preformatted ( const QString& text, int columns );
}
