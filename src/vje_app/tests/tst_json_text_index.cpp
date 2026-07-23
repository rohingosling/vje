//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for json_text_index -- the pointer -> line map the Code View reveals nodes
//   through (EDITOR-07).
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
	int line_of ( const PointerLineIndex& index, const QString& pointerText )
	{
		return line_for_pointer ( index, JsonPointer::parse ( pointerText ) );
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

		const PointerLineIndex index = build_pointer_line_index ( text );

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

		const PointerLineIndex index = build_pointer_line_index ( text );

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

		const PointerLineIndex index = build_pointer_line_index ( text );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/projects/0" ) ),      3 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/projects/0/name" ) ), 3 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/projects/1/name" ) ), 4 );
	}

	//=================================================================================================================
	// It describes the TEXT, not a format profile.
	//=================================================================================================================

	void a_single_line_document_maps_every_node_to_line_one ()
	{
		const PointerLineIndex index = build_pointer_line_index ( QStringLiteral ( "{\"a\":{\"b\":[1,2]}}" ) );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/a" ) ),     1 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/a/b/1" ) ), 1 );
	}

	void escaped_keys_map_to_decoded_pointer_tokens ()
	{
		// "a/b" is written "a~1b" in a pointer, and the index is keyed by the pointer's own text form -- so the
		// encoding has to happen exactly once, in JsonPointer, and not again here.

		const PointerLineIndex index = build_pointer_line_index ( QStringLiteral ( "{\n  \"a/b\": 1\n}" ) );

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

		const PointerLineIndex index = build_pointer_line_index ( text );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/id" ) ),   2 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/name" ) ), 3 );
	}

	void an_unknown_pointer_answers_zero ()
	{
		const PointerLineIndex index = build_pointer_line_index ( QStringLiteral ( "{ \"a\": 1 }" ) );

		// 0 rather than -1, because lines are 1-based and so it cannot be mistaken for a real answer.

		QCOMPARE ( line_of ( index, QStringLiteral ( "/nope" ) ), 0 );
	}

	void empty_and_junk_input_do_not_crash ()
	{
		QCOMPARE ( build_pointer_line_index ( QString () ).value ( QString (), 0 ), 0 );

		QVERIFY ( build_pointer_line_index ( QStringLiteral ( "not json at all" ) ).size () <= 1 );
	}

	void empty_containers_are_mapped ()
	{
		const PointerLineIndex index = build_pointer_line_index ( QStringLiteral ( "{\n  \"a\": {},\n  \"b\": []\n}" ) );

		QCOMPARE ( line_of ( index, QStringLiteral ( "/a" ) ), 2 );
		QCOMPARE ( line_of ( index, QStringLiteral ( "/b" ) ), 3 );
	}
};

QTEST_APPLESS_MAIN ( TestJsonTextIndex )

#include "tst_json_text_index.moc"
