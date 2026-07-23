//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Byte-exact goldens for XmlExporter (the simple readable JSON -> XML mapping): object
//   members become named elements, arrays become repeated <item> elements, scalars become escaped text, null and
//   empty containers become empty elements, number tokens survive verbatim, and character data is XML-escaped.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/convert/XmlExporter.hpp>
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

	QString export_json ( const QString& json )
	{
		return XmlExporter::export_document ( *parse ( json ) );
	}

	const QString DECL = QStringLiteral ( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" );
}

class TestXmlExporter : public QObject
{
	Q_OBJECT

private slots:

	void object_members ();
	void array_items ();
	void nested_and_escaping ();
	void scalar_root ();
	void null_and_empty_containers ();
	void booleans_and_number_tokens ();
};

void TestXmlExporter::object_members ()
{
	QCOMPARE ( export_json ( QStringLiteral ( "{\"a\":1,\"b\":\"x\"}" ) ),
	           DECL + QStringLiteral ( "<document>\n  <a>1</a>\n  <b>x</b>\n</document>\n" ) );
}

void TestXmlExporter::array_items ()
{
	QCOMPARE ( export_json ( QStringLiteral ( "{\"items\":[1,2]}" ) ),
	           DECL + QStringLiteral ( "<document>\n  <items>\n    <item>1</item>\n    <item>2</item>\n  </items>\n</document>\n" ) );
}

void TestXmlExporter::nested_and_escaping ()
{
	QCOMPARE ( export_json ( QStringLiteral ( "{\"x\":{\"y\":\"a<b>&c\"}}" ) ),
	           DECL + QStringLiteral ( "<document>\n  <x>\n    <y>a&lt;b&gt;&amp;c</y>\n  </x>\n</document>\n" ) );
}

void TestXmlExporter::scalar_root ()
{
	QCOMPARE ( export_json ( QStringLiteral ( "\"hi\"" ) ),
	           DECL + QStringLiteral ( "<document>hi</document>\n" ) );
}

void TestXmlExporter::null_and_empty_containers ()
{
	QCOMPARE ( export_json ( QStringLiteral ( "{\"n\":null,\"o\":{},\"a\":[]}" ) ),
	           DECL + QStringLiteral ( "<document>\n  <n></n>\n  <o></o>\n  <a></a>\n</document>\n" ) );
}

void TestXmlExporter::booleans_and_number_tokens ()
{
	QCOMPARE ( export_json ( QStringLiteral ( "{\"b\":true,\"n\":1.50}" ) ),
	           DECL + QStringLiteral ( "<document>\n  <b>true</b>\n  <n>1.50</n>\n</document>\n" ) );
}

QTEST_APPLESS_MAIN ( TestXmlExporter )

#include "tst_xml_exporter.moc"
