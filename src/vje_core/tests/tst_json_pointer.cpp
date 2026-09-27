//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include <vje_core/services/JsonParser.hpp>
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
	void a_duplicated_key_is_disambiguated_by_position ();
	void a_unique_key_records_nothing_so_equality_is_unchanged ();
	void a_stale_position_falls_back_to_the_first_match ();
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

//---------------------------------------------------------------------------------------------------------------------
// Duplicate sibling keys (VAL-02 / FILE-04)
//---------------------------------------------------------------------------------------------------------------------

void TestJsonPointer::a_duplicated_key_is_disambiguated_by_position ()
{
	// An object may carry the same key twice and a JSON Pointer names only the FIRST -- so every command routed by
	// pointer acted on the first whatever the user had selected, and deleting the second `name` deleted the first
	// (reported 2026-08-22). from_node knows the node's real position, so the pointer it builds says which one.

	ParseResult parsed = JsonParser::parse ( QStringLiteral ( R"({"o":{"name":[1],"name":[2,3]}})" ) );

	QVERIFY ( parsed.ok );

	JsonNode* const object = parsed.root->find_member ( QStringLiteral ( "o" ) );

	QCOMPARE ( object->member_count (), 2 );

	const JsonPointer first  = JsonPointer::from_node ( object->member_value ( 0 ) );
	const JsonPointer second = JsonPointer::from_node ( object->member_value ( 1 ) );

	// Identical RFC 6901 text -- which is the ambiguity, and it is the RFC's rather than something invented here. So
	// Copy JSON Pointer still produces text Go To accepts, and neither is changed by this.

	QCOMPARE ( first.to_string (), QStringLiteral ( "/o/name" ) );
	QCOMPARE ( second.to_string (), first.to_string () );

	// ... and they are nevertheless different pointers, because they name different nodes.

	QVERIFY ( first != second );

	QCOMPARE ( first.resolve  ( parsed.root.get () ), object->member_value ( 0 ) );
	QCOMPARE ( second.resolve ( parsed.root.get () ), object->member_value ( 1 ) );

	// A pointer PARSED from that text resolves to the first, which is what the RFC says and what Go To must keep
	// doing -- the disambiguator is for the routes that know a position, not a change to the notation.

	QCOMPARE ( JsonPointer::parse ( QStringLiteral ( "/o/name" ) ).resolve ( parsed.root.get () ),
	           object->member_value ( 0 ) );
}

void TestJsonPointer::a_unique_key_records_nothing_so_equality_is_unchanged ()
{
	// THE HALF THAT PROTECTS EVERYTHING ELSE. Go To, Find, the tree's expansion restore and every echo guard compare
	// pointers, and a from_node pointer that recorded a position for an ORDINARY member would compare unequal to the
	// same pointer parsed from text -- breaking all of them at once, silently, on documents with no duplicates at
	// all. So an occurrence is recorded only where the key is actually duplicated.

	ParseResult parsed = JsonParser::parse ( QStringLiteral ( R"({"o":{"a":1,"b":2}})" ) );

	JsonNode* const object = parsed.root->find_member ( QStringLiteral ( "o" ) );

	const JsonPointer built  = JsonPointer::from_node ( object->member_value ( 1 ) );
	const JsonPointer parsedPointer = JsonPointer::parse ( QStringLiteral ( "/o/b" ) );

	QVERIFY ( !built.has_occurrences () );
	QCOMPARE ( built, parsedPointer );

	// And the same node in a document that DOES duplicate a different key stays clean too -- the rule is per token.

	ParseResult mixed = JsonParser::parse ( QStringLiteral ( R"({"o":{"a":1,"a":2,"b":3}})" ) );

	JsonNode* const mixedObject = mixed.root->find_member ( QStringLiteral ( "o" ) );

	QVERIFY ( !JsonPointer::from_node ( mixedObject->member_value ( 2 ) ).has_occurrences () );
	QVERIFY (  JsonPointer::from_node ( mixedObject->member_value ( 1 ) ).has_occurrences () );
}

void TestJsonPointer::a_stale_position_falls_back_to_the_first_match ()
{
	// An occurrence is a SNAPSHOT of where the member sat, and an edit can move it. Resolving by position blindly
	// would then confidently return the wrong node -- which is the failure this mechanism exists to prevent, arriving
	// from the fix itself. The key is verified against the position before it is trusted, and where it no longer
	// matches the answer degrades to the RFC's own.

	ParseResult parsed = JsonParser::parse ( QStringLiteral ( R"({"o":{"name":1,"name":2}})" ) );

	JsonNode* const object = parsed.root->find_member ( QStringLiteral ( "o" ) );

	const JsonPointer second = JsonPointer::from_node ( object->member_value ( 1 ) );

	QCOMPARE ( second.resolve ( parsed.root.get () ), object->member_value ( 1 ) );

	// Now a DIFFERENT document, where position 1 holds something else entirely.

	ParseResult moved = JsonParser::parse ( QStringLiteral ( R"({"o":{"name":1,"other":2}})" ) );

	JsonNode* const movedObject = moved.root->find_member ( QStringLiteral ( "o" ) );

	QCOMPARE ( second.resolve ( moved.root.get () ), movedObject->member_value ( 0 ) );

	// And a position past the end resolves rather than crashing.

	ParseResult shrunk = JsonParser::parse ( QStringLiteral ( R"({"o":{"name":1}})" ) );

	QCOMPARE ( second.resolve ( shrunk.root.get () ),
	           shrunk.root->find_member ( QStringLiteral ( "o" ) )->member_value ( 0 ) );
}

#include "tst_json_pointer.moc"
