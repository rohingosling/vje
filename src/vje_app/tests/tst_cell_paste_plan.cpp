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
	void a_convertible_string_applies_and_a_mismatch_asks ();
	void null_is_universal ();
	void an_untyped_target_takes_a_scalar_as_is ();
	void a_container_on_either_side_is_refused_rather_than_offered ();

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

void TestCellPastePlan::a_convertible_string_applies_and_a_mismatch_asks ()
{
	// A string into a number cell converts iff it is a valid JSON number (EDITOR-11).

	const std::unique_ptr<JsonNode> numeric = JsonNode::make_string ( QStringLiteral ( "42" ) );

	QCOMPARE ( static_cast<int> ( plan_cell_paste ( *numeric, CellTarget::Number, {}, false ).plan ),
	           static_cast<int> ( CellPastePlan::Apply ) );

	// AND A MISMATCH IS NOW A QUESTION RATHER THAN A VERDICT (EDITOR-11, revised 2026-08-20). This case used to
	// assert Incompatible with a null value, which is exactly the behaviour the change removes: overwriting a column
	// of numbers with a column of strings is something users do deliberately, and a modal that only says no leaves
	// them retyping by hand what they had already copied.

	const std::unique_ptr<JsonNode> words = JsonNode::make_string ( QStringLiteral ( "not a number" ) );

	CellPasteDecision offered = plan_cell_paste ( *words, CellTarget::Number, {}, false );

	QCOMPARE ( static_cast<int> ( offered.plan ), static_cast<int> ( CellPastePlan::NeedsTypeConfirm ) );
	QVERIFY  ( !offered.message.isEmpty () );

	// The value travels, and it is the SOURCE AS IT STANDS -- there was no conversion, so there is nothing else it
	// could be, and a confirmed paste landing anything else would be answering a different question from the one the
	// user was asked.

	QVERIFY  ( offered.value != nullptr );
	QCOMPARE ( offered.value->kind (), JsonKind::String );
	QCOMPARE ( offered.value->string_value (), QStringLiteral ( "not a number" ) );
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

void TestCellPastePlan::a_container_on_either_side_is_refused_rather_than_offered ()
{
	// THE BOUNDARY OF THE TYPE OVERRIDE, written as an opposing pair against the case above so that neither survives
	// a build which ignores the scalar test in either direction.
	//
	// A scalar over a scalar replaces a value with a value, which is a question worth asking. A scalar over a
	// CONTAINER destroys a subtree -- a delete wearing a paste's name, which is EDIT-09's own argument for why
	// "convert to object" is not offered -- and a container over a SCALAR creates structure in a column that has
	// none, which is the jagged question SET-05 already answers and would be answered twice, differently, if this
	// route let it through as well. Both keep the refusal they had.

	const std::unique_ptr<JsonNode> object = json ( R"({ "a": 1 })" );

	CellPasteDecision intoScalar = plan_cell_paste ( *object, CellTarget::String, {}, false );

	QCOMPARE ( static_cast<int> ( intoScalar.plan ), static_cast<int> ( CellPastePlan::Incompatible ) );
	QVERIFY  ( intoScalar.value == nullptr );

	const std::unique_ptr<JsonNode> text = JsonNode::make_string ( QStringLiteral ( "words" ) );

	CellPasteDecision intoContainer = plan_cell_paste ( *text, CellTarget::Object, {}, false );

	QCOMPARE ( static_cast<int> ( intoContainer.plan ), static_cast<int> ( CellPastePlan::Incompatible ) );
	QVERIFY  ( intoContainer.value == nullptr );
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
