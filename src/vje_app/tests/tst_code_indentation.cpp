//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
};

QTEST_APPLESS_MAIN ( TestCodeIndentation )

#include "tst_code_indentation.moc"
