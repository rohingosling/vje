//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for code_indentation -- the Code View's Tab / Shift+Tab math (EDITOR-07).
//
//   HEADLESS, and that is the point of extracting it. The edge cases here -- an already-flush line, a line indented
//   with the character the profile does not use, a block whose lines are indented unevenly -- are the ones that would
//   otherwise be discovered by a user rather than by a test, because in the widget they hide behind QTextCursor's own
//   behaviour and are only visible by eye.
//
//   The claims are:
//
//     - WHAT TAB INSERTS FOLLOWS THE PROFILE (EDITOR-07). Spaces documents get spaces, tab documents get a tab, and the
//       count follows indentSize -- there is deliberately no separate Tab setting to disagree with File > Save.
//     - OUTDENT IS TOLERANT AND INDENT IS NOT. A line indented with a tab outdents under a Spaces profile and vice
//       versa, because that whitespace was very possibly typed by hand or pasted, and arguing with the user about it
//       is not the editor's job.
//     - OUTDENT NEVER EATS CONTENT. A flush line is left alone rather than losing its first character, and a line with
//       fewer leading spaces than one level loses only what it has.
//     - EMPTY LINES ARE NOT INDENTED. "Indenting" a blank line only adds trailing whitespace, which the user cannot see
//       and a diff can.
//     - BACKSPACE IN THE INDENTATION GOES WHERE THE BRACKETS SAY (Phase 15k.1): back to the enclosing bracket's
//       indent plus one level when the caret is deeper, otherwise to the previous indent stop -- and anywhere else it
//       is Qt's Backspace, reading no line but its own.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/code_indentation.hpp"

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	FormatProfile spaces_profile ( int size )
	{
		FormatProfile profile;

		profile.indent     = IndentKind::Spaces;
		profile.indentSize = size;

		return profile;
	}

	FormatProfile tabs_profile ()
	{
		FormatProfile profile;

		profile.indent = IndentKind::Tabs;

		return profile;
	}

	// Enter, applied: the line as it reads after the edit, with the caret shown as '|'. Stating each case as the text
	// the user ends up looking at is what makes a table of them reviewable -- a list of column offsets is not.

	QString press_enter ( const QString& lineWithCaret, const FormatProfile& profile )
	{
		const int     column = lineWithCaret.indexOf ( QLatin1Char ( '|' ) );
		const QString line   = QString ( lineWithCaret ).remove ( column, 1 );

		const NewLineEdit edit = indent_for_new_line ( line, column, profile );

		QString result = line;

		result.replace ( edit.replaceFrom, edit.replaceTo - edit.replaceFrom, edit.insertion );
		result.insert  ( edit.replaceFrom + edit.caretOffset, QLatin1Char ( '|' ) );

		return result;
	}

	// Backspace, applied: the document as it reads after one press, the caret shown as '|'. A result equal to the input
	// is backspace_width leaving the key to Qt. outLinesRead counts the lines above that the rule asked for.

	QString press_backspace ( const QString& textWithCaret, const FormatProfile& profile, int* outLinesRead = nullptr )
	{
		const int     caret = textWithCaret.indexOf ( QLatin1Char ( '|' ) );
		const QString text  = QString ( textWithCaret ).remove ( caret, 1 );

		const int lineStart = ( caret == 0 ) ? 0 : text.lastIndexOf ( QLatin1Char ( '\n' ), caret - 1 ) + 1;
		const int newLine   = text.indexOf ( QLatin1Char ( '\n' ), caret );
		const int lineEnd   = ( newLine < 0 ) ? text.length () : newLine;

		QStringList above = ( lineStart == 0 ) ? QStringList () : text.left ( lineStart - 1 ).split ( QLatin1Char ( '\n' ) );

		int linesRead = 0;

		const LineSource nextLineUp = [ &above, &linesRead ] () -> std::optional<QString>
		{
			if ( above.isEmpty () )
			{
				return std::nullopt;
			}

			++linesRead;

			return above.takeLast ();
		};

		const int width = backspace_width ( text.mid ( lineStart, lineEnd - lineStart ), caret - lineStart, nextLineUp, profile );

		if ( outLinesRead != nullptr )
		{
			*outLinesRead = linesRead;
		}

		QString result = text;

		result.remove ( caret - width, width );
		result.insert ( caret - width, QLatin1Char ( '|' ) );

		return result;
	}
}

class TestCodeIndentation : public QObject
{
	Q_OBJECT

private slots:

	//=================================================================================================================
	// indent_unit -- what the Tab key inserts (EDITOR-07).
	//=================================================================================================================

	void unit_follows_the_profile ()
	{
		QCOMPARE ( indent_unit ( spaces_profile ( 2 ) ), QStringLiteral ( "  " ) );
		QCOMPARE ( indent_unit ( spaces_profile ( 4 ) ), QStringLiteral ( "    " ) );
		QCOMPARE ( indent_unit ( tabs_profile () ),      QStringLiteral ( "\t" ) );
	}

	void unit_never_collapses_to_nothing ()
	{
		// A stored indentSize of 0 is meaningless but is still a request for "as little as possible". Honouring it
		// literally would make the Tab key insert an empty string, which reads as a broken keyboard.

		QCOMPARE ( indent_unit ( spaces_profile ( 0 ) ).length (), 1 );
	}

	//=================================================================================================================
	// outdent_width -- how much one line loses.
	//=================================================================================================================

	void outdent_removes_one_level_of_spaces ()
	{
		QCOMPARE ( outdent_width ( QStringLiteral ( "    \"a\": 1" ), spaces_profile ( 2 ) ), 2 );
		QCOMPARE ( outdent_width ( QStringLiteral ( "    \"a\": 1" ), spaces_profile ( 4 ) ), 4 );
	}

	void outdent_takes_only_what_is_there ()
	{
		// One space under a 4-space profile loses its one space, not four characters of content.

		QCOMPARE ( outdent_width ( QStringLiteral ( " \"a\": 1" ), spaces_profile ( 4 ) ), 1 );
	}

	void outdent_leaves_a_flush_line_alone ()
	{
		QCOMPARE ( outdent_width ( QStringLiteral ( "\"a\": 1" ), spaces_profile ( 2 ) ), 0 );
		QCOMPARE ( outdent_width ( QString (),                   spaces_profile ( 2 ) ), 0 );
	}

	void outdent_accepts_whichever_whitespace_the_line_has ()
	{
		// The tolerance rule, both ways round: a tab-indented line outdents under a Spaces profile, and a
		// space-indented line outdents under a Tabs one. Neither is what the profile would have produced.

		QCOMPARE ( outdent_width ( QStringLiteral ( "\t\"a\": 1" ), spaces_profile ( 4 ) ), 1 );
		QCOMPARE ( outdent_width ( QStringLiteral ( "  \"a\": 1" ), tabs_profile () ),      2 );
	}

	//=================================================================================================================
	// Block operations.
	//=================================================================================================================

	void indent_block_adds_one_level_per_line ()
	{
		const QString block = QStringLiteral ( "\"a\": 1\n\"b\": 2" );

		QCOMPARE ( indent_block ( block, spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": 1\n  \"b\": 2" ) );
	}

	void indent_block_skips_empty_lines ()
	{
		const QString block = QStringLiteral ( "\"a\": 1\n\n\"b\": 2" );

		// The middle line stays genuinely empty rather than becoming two spaces of invisible trailing whitespace.

		QCOMPARE ( indent_block ( block, spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": 1\n\n  \"b\": 2" ) );
	}

	void outdent_block_is_per_line ()
	{
		// A block with one flush line among indented ones outdents the rest and leaves that one where it is, which is
		// what dragging a selection over a whole object and pressing Shift+Tab has to do.

		const QString block = QStringLiteral ( "    \"a\": 1\n\"b\": 2\n    \"c\": 3" );

		QCOMPARE
		(
			outdent_block ( block, spaces_profile ( 4 ) ),
			QStringLiteral ( "\"a\": 1\n\"b\": 2\n\"c\": 3" )
		);
	}

	void indent_then_outdent_is_the_identity ()
	{
		const QString block = QStringLiteral ( "{\n  \"a\": 1,\n\n  \"b\": [ 2, 3 ]\n}" );

		for ( const FormatProfile& profile : { spaces_profile ( 2 ), spaces_profile ( 4 ), tabs_profile () } )
		{
			QCOMPARE ( outdent_block ( indent_block ( block, profile ), profile ), block );
		}
	}

	//=================================================================================================================
	// indent_for_new_line -- what Enter does (EDITOR-07, Phase 15k).
	//=================================================================================================================

	void enter_keeps_the_lines_indent ()
	{
		QCOMPARE ( press_enter ( QStringLiteral ( "    \"a\": 1,|" ), spaces_profile ( 2 ) ), QStringLiteral ( "    \"a\": 1,\n    |" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "\t\t\"a\": 1,|" ),  tabs_profile () ),      QStringLiteral ( "\t\t\"a\": 1,\n\t\t|" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "\"a\": 1|" ),       spaces_profile ( 2 ) ), QStringLiteral ( "\"a\": 1\n|" ) );
	}

	void enter_after_an_opening_bracket_goes_one_level_in ()
	{
		// Both brace styles: K&R opens on the key's line, Allman on a line of its own. The level is the PROFILE's, so
		// this is what Tab would insert and what Save would write.

		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\": {|" ), spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": {\n    |" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\": [|" ), spaces_profile ( 4 ) ), QStringLiteral ( "  \"a\": [\n      |" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "  {|" ),        spaces_profile ( 2 ) ), QStringLiteral ( "  {\n    |" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "\t{|" ),        tabs_profile () ),      QStringLiteral ( "\t{\n\t\t|" ) );

		// The key line under Allman opens nothing yet, so it keeps the level; the brace the user types next does.

		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\":|" ),   spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\":\n  |" ) );
	}

	void enter_before_a_closing_bracket_goes_one_level_out ()
	{
		// The line being opened starts with the bracket, so it takes the level of the line the bracket closes.

		QCOMPARE ( press_enter ( QStringLiteral ( "    \"b\": 2|}" ),  spaces_profile ( 2 ) ), QStringLiteral ( "    \"b\": 2\n  |}" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "\t\t\"b\": 2|]" ),  tabs_profile () ),      QStringLiteral ( "\t\t\"b\": 2\n\t|]" ) );
	}

	void a_bracket_closed_on_the_line_that_opened_it_closes_at_that_lines_level ()
	{
		// K&R's inline array broken by hand: the "]" belongs to the "[" on this same line, so it lands under the member,
		// not a level further out -- and a line broken inside the array continues one level in.

		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\": [ 1, 2 |]" ), spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": [ 1, 2\n  |]" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\": [ 1,| 2 ]" ), spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": [ 1,\n    |2 ]" ) );
	}

	void enter_between_a_bracket_pair_opens_it_over_three_lines ()
	{
		// "{}" is how the formatter writes an empty container, and Enter between the two is how a user starts filling
		// one: the caret gets a line of its own one level in, and the closing bracket stays at the opening one's level.

		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\": {|}" ),  spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": {\n    |\n  }" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "\t\"a\": [ | ]" ), tabs_profile () ),      QStringLiteral ( "\t\"a\": [\n\t\t|\n\t]" ) );
	}

	void a_closing_bracket_at_the_start_of_a_line_does_not_count ()
	{
		// The line's own indentation already stands for it: after "}," comes a sibling of the object just closed, at the
		// same level, not a line one level further out.

		QCOMPARE ( press_enter ( QStringLiteral ( "  },|" ),   spaces_profile ( 2 ) ), QStringLiteral ( "  },\n  |" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "  }|" ),    spaces_profile ( 2 ) ), QStringLiteral ( "  }\n  |" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "  }, {|" ), spaces_profile ( 2 ) ), QStringLiteral ( "  }, {\n    |" ) );
	}

	void brackets_inside_strings_do_not_count ()
	{
		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\": \"{[\",|" ),     spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": \"{[\",\n  |" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\": \"x\\\"{\",|" ), spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": \"x\\\"{\",\n  |" ) );
	}

	void a_caret_in_the_indentation_pushes_the_line_down_unchanged ()
	{
		// The line above is left EMPTY, not holding the indentation as invisible trailing whitespace -- and the line
		// itself keeps its full indent however far into it the caret was.

		QCOMPARE ( press_enter ( QStringLiteral ( "|    \"a\": 1" ), spaces_profile ( 2 ) ), QStringLiteral ( "\n    |\"a\": 1" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "  |  \"a\": 1" ), spaces_profile ( 2 ) ), QStringLiteral ( "\n    |\"a\": 1" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "  |}" ),          spaces_profile ( 2 ) ), QStringLiteral ( "\n  |}" ) );
	}

	void enter_on_a_blank_line_keeps_its_indent_and_leaves_no_trailing_whitespace ()
	{
		QCOMPARE ( press_enter ( QStringLiteral ( "    |" ), spaces_profile ( 2 ) ), QStringLiteral ( "\n    |" ) );
		QCOMPARE ( press_enter ( QStringLiteral ( "|" ),     spaces_profile ( 2 ) ), QStringLiteral ( "\n|" ) );
	}

	void whitespace_either_side_of_the_caret_is_not_carried ()
	{
		// Trailing spaces stay off the line the caret leaves, and the text carried down starts AT the new indent rather
		// than at the new indent plus the spaces that happened to follow the caret.

		QCOMPARE ( press_enter ( QStringLiteral ( "  \"a\": 1,  |  \"b\": 2" ), spaces_profile ( 2 ) ), QStringLiteral ( "  \"a\": 1,\n  |\"b\": 2" ) );
	}

	void outdenting_a_hand_made_indent_takes_what_is_there ()
	{
		// A one-space indent under a 4-space profile, before a closing bracket: out by what exists, and no further.

		QCOMPARE ( press_enter ( QStringLiteral ( " \"b\": 2|}" ), spaces_profile ( 4 ) ), QStringLiteral ( " \"b\": 2\n|}" ) );
	}

	//=================================================================================================================
	// backspace_width -- what Backspace does in a line's indentation (EDITOR-07, Phase 15k.1).
	//=================================================================================================================

	void a_caret_deeper_than_the_structure_goes_back_to_it_in_one_press ()
	{
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": {\n        |\"b\": 1" ), spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": {\n    |\"b\": 1" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "[\n      |" ),                     spaces_profile ( 2 ) ), QStringLiteral ( "[\n  |" ) );

		// A caret partway into the indentation takes only what is before it; the line keeps the rest.

		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n    |  \"a\": 1" ),              spaces_profile ( 2 ) ), QStringLiteral ( "{\n  |  \"a\": 1" ) );
	}

	void a_caret_at_the_structure_goes_back_one_level ()
	{
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": {\n    |\"b\": 1" ), spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": {\n  |\"b\": 1" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": {\n  |\"b\": 1" ),   spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": {\n|\"b\": 1" ) );
	}

	void a_caret_off_an_indent_stop_goes_back_to_the_previous_stop ()
	{
		// Column 6 under a 4-space profile, which is where the structure puts it: to column 4, not to 2 -- a Backspace
		// ends on an indent stop, which is what the next Tab or Enter will measure from.

		QCOMPARE ( press_backspace ( QStringLiteral ( "[\n  [\n      |1" ), spaces_profile ( 4 ) ), QStringLiteral ( "[\n  [\n    |1" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "[\n   |1" ),         spaces_profile ( 4 ) ), QStringLiteral ( "[\n|1" ) );
	}

	void a_closing_bracket_goes_back_to_its_opening_brackets_line ()
	{
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": {\n    \"b\": 1\n      |}" ), spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": {\n    \"b\": 1\n  |}" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": [\n    |]" ),                spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": [\n  |]" ) );
	}

	void the_structure_is_the_bracket_lines_indent_not_the_brackets_column ()
	{
		// K&R ends a key's line with the bracket, at column 7 here: the members belong one level inside the LINE, at 4,
		// not beside the bracket. Allman puts the bracket on a line of its own, where the two agree.

		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": {\n          |\"b\": 1" ), spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": {\n    |\"b\": 1" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\":\n  {\n        |\"b\": 1" ), spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\":\n  {\n    |\"b\": 1" ) );
	}

	void containers_closed_above_the_caret_are_not_the_enclosing_one ()
	{
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": {\n    \"x\": 1\n  },\n  \"b\": [ 1, 2 ],\n      |\"c\": 3" ), spaces_profile ( 2 ) ),
		           QStringLiteral ( "{\n  \"a\": {\n    \"x\": 1\n  },\n  \"b\": [ 1, 2 ],\n  |\"c\": 3" ) );

		// "}, {" closes one element and opens the next: the one it opens is the caret's.

		QCOMPARE ( press_backspace ( QStringLiteral ( "[\n  {\n    \"x\": 1\n  }, {\n        |\"y\": 2" ), spaces_profile ( 2 ) ),
		           QStringLiteral ( "[\n  {\n    \"x\": 1\n  }, {\n    |\"y\": 2" ) );
	}

	void brackets_inside_strings_are_not_structure ()
	{
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": \"{[\",\n      |\"b\": 1" ),     spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": \"{[\",\n  |\"b\": 1" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": \"x\\\"{\",\n      |\"b\": 1" ), spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": \"x\\\"{\",\n  |\"b\": 1" ) );
	}

	void with_no_enclosing_bracket_the_structure_is_column_zero ()
	{
		QCOMPARE ( press_backspace ( QStringLiteral ( "      |{" ),             spaces_profile ( 2 ) ), QStringLiteral ( "|{" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n}\n    |" ),          spaces_profile ( 2 ) ), QStringLiteral ( "{\n}\n|" ) );
	}

	void an_unreadable_line_above_falls_back_to_the_previous_stop ()
	{
		// A line ending inside a string is not JSON, so nothing above it can be trusted: the caret at 8 goes to the stop
		// at 6, where the readable version of the same text goes to the structure at 2.

		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": \"open\n        |\"b\": 1" ),   spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": \"open\n      |\"b\": 1" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": \"open\",\n        |\"b\": 1" ), spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": \"open\",\n  |\"b\": 1" ) );
	}

	void tab_indentation_is_measured_in_the_columns_a_tab_occupies ()
	{
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n\t\"a\": {\n\t\t\t\t|\"b\": 1" ), tabs_profile () ), QStringLiteral ( "{\n\t\"a\": {\n\t\t|\"b\": 1" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n\t\"a\": {\n\t\t|\"b\": 1" ),     tabs_profile () ), QStringLiteral ( "{\n\t\"a\": {\n\t|\"b\": 1" ) );

		// Mixed, under a 4-space profile: a tab and six spaces is column 10, and the structure is at 8.

		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n\t\"a\": {\n\t      |\"b\": 1" ), spaces_profile ( 4 ) ), QStringLiteral ( "{\n\t\"a\": {\n\t    |\"b\": 1" ) );
	}

	void a_tab_the_target_falls_inside_is_removed_whole ()
	{
		// The structure is at column 6 and the second tab spans 4 to 8: it goes, and the caret ends at 4 rather than a
		// tab being split into spaces behind the user's back.

		QCOMPARE ( press_backspace ( QStringLiteral ( "[\n  [\n\t\t|1" ), spaces_profile ( 4 ) ), QStringLiteral ( "[\n  [\n\t|1" ) );
	}

	void anywhere_but_the_indentation_is_an_ordinary_backspace ()
	{
		// Left to Qt, which removes one character or joins the line to the one above.

		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\": 1|" ),    spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\": 1|" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n  \"a\":    |1" ), spaces_profile ( 2 ) ), QStringLiteral ( "{\n  \"a\":    |1" ) );
		QCOMPARE ( press_backspace ( QStringLiteral ( "{\n|  \"a\": 1" ),    spaces_profile ( 2 ) ), QStringLiteral ( "{\n|  \"a\": 1" ) );
	}

	void the_lines_above_are_read_only_as_far_as_the_enclosing_bracket ()
	{
		// A guard on the work, which no result above can see: a Backspace after text reads nothing, and one in the
		// indentation stops at the bracket it is inside rather than walking to the top of the document.

		int linesRead = -1;

		press_backspace ( QStringLiteral ( "{\n  \"x\": 1,\n  \"a\": 1|" ), spaces_profile ( 2 ), &linesRead );

		QCOMPARE ( linesRead, 0 );

		press_backspace ( QStringLiteral ( "{\n  \"x\": 1,\n  \"a\": {\n    \"b\": 1,\n      |\"c\": 2" ), spaces_profile ( 2 ), &linesRead );

		QCOMPARE ( linesRead, 2 );
	}
};

QTEST_APPLESS_MAIN ( TestCodeIndentation )

#include "tst_code_indentation.moc"
