//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for YamlCodec (yaml-cpp, FILE-11): JSON -> YAML -> JSON round-trips preserving structure, member order,
//   raw number tokens (FILE-10), and -- critically -- the string/scalar distinction (a string that reads like a
//   number/boolean/null survives as a string because export double-quotes it). Also: importing ordinary third-party
//   YAML (plain scalars inferred, ~ as null) and malformed-input parity.
//
//   This suite is built only when yaml-cpp is available (VJE_ENABLE_YAML).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/convert/YamlCodec.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/services/JsonSerializer.hpp>
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

	QString canon ( const QString& json )
	{
		return JsonSerializer::serialize ( *parse ( json ) );
	}

	// Round-trip a JSON document through YAML and return the reimported canonical form.

	QString round_trip ( const QString& json )
	{
		const QString          yaml       = YamlCodec::to_yaml ( *parse ( json ) );
		YamlCodec::ImportResult reimported = YamlCodec::from_yaml ( yaml );

		return reimported.ok ? JsonSerializer::serialize ( *reimported.root ) : QStringLiteral ( "<error>" );
	}
}

class TestYamlCodec : public QObject
{
	Q_OBJECT

private slots:

	void round_trip_scalars_and_containers ();
	void round_trip_number_tokens ();
	void round_trip_stringly_typed_values ();
	void round_trip_nested ();
	void import_plain_yaml ();
	void malformed_is_an_error ();
};

void TestYamlCodec::round_trip_scalars_and_containers ()
{
	const QString document = QStringLiteral (
		"{\"name\":\"Alice\",\"age\":30,\"active\":true,\"note\":null,\"tags\":[\"x\",\"y\"]}" );

	QCOMPARE ( round_trip ( document ), canon ( document ) );
}

void TestYamlCodec::round_trip_number_tokens ()
{
	// The exact number lexeme must survive (FILE-10): 1.50 does not become 1.5, 1e3 does not become 1000.

	const QString document = QStringLiteral ( "{\"a\":1.50,\"b\":1e3,\"c\":-0,\"d\":9007199254740993}" );

	QCOMPARE ( round_trip ( document ), canon ( document ) );
}

void TestYamlCodec::round_trip_stringly_typed_values ()
{
	// Strings that look like other types stay strings -- export double-quotes them, import reads the quoted tag.

	const QString document = QStringLiteral ( "{\"s1\":\"42\",\"s2\":\"true\",\"s3\":\"null\",\"s4\":\"3.14\"}" );

	QCOMPARE ( round_trip ( document ), canon ( document ) );
}

void TestYamlCodec::round_trip_nested ()
{
	const QString document = QStringLiteral ( "{\"outer\":{\"inner\":[1,2,{\"k\":\"v\"}]},\"list\":[[1],[2,3]]}" );

	QCOMPARE ( round_trip ( document ), canon ( document ) );
}

void TestYamlCodec::import_plain_yaml ()
{
	// Ordinary hand-written YAML: plain scalars infer, ~ is null, document (map) order is preserved.

	YamlCodec::ImportResult result = YamlCodec::from_yaml ( QStringLiteral ( "key: value\nnum: 42\nflag: true\nempty: ~\n" ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( JsonSerializer::serialize ( *result.root ),
	           canon ( QStringLiteral ( "{\"key\":\"value\",\"num\":42,\"flag\":true,\"empty\":null}" ) ) );
}

void TestYamlCodec::malformed_is_an_error ()
{
	YamlCodec::ImportResult result = YamlCodec::from_yaml ( QStringLiteral ( "key: [unclosed\n" ) );

	QVERIFY ( !result.ok );
	QVERIFY ( !result.error.isEmpty () );
}

QTEST_APPLESS_MAIN ( TestYamlCodec )

#include "tst_yaml_codec.moc"
