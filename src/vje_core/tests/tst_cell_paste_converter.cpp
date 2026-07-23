//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for CellPasteConverter (EDITOR-11 matrix + EDITOR-12 typed entry): every source x target pairing
//   (Converted / Incompatible / NeedsShapeCheck), raw number-token preservation (FILE-10), the universal null, the
//   as-is untyped target, the JSON-literal typed-entry interpretation (containers commit as strings), and clipboard
//   text resolution (JSON parses to its type; else a string).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/CellPasteConverter.hpp>
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

	// resolve_paste against a freshly built source of the given text/kind.

	CellPasteOutcome paste ( const JsonNode& source, CellTarget target )
	{
		return CellPasteConverter::resolve_paste ( source, target );
	}
}

class TestCellPasteConverter : public QObject
{
	Q_OBJECT

private slots:

	void string_source ();
	void number_source_preserves_token ();
	void number_to_boolean_by_equality ();
	void boolean_source ();
	void null_is_universal ();
	void untyped_target_takes_scalars_as_is ();
	void container_sources ();
	void incompatible_carries_a_message ();

	void typed_entry_literals ();
	void typed_entry_container_becomes_string ();
	void clipboard_text_resolution ();
};

void TestCellPasteConverter::string_source ()
{
	std::unique_ptr<JsonNode> s = JsonNode::make_string ( QStringLiteral ( "42" ) );

	QCOMPARE ( paste ( *s, CellTarget::String ).resolution,  PasteResolution::Converted );

	// "42" -> a number cell converts; a word does not.

	QCOMPARE ( paste ( *s, CellTarget::Number ).resolution, PasteResolution::Converted );
	QCOMPARE ( paste ( *JsonNode::make_string ( QStringLiteral ( "abc" ) ), CellTarget::Number ).resolution, PasteResolution::Incompatible );

	// "true"/"false" (case-insensitive) convert to a boolean cell; anything else does not.

	QCOMPARE ( paste ( *JsonNode::make_string ( QStringLiteral ( "TRUE" ) ), CellTarget::Boolean ).resolution, PasteResolution::Converted );
	QCOMPARE ( paste ( *JsonNode::make_string ( QStringLiteral ( "x" ) ),    CellTarget::Boolean ).resolution, PasteResolution::Incompatible );

	// Into a container cell: refused.

	QCOMPARE ( paste ( *s, CellTarget::Object ).resolution, PasteResolution::Incompatible );
	QCOMPARE ( paste ( *s, CellTarget::Array ).resolution,  PasteResolution::Incompatible );
}

void TestCellPasteConverter::number_source_preserves_token ()
{
	std::unique_ptr<JsonNode> n = JsonNode::make_number ( QStringLiteral ( "1.50" ) );

	// -> string: the raw token as a string.

	CellPasteOutcome toString = paste ( *n, CellTarget::String );
	QCOMPARE ( toString.resolution, PasteResolution::Converted );
	QCOMPARE ( toString.value->kind (), JsonKind::String );
	QCOMPARE ( toString.value->string_value (), QStringLiteral ( "1.50" ) );

	// -> number: the raw token survives.

	CellPasteOutcome toNumber = paste ( *n, CellTarget::Number );
	QCOMPARE ( toNumber.resolution, PasteResolution::Converted );
	QCOMPARE ( toNumber.value->number_token (), QStringLiteral ( "1.50" ) );
}

void TestCellPasteConverter::number_to_boolean_by_equality ()
{
	QCOMPARE ( paste ( *JsonNode::make_number ( QStringLiteral ( "0" ) ),   CellTarget::Boolean ).value->boolean_value (), false );
	QCOMPARE ( paste ( *JsonNode::make_number ( QStringLiteral ( "1" ) ),   CellTarget::Boolean ).value->boolean_value (), true );
	QCOMPARE ( paste ( *JsonNode::make_number ( QStringLiteral ( "1.0" ) ), CellTarget::Boolean ).value->boolean_value (), true );
	QCOMPARE ( paste ( *JsonNode::make_number ( QStringLiteral ( "2" ) ),   CellTarget::Boolean ).resolution, PasteResolution::Incompatible );
}

void TestCellPasteConverter::boolean_source ()
{
	std::unique_ptr<JsonNode> t = JsonNode::make_boolean ( true );

	QCOMPARE ( paste ( *t, CellTarget::String ).value->string_value (), QStringLiteral ( "true" ) );
	QCOMPARE ( paste ( *t, CellTarget::Number ).value->number_token (), QStringLiteral ( "1" ) );
	QCOMPARE ( paste ( *JsonNode::make_boolean ( false ), CellTarget::Number ).value->number_token (), QStringLiteral ( "0" ) );
	QCOMPARE ( paste ( *t, CellTarget::Boolean ).resolution, PasteResolution::Converted );
	QCOMPARE ( paste ( *t, CellTarget::Object ).resolution,  PasteResolution::Incompatible );
}

void TestCellPasteConverter::null_is_universal ()
{
	std::unique_ptr<JsonNode> nul = JsonNode::make_null ();

	for ( const CellTarget target : { CellTarget::String, CellTarget::Number, CellTarget::Boolean,
	                                  CellTarget::Object, CellTarget::Array, CellTarget::Untyped } )
	{
		CellPasteOutcome outcome = paste ( *nul, target );
		QCOMPARE ( outcome.resolution, PasteResolution::Converted );
		QCOMPARE ( outcome.value->kind (), JsonKind::Null );
	}
}

void TestCellPasteConverter::untyped_target_takes_scalars_as_is ()
{
	std::unique_ptr<JsonNode> s = JsonNode::make_string ( QStringLiteral ( "hi" ) );
	std::unique_ptr<JsonNode> n = JsonNode::make_number ( QStringLiteral ( "7" ) );

	CellPasteOutcome fromString = paste ( *s, CellTarget::Untyped );
	QCOMPARE ( fromString.resolution, PasteResolution::Converted );
	QCOMPARE ( fromString.value->string_value (), QStringLiteral ( "hi" ) );

	QCOMPARE ( paste ( *n, CellTarget::Untyped ).value->number_token (), QStringLiteral ( "7" ) );
}

void TestCellPasteConverter::container_sources ()
{
	std::unique_ptr<JsonNode> object = parse ( QStringLiteral ( "{\"a\":1}" ) );
	std::unique_ptr<JsonNode> array  = parse ( QStringLiteral ( "[1,2]" ) );

	// Container into matching container / untyped: defer to the shape check, carrying the source clone.

	CellPasteOutcome objToObj = paste ( *object, CellTarget::Object );
	QCOMPARE ( objToObj.resolution, PasteResolution::NeedsShapeCheck );
	QVERIFY  ( objToObj.value != nullptr );
	QCOMPARE ( objToObj.value->kind (), JsonKind::Object );

	QCOMPARE ( paste ( *object, CellTarget::Untyped ).resolution, PasteResolution::NeedsShapeCheck );
	QCOMPARE ( paste ( *array,  CellTarget::Array ).resolution,   PasteResolution::NeedsShapeCheck );
	QCOMPARE ( paste ( *array,  CellTarget::Untyped ).resolution, PasteResolution::NeedsShapeCheck );

	// Cross-container and container-into-scalar: refused.

	QCOMPARE ( paste ( *object, CellTarget::Array ).resolution,  PasteResolution::Incompatible );
	QCOMPARE ( paste ( *array,  CellTarget::Object ).resolution, PasteResolution::Incompatible );
	QCOMPARE ( paste ( *object, CellTarget::String ).resolution, PasteResolution::Incompatible );
}

void TestCellPasteConverter::incompatible_carries_a_message ()
{
	CellPasteOutcome outcome = paste ( *JsonNode::make_string ( QStringLiteral ( "x" ) ), CellTarget::Number );

	QCOMPARE ( outcome.resolution, PasteResolution::Incompatible );
	QVERIFY  ( !outcome.message.isEmpty () );
	QVERIFY  ( outcome.value == nullptr );
}

void TestCellPasteConverter::typed_entry_literals ()
{
	std::unique_ptr<JsonNode> number = CellPasteConverter::interpret_typed_entry ( QStringLiteral ( "42" ) );
	QCOMPARE ( number->kind (), JsonKind::Number );
	QCOMPARE ( number->number_token (), QStringLiteral ( "42" ) );

	QCOMPARE ( CellPasteConverter::interpret_typed_entry ( QStringLiteral ( " 42 " ) )->number_token (), QStringLiteral ( "42" ) );
	QCOMPARE ( CellPasteConverter::interpret_typed_entry ( QStringLiteral ( "true" ) )->kind (), JsonKind::Boolean );
	QCOMPARE ( CellPasteConverter::interpret_typed_entry ( QStringLiteral ( "null" ) )->kind (), JsonKind::Null );

	std::unique_ptr<JsonNode> quoted = CellPasteConverter::interpret_typed_entry ( QStringLiteral ( "\"true\"" ) );
	QCOMPARE ( quoted->kind (), JsonKind::String );
	QCOMPARE ( quoted->string_value (), QStringLiteral ( "true" ) );

	std::unique_ptr<JsonNode> word = CellPasteConverter::interpret_typed_entry ( QStringLiteral ( "hello" ) );
	QCOMPARE ( word->kind (), JsonKind::String );
	QCOMPARE ( word->string_value (), QStringLiteral ( "hello" ) );

	QCOMPARE ( CellPasteConverter::interpret_typed_entry ( QString () )->kind (), JsonKind::String );
}

void TestCellPasteConverter::typed_entry_container_becomes_string ()
{
	// A bare object / array literal is NOT a scalar literal, so it commits as a plain string.

	std::unique_ptr<JsonNode> objectText = CellPasteConverter::interpret_typed_entry ( QStringLiteral ( "{\"a\":1}" ) );
	QCOMPARE ( objectText->kind (), JsonKind::String );
	QCOMPARE ( objectText->string_value (), QStringLiteral ( "{\"a\":1}" ) );

	QCOMPARE ( CellPasteConverter::interpret_typed_entry ( QStringLiteral ( "[1,2]" ) )->kind (), JsonKind::String );
}

void TestCellPasteConverter::clipboard_text_resolution ()
{
	// Valid JSON parses to its type, containers included; invalid text is a plain string.

	QCOMPARE ( CellPasteConverter::resolve_clipboard_text ( QStringLiteral ( "{\"a\":1}" ) )->kind (), JsonKind::Object );
	QCOMPARE ( CellPasteConverter::resolve_clipboard_text ( QStringLiteral ( "[1,2]" ) )->kind (),    JsonKind::Array );
	QCOMPARE ( CellPasteConverter::resolve_clipboard_text ( QStringLiteral ( "42" ) )->number_token (), QStringLiteral ( "42" ) );

	std::unique_ptr<JsonNode> text = CellPasteConverter::resolve_clipboard_text ( QStringLiteral ( "hello" ) );
	QCOMPARE ( text->kind (), JsonKind::String );
	QCOMPARE ( text->string_value (), QStringLiteral ( "hello" ) );
}

QTEST_APPLESS_MAIN ( TestCellPasteConverter )

#include "tst_cell_paste_converter.moc"
