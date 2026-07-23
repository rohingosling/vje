//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for XmlImporter (FILE-13): the four strategies on the spec's worked example
//   (byte-for-byte), same-named-sibling collapse into arrays, nested-object mapping in both scalar modes, the
//   Infer-scalar-types rules (integers / true / false / null only; decimals and exponents stay strings), BadgerFish's
//   non-collapsing leaf, the Custom-flattened text-key collision suffix, strategy metadata, and malformed/empty
//   parity.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/convert/XmlImporter.hpp>
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

	QString import_xml ( const QString& xml, XmlImportStrategyKind kind, bool infer = false, const QString& textKey = QString () )
	{
		std::unique_ptr<IXmlImportStrategy> strategy = XmlImporter::make_strategy ( kind, textKey );
		XmlImporter::Result                 result   = XmlImporter::import_text ( xml, *strategy, infer );

		return result.ok ? JsonSerializer::serialize ( *result.root ) : QStringLiteral ( "<error>" );
	}

	// The worked-example element.

	const QString WORKED_EXAMPLE = QStringLiteral (
		"<sentence name=\"test-sentence\" description=\"Short example sentence.\" tag=\"0\">The cat went up the hill.</sentence>" );
}

class TestXmlImporter : public QObject
{
	Q_OBJECT

private slots:

	void worked_example_badgerfish ();
	void worked_example_direct ();
	void worked_example_grouped ();
	void worked_example_custom ();
	void sibling_collapse ();
	void nested_objects_string_mode ();
	void nested_objects_infer_mode ();
	void infer_rules ();
	void badgerfish_leaf_is_wrapped ();
	void custom_text_key_collision ();
	void strategy_metadata ();
	void malformed_and_empty ();
};

void TestXmlImporter::worked_example_badgerfish ()
{
	QCOMPARE ( import_xml ( WORKED_EXAMPLE, XmlImportStrategyKind::BadgerFish ),
	           canon ( QStringLiteral (
		           "{\"sentence\":{\"@name\":\"test-sentence\",\"@description\":\"Short example sentence.\","
		           "\"@tag\":\"0\",\"$\":\"The cat went up the hill.\"}}" ) ) );
}

void TestXmlImporter::worked_example_direct ()
{
	QCOMPARE ( import_xml ( WORKED_EXAMPLE, XmlImportStrategyKind::DirectAttributeKeys ),
	           canon ( QStringLiteral (
		           "{\"sentence\":{\"name\":\"test-sentence\",\"description\":\"Short example sentence.\","
		           "\"tag\":\"0\",\"content\":\"The cat went up the hill.\"}}" ) ) );
}

void TestXmlImporter::worked_example_grouped ()
{
	QCOMPARE ( import_xml ( WORKED_EXAMPLE, XmlImportStrategyKind::GroupedAttributes ),
	           canon ( QStringLiteral (
		           "{\"sentence\":{\"attributes\":{\"name\":\"test-sentence\",\"description\":\"Short example sentence.\","
		           "\"tag\":\"0\"},\"content\":\"The cat went up the hill.\"}}" ) ) );
}

void TestXmlImporter::worked_example_custom ()
{
	// Text value key left at its default -- the element's own name ("sentence").

	QCOMPARE ( import_xml ( WORKED_EXAMPLE, XmlImportStrategyKind::CustomFlattened ),
	           canon ( QStringLiteral (
		           "{\"sentence\":{\"name\":\"test-sentence\",\"description\":\"Short example sentence.\","
		           "\"tag\":\"0\",\"sentence\":\"The cat went up the hill.\"}}" ) ) );
}

void TestXmlImporter::sibling_collapse ()
{
	// Same-named sibling elements collapse into an array; each text-only <item> collapses to a scalar.

	QCOMPARE ( import_xml ( QStringLiteral ( "<root><item>a</item><item>b</item></root>" ), XmlImportStrategyKind::DirectAttributeKeys ),
	           canon ( QStringLiteral ( "{\"root\":{\"item\":[\"a\",\"b\"]}}" ) ) );
}

void TestXmlImporter::nested_objects_string_mode ()
{
	QCOMPARE ( import_xml ( QStringLiteral ( "<user><name>Bob</name><age>30</age></user>" ), XmlImportStrategyKind::DirectAttributeKeys ),
	           canon ( QStringLiteral ( "{\"user\":{\"name\":\"Bob\",\"age\":\"30\"}}" ) ) );
}

void TestXmlImporter::nested_objects_infer_mode ()
{
	// Infer on: the integer age becomes a number; the name stays a string.

	QCOMPARE ( import_xml ( QStringLiteral ( "<user><name>Bob</name><age>30</age></user>" ), XmlImportStrategyKind::DirectAttributeKeys, true ),
	           canon ( QStringLiteral ( "{\"user\":{\"name\":\"Bob\",\"age\":30}}" ) ) );
}

void TestXmlImporter::infer_rules ()
{
	// null / true / false / a JSON integer type; a decimal, an exponent, and a leading-zero token stay strings.

	QCOMPARE ( import_xml ( QStringLiteral (
		           "<v><a>1</a><b>1.5</b><c>true</c><d>null</d><e>1e3</e><f>07</f></v>" ),
	           XmlImportStrategyKind::DirectAttributeKeys, true ),
	           canon ( QStringLiteral (
		           "{\"v\":{\"a\":1,\"b\":\"1.5\",\"c\":true,\"d\":null,\"e\":\"1e3\",\"f\":\"07\"}}" ) ) );
}

void TestXmlImporter::badgerfish_leaf_is_wrapped ()
{
	// BadgerFish never collapses a leaf: text content stays under $ so the mapping remains lossless.

	QCOMPARE ( import_xml ( QStringLiteral ( "<b>2</b>" ), XmlImportStrategyKind::BadgerFish ),
	           canon ( QStringLiteral ( "{\"b\":{\"$\":\"2\"}}" ) ) );
}

void TestXmlImporter::custom_text_key_collision ()
{
	// The configured text key "x" collides with attribute x, so the text member takes the "-text" suffix.

	QCOMPARE ( import_xml ( QStringLiteral ( "<e x=\"1\">text</e>" ), XmlImportStrategyKind::CustomFlattened, false, QStringLiteral ( "x" ) ),
	           canon ( QStringLiteral ( "{\"e\":{\"x\":\"1\",\"x-text\":\"text\"}}" ) ) );
}

void TestXmlImporter::strategy_metadata ()
{
	QVERIFY (  XmlImporter::make_strategy ( XmlImportStrategyKind::DirectAttributeKeys )->is_default () );
	QVERIFY ( !XmlImporter::make_strategy ( XmlImportStrategyKind::BadgerFish )->is_default () );
	QVERIFY ( !XmlImporter::make_strategy ( XmlImportStrategyKind::GroupedAttributes )->is_default () );
	QVERIFY ( !XmlImporter::make_strategy ( XmlImportStrategyKind::CustomFlattened )->is_default () );

	QCOMPARE ( XmlImporter::make_strategy ( XmlImportStrategyKind::BadgerFish )->display_name (), QStringLiteral ( "BadgerFish" ) );
	QVERIFY  ( !XmlImporter::make_strategy ( XmlImportStrategyKind::DirectAttributeKeys )->description ().isEmpty () );
}

void TestXmlImporter::malformed_and_empty ()
{
	std::unique_ptr<IXmlImportStrategy> strategy = XmlImporter::make_strategy ( XmlImportStrategyKind::DirectAttributeKeys );

	XmlImporter::Result malformed = XmlImporter::import_text ( QStringLiteral ( "<root><child></root>" ), *strategy, false );
	QVERIFY ( !malformed.ok );
	QVERIFY ( !malformed.error.isEmpty () );

	XmlImporter::Result empty = XmlImporter::import_text ( QString (), *strategy, false );
	QVERIFY ( !empty.ok );
}

QTEST_APPLESS_MAIN ( TestXmlImporter )

#include "tst_xml_importer.moc"
