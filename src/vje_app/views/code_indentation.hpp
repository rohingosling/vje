//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
//   Kept out of the widget so the edge cases -- an already-flush line, a mixed-whitespace line, a partially selected
//   last line -- are stated where nothing can hide behind QTextCursor's own behaviour (the same reasoning as
//   grid_navigation).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/services/JsonFormatter.hpp>

#include <QString>

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
}
