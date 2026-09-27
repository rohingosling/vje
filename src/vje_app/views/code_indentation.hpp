//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   code_indentation -- the Code View's Tab / Shift+Tab math (EDITOR-07), as pure functions over text.
//
//   WHAT TAB INSERTS IS NOT A TAB. EDITOR-07 is explicit that the indentation follows the DOCUMENT FORMAT PROFILE
//   (SET-07) -- the same spaces-or-tab and the same size the view displays and File > Save writes -- so a user working
//   in a 4-space document gets four spaces from the Tab key, and pressing Tab never introduces a character the saved
//   file would not otherwise contain. There is deliberately no separate Tab setting to disagree with.
//
//   OUTDENT IS TOLERANT AND INDENT IS NOT. Indentation is generated, so it can be exact; the text being outdented was
//   very possibly typed by hand, or pasted from somewhere with different habits, and refusing to outdent a line because
//   it is indented with the wrong character would be the editor arguing with the user about whitespace. So outdent
//   removes one leading tab OR up to one indent-width of leading spaces, whichever the line actually starts with, under
//   either profile.
//
//   ENTER KEEPS THE INDENT (Phase 15k). What Enter does is the question Tab answers, asked of a new line: the new line
//   carries the current line's indentation, one level deeper after an opening bracket and one level shallower when
//   what follows the caret is a closing one. The level is the profile's, so Enter, Tab and File > Save cannot disagree
//   about what one level is.
//
//   BACKSPACE GOES BACK TO WHERE THE BRACKETS SAY (Phase 15k.1). In a line's indentation Backspace removes whitespace
//   rather than one character: back to the indent the enclosing brackets call for when the caret is deeper than that,
//   otherwise back to the previous indent stop. Anywhere else it is an ordinary Backspace, which this module leaves to
//   Qt. The enclosing brackets are found by reading the lines above the caret, which is exact line by line because no
//   JSON token -- a string included -- spans a line break.
//
//   Kept out of the widget so the edge cases -- an already-flush line, a mixed-whitespace line, a partially selected
//   last line, a caret between a bracket pair -- are stated where nothing can hide behind QTextCursor's own behaviour
//   (the same reasoning as grid_navigation).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/services/JsonFormatter.hpp>

#include <QString>

#include <functional>
#include <optional>

namespace vje
{
	// One indent level under the profile: indentSize spaces, or a single tab.

	QString indent_unit ( const FormatProfile& profile );

	// How many leading characters an outdent removes from one line: 1 for a leading tab, otherwise the number of
	// leading spaces up to one indent width, otherwise 0 (a flush line, or one starting with anything else).

	int outdent_width ( const QString& line, const FormatProfile& profile );

	// Indent / outdent every line of a block, newline-separated and returned in the same form. EMPTY lines are left
	// alone by the indent -- an editor that "indents" a blank line has only added trailing whitespace to it, which the
	// user cannot see and a diff can.

	QString indent_block  ( const QString& block, const FormatProfile& profile );
	QString outdent_block ( const QString& block, const FormatProfile& profile );

	//-----------------------------------------------------------------------------------------------------------------
	// Enter (EDITOR-07). The edit is stated as a replacement WITHIN THE ONE LINE the caret is on: the columns
	// [replaceFrom, replaceTo) give way to insertion, which begins with the line break, and the caret lands caretOffset
	// characters into the insertion. The whitespace either side of the caret is inside that range on purpose -- the line
	// the caret leaves keeps no trailing whitespace, and the text carried down starts exactly at the new indent rather
	// than at the new indent plus whatever spaces happened to follow the caret.
	//-----------------------------------------------------------------------------------------------------------------

	struct NewLineEdit
	{
		int     replaceFrom = 0;
		int     replaceTo   = 0;
		QString insertion;
		int     caretOffset = 0;
	};

	// The rule, in order:
	//
	//   - The new line starts from the current line's LEADING WHITESPACE, whatever it is made of -- a line indented by
	//     hand keeps its hand-made indent.
	//   - A caret inside that indentation pushes the line down unchanged, and the line above it is left empty.
	//   - Otherwise the brackets the text BEFORE the caret leaves open set the level: one deeper for each it opens and
	//     does not close, one shallower for each it closes without having opened. Closing brackets at the START of the
	//     line do not count -- the line's own indentation already stands for them, which is why the line after "}," is
	//     a sibling of it rather than one level out. Brackets inside strings never count.
	//   - A closing bracket straight after the caret takes one level off, because the new line starts with it -- unless
	//     the text before the caret ENDS with an opening bracket, where the pair is split over three lines: the caret on
	//     a line of its own one level in, and the closing bracket beneath it at the opening bracket's level.

	NewLineEdit indent_for_new_line ( const QString& line, int column, const FormatProfile& profile );

	//-----------------------------------------------------------------------------------------------------------------
	// Backspace (EDITOR-07, Phase 15k.1). How many characters before the caret one Backspace removes, or 0 where it is
	// an ordinary Backspace and the widget's own behaviour applies: a caret at the start of the line (which joins it to
	// the one above), or one with anything but whitespace before it.
	//
	// nextLineUp answers the line above the caret's, then the one above that, and nothing once past the first line.
	// It is asked only for a caret in the indentation, and only as far up as the enclosing bracket -- so a Backspace
	// after text reads no other line, and one inside a container reads back only to where the container opens.
	//-----------------------------------------------------------------------------------------------------------------

	using LineSource = std::function< std::optional<QString> () >;

	// The rule, measured in columns, a tab reaching the next multiple of the indent size:
	//
	//   - The STRUCTURAL indent is that of the line holding the nearest opening bracket above the caret that nothing
	//     between closes, plus one level -- or that line's own indent where the caret's line begins with a closing
	//     bracket, which is that opening bracket's partner. With no enclosing bracket it is column 0. It is the
	//     indentation of the bracket's LINE and not the bracket's column: under K&R the bracket ends a key's line.
	//   - A caret deeper than the structural indent goes back to it, in one press.
	//   - A caret at or shallower than it goes back to the previous indent stop.
	//   - A line above that ends inside a string cannot be read -- a JSON string never spans a line break -- so the
	//     structure is unknown and the caret goes back to the previous indent stop.
	//
	// Only whole characters are removed, so a tab the target falls inside is removed with it.

	int backspace_width ( const QString& line, int column, const LineSource& nextLineUp, const FormatProfile& profile );
}
