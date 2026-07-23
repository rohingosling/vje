//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for JsonPointer (RFC 6901): parsing and round-tripping text, token escaping (~0 / ~1),
//   resolution through objects and arrays, canonical array-index rules, and failure paths.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

using namespace vje;

class TestJsonPointer : public QObject
{
	Q_OBJECT

private:

	static std::unique_ptr<JsonNode> sample_tree ();

private slots:

	void root_pointer ();
	void parse_and_to_string_round_trip ();
	void token_escaping ();
	void rejects_text_without_leading_slash ();
	void resolves_into_objects_and_arrays ();
	void rejects_non_canonical_index ();
	void unresolvable_paths_return_null ();
	void first_match_on_duplicate_keys ();
	void child_and_parent ();
};

std::unique_ptr<JsonNode> TestJsonPointer::sample_tree ()
{
	// { "projects": [ { "name": "vje" } ], "empty": {} }

	auto root = JsonNode::make_object ();

	auto projects = JsonNode::make_array ();
	auto project  = JsonNode::make_object ();
	project->append_member ( "name", JsonNode::make_string ( "vje" ) );
	projects->append_element ( std::move ( project ) );

	root->append_member ( "projects", std::move ( projects ) );
	root->append_member ( "empty", JsonNode::make_object () );

	return root;
}

void TestJsonPointer::root_pointer ()
{
	JsonPointer root;

	QVERIFY  ( root.is_root () );
	QCOMPARE ( root.token_count (), 0 );
	QCOMPARE ( root.to_string (), QString () );

	auto tree = sample_tree ();
	QCOMPARE ( root.resolve ( tree.get () ), tree.get () );
}

void TestJsonPointer::parse_and_to_string_round_trip ()
{
	bool ok = false;
	JsonPointer pointer = JsonPointer::parse ( "/projects/0/name", &ok );

	QVERIFY  ( ok );
	QCOMPARE ( pointer.token_count (), 3 );
	QCOMPARE ( pointer.token ( 0 ), QStringLiteral ( "projects" ) );
	QCOMPARE ( pointer.token ( 1 ), QStringLiteral ( "0" ) );
	QCOMPARE ( pointer.token ( 2 ), QStringLiteral ( "name" ) );
	QCOMPARE ( pointer.to_string (), QStringLiteral ( "/projects/0/name" ) );
}

void TestJsonPointer::token_escaping ()
{
	bool ok = false;
	JsonPointer pointer = JsonPointer::parse ( "/a~1b/c~0d", &ok );

	QVERIFY  ( ok );
	QCOMPARE ( pointer.token ( 0 ), QStringLiteral ( "a/b" ) );
	QCOMPARE ( pointer.token ( 1 ), QStringLiteral ( "c~d" ) );

	// Re-encoding restores the escaped form.

	QCOMPARE ( pointer.to_string (), QStringLiteral ( "/a~1b/c~0d" ) );
}

void TestJsonPointer::rejects_text_without_leading_slash ()
{
	bool ok = true;
	JsonPointer pointer = JsonPointer::parse ( "projects/0", &ok );

	QVERIFY ( !ok );
	QVERIFY ( pointer.is_root () );
}

void TestJsonPointer::resolves_into_objects_and_arrays ()
{
	auto tree = sample_tree ();

	JsonNode* name = JsonPointer::parse ( "/projects/0/name" ).resolve ( tree.get () );

	QVERIFY  ( name != nullptr );
	QCOMPARE ( name->kind (), JsonKind::String );
	QCOMPARE ( name->string_value (), QStringLiteral ( "vje" ) );
}

void TestJsonPointer::rejects_non_canonical_index ()
{
	auto tree = sample_tree ();

	// "01" has a leading zero; "-" and "x" are not indices.

	QVERIFY ( JsonPointer::parse ( "/projects/01/name" ).resolve ( tree.get () ) == nullptr );
	QVERIFY ( JsonPointer::parse ( "/projects/-/name" ).resolve ( tree.get () ) == nullptr );
	QVERIFY ( JsonPointer::parse ( "/projects/x" ).resolve ( tree.get () ) == nullptr );
}

void TestJsonPointer::unresolvable_paths_return_null ()
{
	auto tree = sample_tree ();

	QVERIFY ( JsonPointer::parse ( "/missing" ).resolve ( tree.get () ) == nullptr );
	QVERIFY ( JsonPointer::parse ( "/projects/5" ).resolve ( tree.get () ) == nullptr );

	// Descending into a scalar fails.

	QVERIFY ( JsonPointer::parse ( "/projects/0/name/x" ).resolve ( tree.get () ) == nullptr );
}

void TestJsonPointer::first_match_on_duplicate_keys ()
{
	auto object = JsonNode::make_object ();
	object->append_member ( "a", JsonNode::make_number ( "1" ) );
	object->append_member ( "a", JsonNode::make_number ( "2" ) );

	JsonNode* resolved = JsonPointer::parse ( "/a" ).resolve ( object.get () );

	QVERIFY  ( resolved != nullptr );
	QCOMPARE ( resolved->number_token (), QStringLiteral ( "1" ) );
}

void TestJsonPointer::child_and_parent ()
{
	JsonPointer pointer = JsonPointer::parse ( "/projects" ).child ( "0" ).child ( "name" );
	QCOMPARE ( pointer.to_string (), QStringLiteral ( "/projects/0/name" ) );

	QCOMPARE ( pointer.parent ().to_string (), QStringLiteral ( "/projects/0" ) );

	JsonPointer root;
	QVERIFY ( root.parent ().is_root () );
}

QTEST_APPLESS_MAIN ( TestJsonPointer )

#include "tst_json_pointer.moc"
