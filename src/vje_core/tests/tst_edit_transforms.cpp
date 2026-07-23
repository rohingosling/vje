//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for the pure edit transforms (editing/edit_transforms): the JSON-number check, the EDIT-09
//   type-conversion matrix, the three array transforms (EDIT-11..13, including normalize idempotence), and the
//   EDIT-07 duplicate-key naming rule. These are side-effect-free node computations, so they are asserted directly
//   against serialized output without any document or undo stack.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/edit_transforms.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/services/JsonSerializer.hpp>

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	// Parse a JSON snippet to a node (the test inputs are all well-formed).

	std::unique_ptr<JsonNode> node_of ( const QString& json )
	{
		ParseResult result = JsonParser::parse ( json );
		Q_ASSERT ( result.ok );
		return std::move ( result.root );
	}

	QString serialize ( const JsonNode& node )
	{
		return JsonSerializer::serialize ( node );
	}
}

class TestEditTransforms : public QObject
{
	Q_OBJECT

private slots:

	void is_json_number_recognizes_whole_number_tokens_only ();
	void is_json_number_recognizes_whole_number_tokens_only_data ();

	void convert_node_follows_the_edit09_matrix ();
	void convert_node_follows_the_edit09_matrix_data ();

	void convert_to_same_kind_clones ();

	void normalize_unions_keys_in_first_encountered_order ();
	void normalize_is_idempotent ();

	void array_to_object_keys_by_index ();
	void object_to_array_drops_keys ();

	void duplicate_key_name_avoids_collisions ();
};

void TestEditTransforms::is_json_number_recognizes_whole_number_tokens_only_data ()
{
	QTest::addColumn<QString> ( "text" );
	QTest::addColumn<bool>    ( "expected" );

	QTest::newRow ( "zero" )          << "0"          << true;
	QTest::newRow ( "integer" )       << "42"         << true;
	QTest::newRow ( "negative" )      << "-17"        << true;
	QTest::newRow ( "fraction" )      << "3.14"       << true;
	QTest::newRow ( "exponent" )      << "-1.5e10"    << true;
	QTest::newRow ( "leading-zero" )  << "01"         << false;
	QTest::newRow ( "trailing-dot" )  << "1."         << false;
	QTest::newRow ( "not-a-number" )  << "hello"      << false;
	QTest::newRow ( "empty" )         << ""           << false;
	QTest::newRow ( "leading-space" ) << " 1"         << false;
	QTest::newRow ( "trailing-space" )<< "1 "         << false;
	QTest::newRow ( "trailing-junk" ) << "1x"         << false;
}

void TestEditTransforms::is_json_number_recognizes_whole_number_tokens_only ()
{
	QFETCH ( QString, text );
	QFETCH ( bool,    expected );

	QCOMPARE ( edit_transforms::is_json_number ( text ), expected );
}

void TestEditTransforms::convert_node_follows_the_edit09_matrix_data ()
{
	QTest::addColumn<QString> ( "source" );      // A JSON scalar/container to convert.
	QTest::addColumn<int>     ( "targetKind" );  // JsonKind as int.
	QTest::addColumn<QString> ( "expected" );    // Serialized result.

	const int toString  = static_cast<int> ( JsonKind::String );
	const int toNumber  = static_cast<int> ( JsonKind::Number );
	const int toBoolean = static_cast<int> ( JsonKind::Boolean );
	const int toNull    = static_cast<int> ( JsonKind::Null );
	const int toArray   = static_cast<int> ( JsonKind::Array );
	const int toObject  = static_cast<int> ( JsonKind::Object );

	// To string: raw number token / "true"/"false" / null and containers collapse to "".

	QTest::newRow ( "number->string" )  << "3.14"        << toString  << "\"3.14\"";
	QTest::newRow ( "true->string" )    << "true"        << toString  << "\"true\"";
	QTest::newRow ( "null->string" )    << "null"        << toString  << "\"\"";
	QTest::newRow ( "object->string" )  << "{\"a\":1}"   << toString  << "\"\"";

	// To number: valid numeric string verbatim; true/false -> 1/0; else 0.

	QTest::newRow ( "numstr->number" )  << "\"42\""      << toNumber  << "42";
	QTest::newRow ( "text->number" )    << "\"hello\""   << toNumber  << "0";
	QTest::newRow ( "true->number" )    << "true"        << toNumber  << "1";
	QTest::newRow ( "false->number" )   << "false"       << toNumber  << "0";
	QTest::newRow ( "null->number" )    << "null"        << toNumber  << "0";

	// To boolean: exact "true"/"false"; number non-zero -> true; else false.

	QTest::newRow ( "truestr->bool" )   << "\"true\""    << toBoolean << "true";
	QTest::newRow ( "falsestr->bool" )  << "\"false\""   << toBoolean << "false";
	QTest::newRow ( "text->bool" )      << "\"maybe\""   << toBoolean << "false";
	QTest::newRow ( "nonzero->bool" )   << "5"           << toBoolean << "true";
	QTest::newRow ( "zero->bool" )      << "0"           << toBoolean << "false";
	QTest::newRow ( "zerofrac->bool" )  << "0.0"         << toBoolean << "false";

	// To null / empty containers.

	QTest::newRow ( "number->null" )    << "42"          << toNull    << "null";
	QTest::newRow ( "string->array" )   << "\"x\""       << toArray   << "[]";
	QTest::newRow ( "string->object" )  << "\"x\""       << toObject  << "{}";
}

void TestEditTransforms::convert_node_follows_the_edit09_matrix ()
{
	QFETCH ( QString, source );
	QFETCH ( int,     targetKind );
	QFETCH ( QString, expected );

	std::unique_ptr<JsonNode> node    = node_of ( source );
	std::unique_ptr<JsonNode> convert = edit_transforms::convert_node ( *node, static_cast<JsonKind> ( targetKind ) );

	QCOMPARE ( serialize ( *convert ), expected );
}

void TestEditTransforms::convert_to_same_kind_clones ()
{
	std::unique_ptr<JsonNode> node    = node_of ( "\"keep me\"" );
	std::unique_ptr<JsonNode> convert = edit_transforms::convert_node ( *node, JsonKind::String );

	QVERIFY  ( convert.get () != node.get () );            // A fresh node, not the original.
	QCOMPARE ( serialize ( *convert ), QStringLiteral ( "\"keep me\"" ) );
}

void TestEditTransforms::normalize_unions_keys_in_first_encountered_order ()
{
	std::unique_ptr<JsonNode> array = node_of ( R"([{"a":1},{"b":2},{"a":3,"c":4}])" );
	std::unique_ptr<JsonNode> out   = edit_transforms::normalize_array_elements ( *array );

	QCOMPARE ( serialize ( *out ), QStringLiteral ( R"([{"a":1,"b":null,"c":null},{"a":null,"b":2,"c":null},{"a":3,"b":null,"c":4}])" ) );
}

void TestEditTransforms::normalize_is_idempotent ()
{
	std::unique_ptr<JsonNode> array = node_of ( R"([{"a":1},{"b":2},{"d":4,"enabled":true}])" );

	std::unique_ptr<JsonNode> once  = edit_transforms::normalize_array_elements ( *array );
	std::unique_ptr<JsonNode> twice = edit_transforms::normalize_array_elements ( *once );

	QCOMPARE ( serialize ( *twice ), serialize ( *once ) );
}

void TestEditTransforms::array_to_object_keys_by_index ()
{
	std::unique_ptr<JsonNode> array = node_of ( "[10,20,30]" );
	std::unique_ptr<JsonNode> out   = edit_transforms::array_to_object ( *array );

	QCOMPARE ( serialize ( *out ), QStringLiteral ( R"({"0":10,"1":20,"2":30})" ) );
}

void TestEditTransforms::object_to_array_drops_keys ()
{
	std::unique_ptr<JsonNode> object = node_of ( R"({"x":1,"y":2})" );
	std::unique_ptr<JsonNode> out    = edit_transforms::object_to_array ( *object );

	QCOMPARE ( serialize ( *out ), QStringLiteral ( "[1,2]" ) );
}

void TestEditTransforms::duplicate_key_name_avoids_collisions ()
{
	std::unique_ptr<JsonNode> object = node_of ( R"json({"k":1,"k (copy)":2,"k (copy 2)":3})json" );

	// "k" -> first free is "k (copy 3)"; a never-seen base gets the plain "(copy)" suffix.

	QCOMPARE ( edit_transforms::duplicate_key_name ( *object, QStringLiteral ( "k" ) ),     QStringLiteral ( "k (copy 3)" ) );
	QCOMPARE ( edit_transforms::duplicate_key_name ( *object, QStringLiteral ( "fresh" ) ), QStringLiteral ( "fresh (copy)" ) );
}

QTEST_GUILESS_MAIN ( TestEditTransforms )

#include "tst_edit_transforms.moc"
