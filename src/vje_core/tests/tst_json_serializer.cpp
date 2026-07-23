//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for JsonSerializer, including the parse-serialize identity property: for canonical compact input, serialize(parse(text)) == text; and for
//   any input, parse(serialize(model))
//   is structurally equal to the model (member order and number tokens preserved).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonSerializer.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

using namespace vje;

class TestJsonSerializer : public QObject
{
	Q_OBJECT

private slots:

	void serializes_scalars ();
	void emits_number_tokens_verbatim ();
	void escapes_strings ();
	void canonical_round_trip_data ();
	void canonical_round_trip ();
	void preserves_duplicate_keys_through_round_trip ();
};

void TestJsonSerializer::serializes_scalars ()
{
	QCOMPARE ( JsonSerializer::serialize ( *JsonNode::make_null () ),          QStringLiteral ( "null" ) );
	QCOMPARE ( JsonSerializer::serialize ( *JsonNode::make_boolean ( true ) ), QStringLiteral ( "true" ) );
	QCOMPARE ( JsonSerializer::serialize ( *JsonNode::make_boolean ( false ) ),QStringLiteral ( "false" ) );
	QCOMPARE ( JsonSerializer::serialize ( *JsonNode::make_string ( "x" ) ),   QStringLiteral ( "\"x\"" ) );
}

void TestJsonSerializer::emits_number_tokens_verbatim ()
{
	QCOMPARE ( JsonSerializer::serialize ( *JsonNode::make_number ( "1.0" ) ), QStringLiteral ( "1.0" ) );
	QCOMPARE ( JsonSerializer::serialize ( *JsonNode::make_number ( "1e3" ) ), QStringLiteral ( "1e3" ) );
}

void TestJsonSerializer::escapes_strings ()
{
	// A value containing a newline, tab, quote, and backslash round-trips through the standard short escapes.

	QString value;
	value += QLatin1Char ( '\n' );
	value += QLatin1Char ( '\t' );
	value += QLatin1Char ( '"' );
	value += QLatin1Char ( '\\' );

	QCOMPARE ( JsonSerializer::encode_string ( value ), QStringLiteral ( "\"\\n\\t\\\"\\\\\"" ) );
}

void TestJsonSerializer::canonical_round_trip_data ()
{
	QTest::addColumn<QString> ( "json" );

	QTest::newRow ( "null" )        << QStringLiteral ( "null" );
	QTest::newRow ( "true" )        << QStringLiteral ( "true" );
	QTest::newRow ( "number" )      << QStringLiteral ( "-1.5e10" );
	QTest::newRow ( "string" )      << QStringLiteral ( "\"hello\"" );
	QTest::newRow ( "escaped" )     << QStringLiteral ( "\"a\\nb\"" );
	QTest::newRow ( "empty array" ) << QStringLiteral ( "[]" );
	QTest::newRow ( "empty object" )<< QStringLiteral ( "{}" );
	QTest::newRow ( "array" )       << QStringLiteral ( "[1,2,3]" );
	QTest::newRow ( "nested" )      << QStringLiteral ( "{\"a\":1,\"b\":[true,null]}" );
	QTest::newRow ( "order" )       << QStringLiteral ( "{\"z\":1,\"a\":2}" );
	QTest::newRow ( "duplicate" )   << QStringLiteral ( "{\"a\":1,\"a\":2}" );
}

void TestJsonSerializer::canonical_round_trip ()
{
	QFETCH ( QString, json );

	ParseResult result = JsonParser::parse ( json );
	QVERIFY ( result.ok );

	// Canonical input reproduces exactly.

	const QString serialized = JsonSerializer::serialize ( *result.root );
	QCOMPARE ( serialized, json );

	// And re-parsing the serialized text yields a structurally identical model.

	ParseResult reparsed = JsonParser::parse ( serialized );
	QVERIFY ( reparsed.ok );
	QVERIFY ( result.root->equals ( *reparsed.root ) );
}

void TestJsonSerializer::preserves_duplicate_keys_through_round_trip ()
{
	ParseResult result = JsonParser::parse ( R"({"a":1,"a":2})" );
	QVERIFY ( result.ok );

	const QString serialized = JsonSerializer::serialize ( *result.root );
	QCOMPARE ( serialized, QStringLiteral ( "{\"a\":1,\"a\":2}" ) );
}

QTEST_APPLESS_MAIN ( TestJsonSerializer )

#include "tst_json_serializer.moc"
