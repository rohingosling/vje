//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include <vje_core/services/value_placeholders.hpp>
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
	void export_nested_containers_as_placeholders ();
	void export_single_column_forms ();
	void export_errors ();
	void import_simple ();
	void import_quoting ();
	void import_inference ();
	void import_lf_and_empty ();
	void round_trip ();
};

void TestCsvCodec::is_exportable_rules ()
{
	// A non-empty array, and nothing more. Every other shape the rule used to refuse is now something export HANDLES,
	// so the cases that were once refusals are the ones that matter here.

	QVERIFY ( CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[{\"a\":1,\"b\":2},{\"a\":3,\"b\":4}]" ) ) ) );
	QVERIFY ( CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[{\"a\":1},{\"b\":2}]" ) ) ) );           // Ragged.
	QVERIFY ( CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[1,2,3]" ) ) ) );                         // Scalars.
	QVERIFY ( CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[{\"a\":{\"x\":1}}]" ) ) ) );             // Nested.
	QVERIFY ( CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[1,{\"a\":2}]" ) ) ) );                   // Mixed.
	QVERIFY ( CsvCodec::is_exportable ( *parse ( QStringLiteral ( "[[1],[2]]" ) ) ) );                       // Arrays.

	// The two that remain, reported as REASONS rather than as a bare false, because the UI has to say which.

	QCOMPARE ( static_cast<int> ( CsvCodec::exportability ( *parse ( QStringLiteral ( "{\"a\":1}" ) ) ) ),
	           static_cast<int> ( CsvExportBlocker::NotAnArray ) );

	QCOMPARE ( static_cast<int> ( CsvCodec::exportability ( *parse ( QStringLiteral ( "[]" ) ) ) ),
	           static_cast<int> ( CsvExportBlocker::EmptyArray ) );

	QCOMPARE ( static_cast<int> ( CsvCodec::exportability ( *parse ( QStringLiteral ( "\"scalar\"" ) ) ) ),
	           static_cast<int> ( CsvExportBlocker::NotAnArray ) );

	QCOMPARE ( static_cast<int> ( CsvCodec::exportability ( *parse ( QStringLiteral ( "[1]" ) ) ) ),
	           static_cast<int> ( CsvExportBlocker::None ) );
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
	// A ragged array exports as its NORMALIZED view without the document being normalized: columns are the union of
	// keys in first-encountered order, and a member an element does not have writes an empty cell.

	CsvExportResult result = CsvCodec::export_array ( *parse ( QStringLiteral ( "[{\"a\":1},{\"b\":2}]" ) ) );

	QVERIFY  ( result.ok );
	QCOMPARE ( result.csv, QStringLiteral ( "a,b\r\n1,\r\n,2" ) );
	QCOMPARE ( result.placeholderCells, 0 );

	// The distinction that empty cell is carrying, stated as its own assertion: ABSENT is not the same as null, and a
	// CSV can say both. This is deliberately NOT what EDIT-11 Normalize would write into the document, which fills an
	// absent member with null and would make the two indistinguishable here.

	CsvExportResult mixed = CsvCodec::export_array ( *parse ( QStringLiteral ( "[{\"a\":null},{\"b\":2}]" ) ) );

	QVERIFY  ( mixed.ok );
	QCOMPARE ( mixed.csv, QStringLiteral ( "a,b\r\nnull,\r\n,2" ) );
}

void TestCsvCodec::export_nested_containers_as_placeholders ()
{
	// A nested object or array writes the same one-slot text the Form View's table shows for it, and the count comes
	// back so a caller can warn that the file cannot be read back.

	CsvExportResult result = CsvCodec::export_array
	(
		*parse ( QStringLiteral ( "[{\"id\":1,\"tags\":[\"a\",\"b\"]},{\"id\":2,\"tags\":{\"x\":1}}]" ) )
	);

	QVERIFY  ( result.ok );
	QCOMPARE ( result.csv, QStringLiteral ( "id,tags\r\n1,[...]\r\n2,{...}" ) );
	QCOMPARE ( result.placeholderCells, 2 );

	// The text is the SHARED constant, not a literal repeated here -- which is the whole reason it was hoisted into
	// vje_core: FILE-11's rule is "the text the table view shows", so this compares against that text rather than
	// against a second spelling of it.

	QVERIFY ( result.csv.contains ( value_placeholders::ARRAY ) );
	QVERIFY ( result.csv.contains ( value_placeholders::OBJECT ) );

	// An empty container is still a container: it has no one-line form either.

	CsvExportResult empties = CsvCodec::export_array ( *parse ( QStringLiteral ( "[{\"a\":{},\"b\":[]}]" ) ) );

	QVERIFY  ( empties.ok );
	QCOMPARE ( empties.csv, QStringLiteral ( "a,b\r\n{...},[...]" ) );
	QCOMPARE ( empties.placeholderCells, 2 );
}

void TestCsvCodec::export_single_column_forms ()
{
	// An array whose elements are not all objects has no column set to derive, so it exports as one column. A root
	// array has no name of its own, so the header falls back to "value".

	CsvExportResult scalars = CsvCodec::export_array ( *parse ( QStringLiteral ( "[\"red\",\"green\",42,null,true]" ) ) );

	QVERIFY  ( scalars.ok );
	QCOMPARE ( scalars.csv, QStringLiteral ( "value\r\nred\r\ngreen\r\n42\r\nnull\r\ntrue" ) );
	QCOMPARE ( scalars.placeholderCells, 0 );

	// MIXED kinds take the same form -- the Form View's own rule, that a mixed array is one column rather than an
	// invented column set -- and a container element becomes a placeholder like any other container value.

	CsvExportResult mixed = CsvCodec::export_array ( *parse ( QStringLiteral ( "[1,{\"a\":2},[3]]" ) ) );

	QVERIFY  ( mixed.ok );
	QCOMPARE ( mixed.csv, QStringLiteral ( "value\r\n1\r\n{...}\r\n[...]" ) );
	QCOMPARE ( mixed.placeholderCells, 2 );

	// The single column is headed by the array's OWN key when it has one: the name the user gave it says more than
	// "value" ever could, and it is the name they will look for in the file.

	const std::unique_ptr<JsonNode> document = parse ( QStringLiteral ( "{\"tags\":[\"red\",\"green\"]}" ) );

	CsvExportResult named = CsvCodec::export_array ( *document->find_member ( QStringLiteral ( "tags" ) ) );

	QVERIFY  ( named.ok );
	QCOMPARE ( named.csv, QStringLiteral ( "tags\r\nred\r\ngreen" ) );

	// An array that is an array ELEMENT has only a position, and a column headed "0" would say less than nothing, so
	// it falls back with the root.

	const std::unique_ptr<JsonNode> nested = parse ( QStringLiteral ( "[[\"red\"]]" ) );

	CsvExportResult inner = CsvCodec::export_array ( *nested->array_element ( 0 ) );

	QVERIFY  ( inner.ok );
	QCOMPARE ( inner.csv, QStringLiteral ( "value\r\nred" ) );

	// The header is a field like any other, so a key needing RFC 4180 quoting gets it.

	const std::unique_ptr<JsonNode> awkward = parse ( QStringLiteral ( "{\"a,b\":[1]}" ) );

	CsvExportResult quoted = CsvCodec::export_array ( *awkward->find_member ( QStringLiteral ( "a,b" ) ) );

	QVERIFY  ( quoted.ok );
	QCOMPARE ( quoted.csv, QStringLiteral ( "\"a,b\"\r\n1" ) );
}

void TestCsvCodec::export_errors ()
{
	// The two remaining failures, and they are the two blockers -- export_array refuses exactly what exportability()
	// refuses, so the menu's reason and the export's own behaviour cannot part company.

	QVERIFY ( !CsvCodec::export_array ( *parse ( QStringLiteral ( "{\"a\":1}" ) ) ).ok );                    // Not an array.
	QVERIFY ( !CsvCodec::export_array ( *parse ( QStringLiteral ( "[]" ) ) ).ok );                           // Empty array.

	QVERIFY ( !CsvCodec::export_array ( *parse ( QStringLiteral ( "{\"a\":1}" ) ) ).error.isEmpty () );
	QVERIFY ( !CsvCodec::export_array ( *parse ( QStringLiteral ( "[]" ) ) ).error.isEmpty () );

	// What used to fail here now succeeds, which is the change stated as a test rather than only as a relaxation.

	QVERIFY ( CsvCodec::export_array ( *parse ( QStringLiteral ( "[1,2]" ) ) ).ok );
	QVERIFY ( CsvCodec::export_array ( *parse ( QStringLiteral ( "[{\"a\":[1]}]" ) ) ).ok );
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
