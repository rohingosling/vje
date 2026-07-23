//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for JsonParser: valid parses with member-order (FILE-04) and raw-number-token (FILE-10)
//   fidelity, string-escape decoding, duplicate-key detection (VAL-02 / SET-03), malformed-input error positions
//   (FILE-06), trailing content, and the nesting-depth guard.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

using namespace vje;

class TestJsonParser : public QObject
{
	Q_OBJECT

private slots:

	void parses_a_nested_document ();
	void preserves_member_order ();
	void preserves_number_tokens ();
	void decodes_string_escapes ();
	void detects_and_keeps_duplicate_keys ();
	void reports_error_position ();
	void rejects_empty_input ();
	void rejects_trailing_content ();
	void enforces_depth_limit ();
};

void TestJsonParser::parses_a_nested_document ()
{
	ParseResult result = JsonParser::parse ( R"({"a":1,"b":[true,null,"x"]})" );

	QVERIFY  ( result.ok );
	QVERIFY  ( result.root != nullptr );
	QCOMPARE ( result.root->kind (), JsonKind::Object );
	QCOMPARE ( result.root->member_count (), 2 );

	JsonNode* b = result.root->find_member ( "b" );
	QVERIFY  ( b != nullptr );
	QCOMPARE ( b->kind (), JsonKind::Array );
	QCOMPARE ( b->array_size (), 3 );
	QCOMPARE ( b->array_element ( 0 )->kind (), JsonKind::Boolean );
	QCOMPARE ( b->array_element ( 1 )->kind (), JsonKind::Null );
	QCOMPARE ( b->array_element ( 2 )->string_value (), QStringLiteral ( "x" ) );
}

void TestJsonParser::preserves_member_order ()
{
	ParseResult result = JsonParser::parse ( R"({"z":1,"a":2,"m":3})" );

	QVERIFY  ( result.ok );
	QCOMPARE ( result.root->member_key ( 0 ), QStringLiteral ( "z" ) );
	QCOMPARE ( result.root->member_key ( 1 ), QStringLiteral ( "a" ) );
	QCOMPARE ( result.root->member_key ( 2 ), QStringLiteral ( "m" ) );
}

void TestJsonParser::preserves_number_tokens ()
{
	ParseResult result = JsonParser::parse ( "[1, 1.0, 1e3, -0.5, 123456789012345678901234567890]" );

	QVERIFY  ( result.ok );
	QCOMPARE ( result.root->array_element ( 0 )->number_token (), QStringLiteral ( "1" ) );
	QCOMPARE ( result.root->array_element ( 1 )->number_token (), QStringLiteral ( "1.0" ) );
	QCOMPARE ( result.root->array_element ( 2 )->number_token (), QStringLiteral ( "1e3" ) );
	QCOMPARE ( result.root->array_element ( 3 )->number_token (), QStringLiteral ( "-0.5" ) );
	QCOMPARE ( result.root->array_element ( 4 )->number_token (),
	           QStringLiteral ( "123456789012345678901234567890" ) );
}

void TestJsonParser::decodes_string_escapes ()
{
	ParseResult result = JsonParser::parse ( R"("a\nb\tcA\"")" );

	QVERIFY  ( result.ok );
	QCOMPARE ( result.root->kind (), JsonKind::String );
	QCOMPARE ( result.root->string_value (), QStringLiteral ( "a\nb\tcA\"" ) );
}

void TestJsonParser::detects_and_keeps_duplicate_keys ()
{
	ParseResult result = JsonParser::parse ( R"({"a":1,"a":2})" );

	QVERIFY  ( result.ok );                                  // Duplicates do not fail the parse.
	QCOMPARE ( result.root->member_count (), 2 );            // Both members are kept.
	QCOMPARE ( result.root->key_count ( "a" ), 2 );

	QCOMPARE ( result.duplicateKeys.size (), std::size_t ( 1 ) );
	QCOMPARE ( result.duplicateKeys [ 0 ].key, QStringLiteral ( "a" ) );
	QVERIFY  ( result.duplicateKeys [ 0 ].objectPointer.is_root () );
}

void TestJsonParser::reports_error_position ()
{
	ParseResult result = JsonParser::parse ( "{\n  \"a\": ,\n}" );

	QVERIFY  ( !result.ok );
	QVERIFY  ( result.root == nullptr );
	QVERIFY  ( !result.error.message.isEmpty () );

	// The offending ',' is on line 2.

	QCOMPARE ( result.error.line, 2 );
	QVERIFY  ( result.error.column > 0 );
}

void TestJsonParser::rejects_empty_input ()
{
	ParseResult result = JsonParser::parse ( "   " );

	QVERIFY ( !result.ok );
	QVERIFY ( result.error.message.contains ( QStringLiteral ( "end of input" ) ) );
}

void TestJsonParser::rejects_trailing_content ()
{
	ParseResult result = JsonParser::parse ( "1 2" );

	QVERIFY ( !result.ok );
	QVERIFY ( result.error.message.contains ( QStringLiteral ( "trailing" ) ) );
}

void TestJsonParser::enforces_depth_limit ()
{
	// 600 nested arrays exceeds MAX_DEPTH (512); the parser reports rather than overflowing the stack.

	const QString deep = QString ( 600, QLatin1Char ( '[' ) ) + QString ( 600, QLatin1Char ( ']' ) );

	ParseResult result = JsonParser::parse ( deep );

	QVERIFY ( !result.ok );
	QVERIFY ( result.error.message.contains ( QStringLiteral ( "depth" ) ) );
}

QTEST_APPLESS_MAIN ( TestJsonParser )

#include "tst_json_parser.moc"
