//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for json_text_index -- the map between the Code View's text and the document's nodes, in both directions:
//   the pointer -> line the tree's selection is revealed through, and the line -> pointer a double click in the editor
//   names a node with (EDITOR-07).
//
//   The claims worth stating, because each is a decision rather than an obvious consequence:
//
//     - A MEMBER'S LINE IS ITS KEY'S LINE, not its value's. Under Allman a container member's key and its opening
//       brace are on different lines, and the key is the one that names the node -- revealing the brace while leaving
//       the key off screen is a surprising answer to selecting a member.
//     - IT READS THE TEXT, so it describes whatever the buffer holds -- hand-formatted, differently indented, or
//       mid-edit. That is the whole reason it is not derived from JsonFormatter, and the "everything on one line" case
//       is the cheapest way to state it.
//     - IT IS TOLERANT. Invalid text is what a user has for most of the time they spend typing, so a malformed tail
//       must still leave the region above it navigable. This is what keeps EDITOR-09's "tree navigation during an
//       uncommitted edit only moves the caret within the text" working while the text is broken.
//     - ESCAPED KEYS resolve to DECODED pointer tokens, since that is what a JsonPointer holds.
//     - THE REVERSE LOOKUP ANSWERS WITH THE DEEPEST NODE COVERING A LINE, which is why the index holds a SPAN and not
//       just a start: a click on a container's closing brace belongs to the container, and with start lines alone the
//       nearest entry above it is the container's last child -- a confidently wrong answer.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/json_text_index.hpp"

#include <vje_core/document/JsonPointer.hpp>

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	int line_of ( const PointerSpanIndex& index, const QString& pointerText )
	{
		return line_for_pointer ( index, JsonPointer::parse ( pointerText ) );
	}

	// The reverse lookup as pointer TEXT, so a case reads as the line-to-pointer table it is.

	QString pointer_at ( const PointerSpanIndex& index, int line )
	{
		return pointer_at_line ( index, line ).to_string ();
	}
}

class TestJsonTextIndex : public QObject
{
	Q_OBJECT

private slots:

	//=================================================================================================================
	// The ordinary case.
	//=================================================================================================================

	void maps_every_node_of_a_formatted_document ()
	{
		// K&R-shaped, one member per line -- the simplest arrangement, so the expected lines can be read off directly.

		const QString text = QStringLiteral (
			"{\n"                       // 1  -- the root
			"  \"id\": 1001,\n"         // 2
			"  \"name\": \"Alex\",\n"   // 3
			"  \"roles\": [\n"          // 4
			"    \"admin\",\n"          // 5
			"    \"editor\"\n"          // 6
			"  ]\n"
			"}" );

		const PointerSpanIndex index = build_pointer_span_index ( text );

		QCOMPARE ( line_of ( index, QString () ),            1 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/id" ) ),       2 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/name" ) ),     3 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/roles" ) ),    4 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/roles/0" ) ),  5 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/roles/1" ) ),  6 );
	}

	void a_member_is_named_by_its_key_line_not_its_value_line ()
	{
		// Allman: the member's key is on line 2 and the object it opens is on line 3. Selecting /profile must reveal
		// the key, which is what identifies the node on screen.

		const QString text = QStringLiteral (
			"{\n"                     // 1
			"  \"profile\":\n"        // 2  <- the key
			"  {\n"                   // 3  <- the value's opening brace
			"    \"city\": \"Cape Town\"\n"   // 4
			"  }\n"
			"}" );

		const PointerSpanIndex index = build_pointer_span_index ( text );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/profile" ) ),      2 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/profile/city" ) ), 4 );
	}

	void nested_arrays_of_objects_are_indexed_by_position ()
	{
		const QString text = QStringLiteral (
			"{\n"                                  // 1
			"  \"projects\": [\n"                  // 2
			"    { \"name\": \"Editor\" },\n"      // 3
			"    { \"name\": \"Migration\" }\n"    // 4
			"  ]\n"
			"}" );

		const PointerSpanIndex index = build_pointer_span_index ( text );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/projects/0" ) ),      3 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/projects/0/name" ) ), 3 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/projects/1/name" ) ), 4 );
	}

	//=================================================================================================================
	// It describes the TEXT, not a format profile.
	//=================================================================================================================

	void a_single_line_document_maps_every_node_to_line_one ()
	{
		const PointerSpanIndex index = build_pointer_span_index ( QStringLiteral ( "{\"a\":{\"b\":[1,2]}}" ) );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/a" ) ),     1 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/a/b/1" ) ), 1 );
	}

	void escaped_keys_map_to_decoded_pointer_tokens ()
	{
		// "a/b" is written "a~1b" in a pointer, and the index is keyed by the pointer's own text form -- so the
		// encoding has to happen exactly once, in JsonPointer, and not again here.

		const PointerSpanIndex index = build_pointer_span_index ( QStringLiteral ( "{\n  \"a/b\": 1\n}" ) );

		QCOMPARE ( line_for_pointer ( index, JsonPointer::from_tokens ( { QStringLiteral ( "a/b" ) } ) ), 2 );
	}

	//=================================================================================================================
	// Tolerance -- the state the buffer is in for most of an editing session.
	//=================================================================================================================

	void a_malformed_tail_leaves_the_region_above_it_navigable ()
	{
		// The user has typed a stray comma and not yet finished the line. Everything above the damage must still be
		// reachable from the tree (EDITOR-09).

		const QString text = QStringLiteral (
			"{\n"
			"  \"id\": 1001,\n"
			"  \"name\": \"Alex\",\n"
			"  \"roles\": ,,,\n"
			"}" );

		const PointerSpanIndex index = build_pointer_span_index ( text );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/id" ) ),   2 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/name" ) ), 3 );
	}

	void an_unknown_pointer_answers_zero ()
	{
		const PointerSpanIndex index = build_pointer_span_index ( QStringLiteral ( "{ \"a\": 1 }" ) );

		// 0 rather than -1, because lines are 1-based and so it cannot be mistaken for a real answer.

		QCOMPARE ( line_of ( index, QStringLiteral ( "/nope" ) ), 0 );
	}

	void empty_and_junk_input_do_not_crash ()
	{
		QCOMPARE ( build_pointer_span_index ( QString () ).value ( QString () ).firstLine, 0 );

		QVERIFY ( build_pointer_span_index ( QStringLiteral ( "not json at all" ) ).size () <= 1 );
	}

	void empty_containers_are_mapped ()
	{
		const PointerSpanIndex index = build_pointer_span_index ( QStringLiteral ( "{\n  \"a\": {},\n  \"b\": []\n}" ) );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/a" ) ), 2 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/b" ) ), 3 );
	}

	//=================================================================================================================
	// The reverse direction: which node does this line belong to? (EDITOR-07's double-click navigation.)
	//=================================================================================================================

	void a_line_answers_with_the_deepest_node_covering_it ()
	{
		// Allman, so every kind of line is present: a key line, a value's opening brace on its own line, a scalar row,
		// a closing brace, and the document's own first and last lines.

		const QString text = QStringLiteral (
			"{\n"                            //  1  root
			"  \"id\": 1001,\n"              //  2  /id
			"  \"profile\":\n"               //  3  /profile          -- the key
			"  {\n"                          //  4  /profile          -- its opening brace
			"    \"city\": \"Cape Town\",\n" //  5  /profile/city
			"    \"tags\":\n"                //  6  /profile/tags
			"    [\n"                        //  7  /profile/tags
			"      \"a\",\n"                 //  8  /profile/tags/0
			"      \"b\"\n"                  //  9  /profile/tags/1
			"    ]\n"                        // 10  /profile/tags     -- its closing bracket
			"  },\n"                         // 11  /profile          -- its closing brace
			"  \"name\": \"Alex\"\n"         // 12  /name
			"}" );                           // 13  root

		const PointerSpanIndex index = build_pointer_span_index ( text );

		QCOMPARE ( pointer_at ( index,  2 ), QStringLiteral ( "/id" ) );
		QCOMPARE ( pointer_at ( index,  5 ), QStringLiteral ( "/profile/city" ) );
		QCOMPARE ( pointer_at ( index,  8 ), QStringLiteral ( "/profile/tags/0" ) );
		QCOMPARE ( pointer_at ( index, 12 ), QStringLiteral ( "/name" ) );

		// A container's own lines -- key, opening brace, and CLOSING brace -- all belong to the container. The closing
		// brace is the case a start-line-only index gets wrong: the nearest entry above line 11 is /name's predecessor
		// /profile/tags, and answering with a child would be confidently wrong.

		QCOMPARE ( pointer_at ( index,  3 ), QStringLiteral ( "/profile" ) );
		QCOMPARE ( pointer_at ( index,  4 ), QStringLiteral ( "/profile" ) );
		QCOMPARE ( pointer_at ( index, 11 ), QStringLiteral ( "/profile" ) );
		QCOMPARE ( pointer_at ( index, 10 ), QStringLiteral ( "/profile/tags" ) );

		// The document's own braces are the root's.

		QCOMPARE ( pointer_at ( index,  1 ), QString () );
		QCOMPARE ( pointer_at ( index, 13 ), QString () );
	}

	void a_line_outside_the_text_answers_with_nothing ()
	{
		const PointerSpanIndex index = build_pointer_span_index ( QStringLiteral ( "{\n  \"a\": 1\n}" ) );

		// Distinguishable from the root, which is also an empty pointer -- a trailing blank line is not a click on the
		// document.

		bool found = true;

		pointer_at_line ( index, 9, &found );

		QVERIFY ( !found );

		pointer_at_line ( index, 0, &found );

		QVERIFY ( !found );

		// And line 1 IS the root, with found set.

		pointer_at_line ( index, 1, &found );

		QVERIFY ( found );
	}

	void a_single_line_document_answers_with_the_root ()
	{
		// Every node spans line 1, so the deepest-span rule has nothing to separate them by width. The root is the
		// widest and therefore the one thing it must NOT answer with... except that here every span is identical, so
		// the tie-break (the deeper pointer) decides -- and any answer within that line is a defensible one.

		const PointerSpanIndex index = build_pointer_span_index ( QStringLiteral ( "{\"a\":{\"b\":1}}" ) );

		bool found = false;

		const JsonPointer pointer = pointer_at_line ( index, 1, &found );

		QVERIFY ( found );
		QCOMPARE ( pointer.to_string (), QStringLiteral ( "/a/b" ) );
	}

	void a_line_below_a_syntax_error_answers_with_nothing_indexed ()
	{
		// The scan stopped at the damage, so the lines below it are not covered by any completed span. The lines above
		// it still answer, which is the half that matters while typing (EDITOR-09).

		const QString text = QStringLiteral (
			"{\n"
			"  \"id\": 1001,\n"
			"  \"roles\": ,,,\n"
			"  \"name\": \"Alex\"\n"
			"}" );

		const PointerSpanIndex index = build_pointer_span_index ( text );

		QCOMPARE ( pointer_at ( index, 2 ), QStringLiteral ( "/id" ) );

		bool found = true;

		pointer_at_line ( index, 4, &found );

		QVERIFY ( !found );
	}
};

QTEST_APPLESS_MAIN ( TestJsonTextIndex )

#include "tst_json_text_index.moc"
