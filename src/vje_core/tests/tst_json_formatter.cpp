//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Golden coverage for JsonFormatter, the document format profile (SET-07 / FILE-03): K&R vs Allman brace styles,
//   spaces vs tabs indentation and size, align-name-separators, inline empty containers, verbatim number tokens
//   (FILE-10), and re-parse identity (format then parse reproduces the model).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	std::unique_ptr<JsonNode> parse ( const QString& json )
	{
		ParseResult result = JsonParser::parse ( json );
		return std::move ( result.root );
	}
}

class TestJsonFormatter : public QObject
{
	Q_OBJECT

private slots:

	void allman_is_the_default ();
	void kandr_hugs_the_key ();
	void empty_containers_render_inline ();
	void tabs_indentation ();
	void indent_size_four ();
	void align_name_separators ();
	void number_tokens_verbatim ();
	void top_level_array ();
	void scalar_root ();
	void reparse_identity_data ();
	void reparse_identity ();
};

void TestJsonFormatter::allman_is_the_default ()
{
	std::unique_ptr<JsonNode> root = parse ( QStringLiteral (
		"{\"id\":7,\"profile\":{\"a\":1},\"roles\":[\"x\",\"y\"]}" ) );

	const QString expected = QStringLiteral (
		"{\n"
		"  \"id\": 7,\n"
		"  \"profile\":\n"
		"  {\n"
		"    \"a\": 1\n"
		"  },\n"
		"  \"roles\":\n"
		"  [\n"
		"    \"x\",\n"
		"    \"y\"\n"
		"  ]\n"
		"}" );

	QCOMPARE ( JsonFormatter::format ( *root, FormatProfile () ), expected );
}

void TestJsonFormatter::kandr_hugs_the_key ()
{
	std::unique_ptr<JsonNode> root = parse ( QStringLiteral (
		"{\"id\":7,\"profile\":{\"a\":1},\"roles\":[\"x\",\"y\"]}" ) );

	FormatProfile profile;
	profile.braceStyle = BraceStyle::KAndR;

	const QString expected = QStringLiteral (
		"{\n"
		"  \"id\": 7,\n"
		"  \"profile\": {\n"
		"    \"a\": 1\n"
		"  },\n"
		"  \"roles\": [\n"
		"    \"x\",\n"
		"    \"y\"\n"
		"  ]\n"
		"}" );

	QCOMPARE ( JsonFormatter::format ( *root, profile ), expected );
}

void TestJsonFormatter::empty_containers_render_inline ()
{
	std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"empty\":{},\"list\":[]}" ) );

	// Inline in both styles.

	QCOMPARE ( JsonFormatter::format ( *root, FormatProfile () ),
	           QStringLiteral ( "{\n  \"empty\": {},\n  \"list\": []\n}" ) );

	FormatProfile kandr;
	kandr.braceStyle = BraceStyle::KAndR;

	QCOMPARE ( JsonFormatter::format ( *root, kandr ),
	           QStringLiteral ( "{\n  \"empty\": {},\n  \"list\": []\n}" ) );
}

void TestJsonFormatter::tabs_indentation ()
{
	std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"a\":{\"b\":1}}" ) );

	FormatProfile profile;
	profile.indent     = IndentKind::Tabs;
	profile.braceStyle = BraceStyle::KAndR;

	QCOMPARE ( JsonFormatter::format ( *root, profile ),
	           QStringLiteral ( "{\n\t\"a\": {\n\t\t\"b\": 1\n\t}\n}" ) );
}

void TestJsonFormatter::indent_size_four ()
{
	std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"a\":1}" ) );

	FormatProfile profile;
	profile.indentSize = 4;

	QCOMPARE ( JsonFormatter::format ( *root, profile ), QStringLiteral ( "{\n    \"a\": 1\n}" ) );
}

void TestJsonFormatter::align_name_separators ()
{
	std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"id\":7,\"description\":\"x\"}" ) );

	FormatProfile profile;
	profile.braceStyle          = BraceStyle::KAndR;
	profile.alignNameSeparators = true;

	QCOMPARE ( JsonFormatter::format ( *root, profile ),
	           QStringLiteral ( "{\n  \"id\"         : 7,\n  \"description\": \"x\"\n}" ) );
}

void TestJsonFormatter::number_tokens_verbatim ()
{
	std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "[1.0,1e3,-0.50,100000000000000000000]" ) );

	QCOMPARE ( JsonFormatter::format ( *root, FormatProfile () ),
	           QStringLiteral ( "[\n  1.0,\n  1e3,\n  -0.50,\n  100000000000000000000\n]" ) );
}

void TestJsonFormatter::top_level_array ()
{
	std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "[{\"a\":1},{\"a\":2}]" ) );

	// An array element's container brace opens at the element indent in BOTH styles (no key to hug).

	const QString expected = QStringLiteral (
		"[\n"
		"  {\n"
		"    \"a\": 1\n"
		"  },\n"
		"  {\n"
		"    \"a\": 2\n"
		"  }\n"
		"]" );

	QCOMPARE ( JsonFormatter::format ( *root, FormatProfile () ), expected );

	FormatProfile kandr;
	kandr.braceStyle = BraceStyle::KAndR;

	QCOMPARE ( JsonFormatter::format ( *root, kandr ), expected );
}

void TestJsonFormatter::scalar_root ()
{
	QCOMPARE ( JsonFormatter::format ( *JsonNode::make_number ( QStringLiteral ( "42" ) ), FormatProfile () ),
	           QStringLiteral ( "42" ) );
	QCOMPARE ( JsonFormatter::format ( *JsonNode::make_string ( QStringLiteral ( "hi" ) ), FormatProfile () ),
	           QStringLiteral ( "\"hi\"" ) );
}

void TestJsonFormatter::reparse_identity_data ()
{
	QTest::addColumn<QString> ( "json" );

	QTest::newRow ( "nested" )   << QStringLiteral ( "{\"a\":[1,{\"b\":true},null],\"c\":\"s\"}" );
	QTest::newRow ( "numbers" )  << QStringLiteral ( "[1.0,1e3,-0.5,0]" );
	QTest::newRow ( "duplicate" )<< QStringLiteral ( "{\"a\":1,\"a\":2}" );
}

void TestJsonFormatter::reparse_identity ()
{
	QFETCH ( QString, json );

	std::unique_ptr<JsonNode> root = parse ( json );

	// Every profile variation is still valid JSON that re-parses to the same model (member order + number tokens).

	const FormatProfile profiles [] = {
		FormatProfile (),
		[] { FormatProfile p; p.braceStyle = BraceStyle::KAndR; return p; } (),
		[] { FormatProfile p; p.indent = IndentKind::Tabs; return p; } (),
		[] { FormatProfile p; p.alignNameSeparators = true; return p; } ()
	};

	for ( const FormatProfile& profile : profiles )
	{
		std::unique_ptr<JsonNode> reparsed = parse ( JsonFormatter::format ( *root, profile ) );

		QVERIFY ( reparsed != nullptr );
		QVERIFY ( root->equals ( *reparsed ) );
	}
}

QTEST_APPLESS_MAIN ( TestJsonFormatter )

#include "tst_json_formatter.moc"
