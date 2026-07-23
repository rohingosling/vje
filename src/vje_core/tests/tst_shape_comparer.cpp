//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for JsonShapeComparer (EDITOR-11 shape rules): null wildcards, scalars by kind, objects by recursive
//   order-insensitive key sets, arrays by element shape (empty matches any, length ignored), and the column checks
//   (homogeneous / heterogeneous-waived / unconstrained) that gate a container paste.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonShapeComparer.hpp>
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

class TestShapeComparer : public QObject
{
	Q_OBJECT

private slots:

	void null_is_a_wildcard ();
	void scalars_by_kind ();
	void objects_by_key_set_order_insensitive ();
	void objects_recurse_into_values ();
	void arrays_by_element_shape ();
	void column_homogeneous_check ();
	void compatible_with_homogeneous_column ();
	void heterogeneous_column_waives_the_check ();
	void unconstrained_column_accepts_anything ();
};

void TestShapeComparer::null_is_a_wildcard ()
{
	std::unique_ptr<JsonNode> nul    = JsonNode::make_null ();
	std::unique_ptr<JsonNode> number = JsonNode::make_number ( QStringLiteral ( "1" ) );
	std::unique_ptr<JsonNode> object = parse ( QStringLiteral ( "{\"a\":1}" ) );

	QVERIFY ( JsonShapeComparer::shapes_compatible ( *nul, *number ) );
	QVERIFY ( JsonShapeComparer::shapes_compatible ( *object, *nul ) );
}

void TestShapeComparer::scalars_by_kind ()
{
	std::unique_ptr<JsonNode> a = JsonNode::make_number ( QStringLiteral ( "1" ) );
	std::unique_ptr<JsonNode> b = JsonNode::make_number ( QStringLiteral ( "999" ) );
	std::unique_ptr<JsonNode> s = JsonNode::make_string ( QStringLiteral ( "x" ) );

	QVERIFY ( JsonShapeComparer::shapes_compatible ( *a, *b ) );      // Same kind, values irrelevant.
	QVERIFY ( !JsonShapeComparer::shapes_compatible ( *a, *s ) );     // Different kinds.
}

void TestShapeComparer::objects_by_key_set_order_insensitive ()
{
	std::unique_ptr<JsonNode> ab = parse ( QStringLiteral ( "{\"a\":1,\"b\":2}" ) );
	std::unique_ptr<JsonNode> ba = parse ( QStringLiteral ( "{\"b\":9,\"a\":8}" ) );
	std::unique_ptr<JsonNode> a  = parse ( QStringLiteral ( "{\"a\":1}" ) );

	QVERIFY ( JsonShapeComparer::shapes_compatible ( *ab, *ba ) );    // Same key set, different order.
	QVERIFY ( !JsonShapeComparer::shapes_compatible ( *ab, *a ) );    // Different key set.
}

void TestShapeComparer::objects_recurse_into_values ()
{
	std::unique_ptr<JsonNode> number = parse ( QStringLiteral ( "{\"a\":1}" ) );
	std::unique_ptr<JsonNode> string = parse ( QStringLiteral ( "{\"a\":\"x\"}" ) );
	std::unique_ptr<JsonNode> nul    = parse ( QStringLiteral ( "{\"a\":null}" ) );

	QVERIFY ( !JsonShapeComparer::shapes_compatible ( *number, *string ) );   // a: number vs string.
	QVERIFY ( JsonShapeComparer::shapes_compatible ( *number, *nul ) );       // a: null wildcard.
}

void TestShapeComparer::arrays_by_element_shape ()
{
	std::unique_ptr<JsonNode> empty   = parse ( QStringLiteral ( "[]" ) );
	std::unique_ptr<JsonNode> numbers = parse ( QStringLiteral ( "[1,2,3]" ) );
	std::unique_ptr<JsonNode> oneNum  = parse ( QStringLiteral ( "[9]" ) );
	std::unique_ptr<JsonNode> strings = parse ( QStringLiteral ( "[\"x\"]" ) );
	std::unique_ptr<JsonNode> objsA   = parse ( QStringLiteral ( "[{\"a\":1}]" ) );
	std::unique_ptr<JsonNode> objsB   = parse ( QStringLiteral ( "[{\"b\":1}]" ) );

	QVERIFY ( JsonShapeComparer::shapes_compatible ( *empty, *numbers ) );    // Empty matches any array.
	QVERIFY ( JsonShapeComparer::shapes_compatible ( *numbers, *oneNum ) );   // Length ignored.
	QVERIFY ( !JsonShapeComparer::shapes_compatible ( *numbers, *strings ) ); // Element kind differs.
	QVERIFY ( !JsonShapeComparer::shapes_compatible ( *objsA, *objsB ) );     // Element object shape differs.
}

void TestShapeComparer::column_homogeneous_check ()
{
	std::unique_ptr<JsonNode> n1  = JsonNode::make_number ( QStringLiteral ( "1" ) );
	std::unique_ptr<JsonNode> n2  = JsonNode::make_number ( QStringLiteral ( "2" ) );
	std::unique_ptr<JsonNode> nul = JsonNode::make_null ();
	std::unique_ptr<JsonNode> s   = JsonNode::make_string ( QStringLiteral ( "x" ) );

	QVERIFY ( JsonShapeComparer::column_is_homogeneous ( { n1.get (), nul.get (), n2.get () } ) );   // null ignored.
	QVERIFY ( !JsonShapeComparer::column_is_homogeneous ( { n1.get (), s.get () } ) );
}

void TestShapeComparer::compatible_with_homogeneous_column ()
{
	std::unique_ptr<JsonNode> n1        = JsonNode::make_number ( QStringLiteral ( "1" ) );
	std::unique_ptr<JsonNode> n2        = JsonNode::make_number ( QStringLiteral ( "2" ) );
	std::unique_ptr<JsonNode> candNum   = JsonNode::make_number ( QStringLiteral ( "5" ) );
	std::unique_ptr<JsonNode> candStr   = JsonNode::make_string ( QStringLiteral ( "x" ) );

	const std::vector<const JsonNode*> column { n1.get (), n2.get () };

	QVERIFY ( JsonShapeComparer::compatible_with_column ( *candNum, column ) );
	QVERIFY ( !JsonShapeComparer::compatible_with_column ( *candStr, column ) );
}

void TestShapeComparer::heterogeneous_column_waives_the_check ()
{
	std::unique_ptr<JsonNode> n = JsonNode::make_number ( QStringLiteral ( "1" ) );
	std::unique_ptr<JsonNode> s = JsonNode::make_string ( QStringLiteral ( "x" ) );
	std::unique_ptr<JsonNode> b = JsonNode::make_boolean ( true );

	const std::vector<const JsonNode*> column { n.get (), s.get () };   // Already heterogeneous.

	QVERIFY ( JsonShapeComparer::compatible_with_column ( *b, column ) );   // Any candidate is accepted.
}

void TestShapeComparer::unconstrained_column_accepts_anything ()
{
	std::unique_ptr<JsonNode> nul  = JsonNode::make_null ();
	std::unique_ptr<JsonNode> cand = JsonNode::make_string ( QStringLiteral ( "x" ) );

	QVERIFY ( JsonShapeComparer::compatible_with_column ( *cand, {} ) );                 // Empty column.
	QVERIFY ( JsonShapeComparer::compatible_with_column ( *cand, { nul.get () } ) );     // All-null column.
}

QTEST_APPLESS_MAIN ( TestShapeComparer )

#include "tst_shape_comparer.moc"
