//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for plan_cell_paste -- the pure composition of the array-table cell paste (EDITOR-11): the conversion
//   matrix, the container shape check, and the SET-05 jagged decision. It runs in the HEADLESS harness, which is the
//   point: the whole paste policy is pinned where nothing hides behind a message box (the controller adds only the
//   box on top of these three outcomes).
//
//   The cases that carry weight, as distinct from the ones that merely re-check CellPasteConverter (which has its own
//   suite):
//
//     - A container source into a COMPATIBLE column applies; into an INCOMPATIBLE one it is refused when jagged pastes
//       are off and asks for confirmation when they are on. Those three are the whole of the SET-05 flow.
//     - null is universal, and an untyped (null / provisional / missing) target takes a scalar as-is.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/cell_paste_plan.hpp"

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <memory>
#include <vector>

using namespace vje;

namespace
{
	std::unique_ptr<JsonNode> json ( const char* text )
	{
		return JsonParser::parse ( QString::fromUtf8 ( text ) ).root;
	}
}

class TestCellPastePlan : public QObject
{
	Q_OBJECT

private slots:

	void a_same_kind_scalar_applies ();
	void a_convertible_string_applies_else_refuses ();
	void null_is_universal ();
	void an_untyped_target_takes_a_scalar_as_is ();
	void an_incompatible_scalar_pairing_is_refused ();

	void a_shape_compatible_container_applies ();
	void a_shape_incompatible_container_is_refused_when_jagged_is_off ();
	void a_shape_incompatible_container_asks_when_jagged_is_on ();
	void an_empty_column_accepts_any_container ();
};

//---------------------------------------------------------------------------------------------------------------------
// Scalars
//---------------------------------------------------------------------------------------------------------------------

void TestCellPastePlan::a_same_kind_scalar_applies ()
{
	const std::unique_ptr<JsonNode> source = JsonNode::make_string ( QStringLiteral ( "hello" ) );

	CellPasteDecision decision = plan_cell_paste ( *source, CellTarget::String, {}, false );

	QCOMPARE ( static_cast<int> ( decision.plan ), static_cast<int> ( CellPastePlan::Apply ) );
	QVERIFY  ( decision.value != nullptr );
	QCOMPARE ( decision.value->string_value (), QStringLiteral ( "hello" ) );
}

void TestCellPastePlan::a_convertible_string_applies_else_refuses ()
{
	// A string into a number cell converts iff it is a valid JSON number (EDITOR-11).

	const std::unique_ptr<JsonNode> numeric = JsonNode::make_string ( QStringLiteral ( "42" ) );

	QCOMPARE ( static_cast<int> ( plan_cell_paste ( *numeric, CellTarget::Number, {}, false ).plan ),
	           static_cast<int> ( CellPastePlan::Apply ) );

	const std::unique_ptr<JsonNode> words = JsonNode::make_string ( QStringLiteral ( "not a number" ) );

	CellPasteDecision refused = plan_cell_paste ( *words, CellTarget::Number, {}, false );

	QCOMPARE ( static_cast<int> ( refused.plan ), static_cast<int> ( CellPastePlan::Incompatible ) );
	QVERIFY  ( !refused.message.isEmpty () );
	QVERIFY  ( refused.value == nullptr );
}

void TestCellPastePlan::null_is_universal ()
{
	// A null source pastes null into every column (EDITOR-11).

	const std::unique_ptr<JsonNode> source = JsonNode::make_null ();

	for ( const CellTarget target : { CellTarget::String, CellTarget::Number, CellTarget::Boolean, CellTarget::Object, CellTarget::Array, CellTarget::Untyped } )
	{
		CellPasteDecision decision = plan_cell_paste ( *source, target, {}, false );

		QCOMPARE ( static_cast<int> ( decision.plan ), static_cast<int> ( CellPastePlan::Apply ) );
		QVERIFY  ( ( decision.value != nullptr ) && ( decision.value->kind () == JsonKind::Null ) );
	}
}

void TestCellPastePlan::an_untyped_target_takes_a_scalar_as_is ()
{
	// A null / provisional / missing cell takes any scalar as-is, even if it makes the column type-heterogeneous.

	const std::unique_ptr<JsonNode> source = JsonNode::make_number ( QStringLiteral ( "1.50" ) );

	CellPasteDecision decision = plan_cell_paste ( *source, CellTarget::Untyped, {}, false );

	QCOMPARE ( static_cast<int> ( decision.plan ), static_cast<int> ( CellPastePlan::Apply ) );
	QCOMPARE ( decision.value->number_token (), QStringLiteral ( "1.50" ) );   // Raw token preserved (FILE-10).
}

void TestCellPastePlan::an_incompatible_scalar_pairing_is_refused ()
{
	// An object into a string cell has no conversion (deliberately stricter than EDIT-09), so it is refused.

	const std::unique_ptr<JsonNode> source = json ( R"({ "a": 1 })" );

	CellPasteDecision decision = plan_cell_paste ( *source, CellTarget::String, {}, false );

	QCOMPARE ( static_cast<int> ( decision.plan ), static_cast<int> ( CellPastePlan::Incompatible ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Containers -- the shape check and the jagged flow
//---------------------------------------------------------------------------------------------------------------------

void TestCellPastePlan::a_shape_compatible_container_applies ()
{
	const std::unique_ptr<JsonNode> columnValue = json ( R"({ "x": 1 })" );
	const std::unique_ptr<JsonNode> source      = json ( R"({ "x": 2 })" );

	const std::vector<const JsonNode*> columnValues { columnValue.get () };

	CellPasteDecision decision = plan_cell_paste ( *source, CellTarget::Object, columnValues, false );

	QCOMPARE ( static_cast<int> ( decision.plan ), static_cast<int> ( CellPastePlan::Apply ) );
	QVERIFY  ( decision.value != nullptr );
}

void TestCellPastePlan::a_shape_incompatible_container_is_refused_when_jagged_is_off ()
{
	const std::unique_ptr<JsonNode> columnValue = json ( R"({ "x": 1 })" );
	const std::unique_ptr<JsonNode> source      = json ( R"({ "y": 2 })" );   // Different key set.

	const std::vector<const JsonNode*> columnValues { columnValue.get () };

	CellPasteDecision decision = plan_cell_paste ( *source, CellTarget::Object, columnValues, false );

	QCOMPARE ( static_cast<int> ( decision.plan ), static_cast<int> ( CellPastePlan::Incompatible ) );
	QVERIFY  ( !decision.message.isEmpty () );
}

void TestCellPastePlan::a_shape_incompatible_container_asks_when_jagged_is_on ()
{
	const std::unique_ptr<JsonNode> columnValue = json ( R"({ "x": 1 })" );
	const std::unique_ptr<JsonNode> source      = json ( R"({ "y": 2 })" );

	const std::vector<const JsonNode*> columnValues { columnValue.get () };

	CellPasteDecision decision = plan_cell_paste ( *source, CellTarget::Object, columnValues, true );

	// SET-05 on: not silently applied and not refused -- the controller must warn and confirm first.

	QCOMPARE ( static_cast<int> ( decision.plan ), static_cast<int> ( CellPastePlan::NeedsJaggedConfirm ) );
	QVERIFY  ( decision.value != nullptr );
}

void TestCellPastePlan::an_empty_column_accepts_any_container ()
{
	// With no other values the column has no reference shape, so the check is waived (EDITOR-11).

	const std::unique_ptr<JsonNode> source = json ( R"([ 1, 2, 3 ])" );

	CellPasteDecision decision = plan_cell_paste ( *source, CellTarget::Array, {}, false );

	QCOMPARE ( static_cast<int> ( decision.plan ), static_cast<int> ( CellPastePlan::Apply ) );
}

QTEST_APPLESS_MAIN ( TestCellPastePlan )

#include "tst_cell_paste_plan.moc"
