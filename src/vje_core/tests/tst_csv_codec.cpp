//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for CsvCodec (RFC 4180, FILE-11): the is_exportable precondition, header + record export over the union
//   of keys, RFC 4180 quoting, the null-literal / empty-cell distinction, import type inference, CRLF/LF tolerance,
//   quoted-field parsing, malformed-input parity, and a full JSON -> CSV -> JSON round-trip.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/convert/CsvCodec.hpp>
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

	// Canonical compact form of a JSON string, for readable structural comparison.

	QString canon ( const QString& json )
	{
		return JsonSerializer::serialize ( *parse ( json ) );
	}
}

class TestCsvCodec : public QObject
{
	Q_OBJECT

private slots:

	void is_exportable_rules ();
	void export_simple ();
	void export_quoting ();
	void export_null_and_empty ();
	void export_union_columns ();
	void export_errors ();
	void import_simple ();
	void import_quoting ();
	void import_inference ();
	void import_lf_and_empty ();
	void round_trip ();
};

void TestCsvCodec::is_exportable_rules ()
{
	QVERIFY (  CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[{\"a\":1,\"b\":2},{\"a\":3,\"b\":4}]" ) ) ) );

	QVERIFY ( !CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[{\"a\":1},{\"b\":2}]" ) ) ) );          // Ragged.
	QVERIFY ( !CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[]" ) ) ) );                             // Empty.
	QVERIFY ( !CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[1,2,3]" ) ) ) );                        // Scalars.
	QVERIFY ( !CsvCodec::is_exportable ( *parse ( QStringLiteral ( "{\"a\":1}" ) ) ) );                      // Not an array.
	QVERIFY ( !CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[{\"a\":{\"x\":1}}]" ) ) ) );            // Non-flat.
}

void TestCsvCodec::export_simple ()
{
	CsvExportResult result = CsvCodec::export_array ( *parse ( QStringLiteral ( "[{\"name\":\"Alice\",\"age\":30},{\"name\":\"Bob\",\"age\":25}]" ) ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( result.csv, QStringLiteral ( "name,age\r\nAlice,30\r\nBob,25" ) );
}

void TestCsvCodec::export_quoting ()
{
	// A comma, an embedded quote (doubled), and a newline each force RFC 4180 quoting.

	CsvExportResult result = CsvCodec::export_array ( *parse ( QStringLiteral (
		"[{\"note\":\"a,b\"},{\"note\":\"say \\\"hi\\\"\"},{\"note\":\"line1\\nline2\"}]" ) ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( result.csv, QStringLiteral ( "note\r\n\"a,b\"\r\n\"say \"\"hi\"\"\"\r\n\"line1\nline2\"" ) );
}

void TestCsvCodec::export_null_and_empty ()
{
	// null writes the literal `null`; an empty string writes an empty cell.

	CsvExportResult result = CsvCodec::export_array ( *parse ( QStringLiteral ( "[{\"a\":null,\"b\":\"\"}]" ) ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( result.csv, QStringLiteral ( "a,b\r\nnull," ) );
}

void TestCsvCodec::export_union_columns ()
{
	// A ragged array still exports (the UI gates on is_exportable): columns are the union, absent members are empty.

	CsvExportResult result = CsvCodec::export_array ( *parse ( QStringLiteral ( "[{\"a\":1},{\"b\":2}]" ) ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( result.csv, QStringLiteral ( "a,b\r\n1,\r\n,2" ) );
}

void TestCsvCodec::export_errors ()
{
	QVERIFY ( !CsvCodec::export_array ( *parse ( QStringLiteral ( "{\"a\":1}" ) ) ).ok );                    // Not an array.
	QVERIFY ( !CsvCodec::export_array ( *parse ( QStringLiteral ( "[]" ) ) ).ok );                           // Empty array.
	QVERIFY ( !CsvCodec::export_array ( *parse ( QStringLiteral ( "[1,2]" ) ) ).ok );                        // Non-object element.
	QVERIFY ( !CsvCodec::export_array ( *parse ( QStringLiteral ( "[{\"a\":[1]}]" ) ) ).ok );                // Non-flat.
}

void TestCsvCodec::import_simple ()
{
	CsvImportResult result = CsvCodec::import_text ( QStringLiteral ( "name,age\r\nAlice,30\r\nBob,25" ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( JsonSerializer::serialize ( *result.root ),
	           canon ( QStringLiteral ( "[{\"name\":\"Alice\",\"age\":30},{\"name\":\"Bob\",\"age\":25}]" ) ) );
}

void TestCsvCodec::import_quoting ()
{
	CsvImportResult result = CsvCodec::import_text ( QStringLiteral ( "note\r\n\"a,b\"\r\n\"say \"\"hi\"\"\"" ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( JsonSerializer::serialize ( *result.root ),
	           canon ( QStringLiteral ( "[{\"note\":\"a,b\"},{\"note\":\"say \\\"hi\\\"\"}]" ) ) );
}

void TestCsvCodec::import_inference ()
{
	// Unambiguous true / false / null / number infer; everything else (including an empty cell) is a string.

	CsvImportResult result = CsvCodec::import_text ( QStringLiteral ( "flag,count,label,maybe,blank\r\ntrue,42,hello,null," ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( JsonSerializer::serialize ( *result.root ),
	           canon ( QStringLiteral ( "[{\"flag\":true,\"count\":42,\"label\":\"hello\",\"maybe\":null,\"blank\":\"\"}]" ) ) );
}

void TestCsvCodec::import_lf_and_empty ()
{
	// LF-only line breaks are tolerated.

	CsvImportResult lf = CsvCodec::import_text ( QStringLiteral ( "a,b\n1,2" ) );
	QVERIFY  ( lf.ok );
	QCOMPARE ( JsonSerializer::serialize ( *lf.root ), canon ( QStringLiteral ( "[{\"a\":1,\"b\":2}]" ) ) );

	// Empty input is an error.

	QVERIFY ( !CsvCodec::import_text ( QString () ).ok );
}

void TestCsvCodec::round_trip ()
{
	const QString original = QStringLiteral (
		"[{\"name\":\"Alice\",\"age\":30,\"active\":true},{\"name\":\"Bob\",\"age\":25,\"active\":false}]" );

	CsvExportResult exported = CsvCodec::export_array ( *parse ( original ) );
	QVERIFY ( exported.ok );

	CsvImportResult reimported = CsvCodec::import_text ( exported.csv );
	QVERIFY ( reimported.ok );

	QCOMPARE ( JsonSerializer::serialize ( *reimported.root ), canon ( original ) );
}

QTEST_APPLESS_MAIN ( TestCsvCodec )

#include "tst_csv_codec.moc"
