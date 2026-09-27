//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for EDIT-16: the pure target-shape decision (editing/paste_target_plan) and the
//   UndoController::paste_value_list that applies it.
//
//   THE CROSS-ROUTE INVARIANT IS ASSERTED HERE AND CAN ONLY BE ASSERTED HERE. EDIT-16 requires that a
//   one-dimensional array give the same result whether it was selected in the tree or its single column was selected
//   in the table. There is no MainWindow harness (lesson D27), so a rule left in the window would have no case behind
//   it -- which is why the decision is a function over ( target, list name, target column ) and the two routes differ
//   only in whether they supply the third.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/paste_target_plan.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/services/JsonSerializer.hpp>

#include <QtTest/QtTest>

using namespace vje;

namespace
{
	std::unique_ptr<JsonNode> node_of ( const QString& json )
	{
		ParseResult result = JsonParser::parse ( json );
		Q_ASSERT ( result.ok );
		return std::move ( result.root );
	}

	// A pasted column: the values in order, a NULL QString meaning an ABSENT cell (a ragged array's missing member).

	std::vector<UndoController::PastedValue> values_of ( const QStringList& json )
	{
		std::vector<UndoController::PastedValue> values;

		for ( const QString& text : json )
		{
			values.push_back ( { text.isNull () ? nullptr : node_of ( text ) } );
		}

		return values;
	}

	struct Fixture
	{
		JsonDocument   document;
		UndoController controller { &document };

		void load ( const QString& json )
		{
			document.set_root ( node_of ( json ) );
			controller.clear ();
		}

		QString text () const { return JsonSerializer::serialize ( *document.root () ); }

		// Defaults to the NODE route, which is what a case is asking about unless it says otherwise -- the header
		// route is reached from the table and has exactly two cases of its own below.

		EditOutcome paste
		(
			const QString&                pointer,
			const QStringList&            json,
			const QString&                name,
			PasteRoute                    route   = PasteRoute::Node,
			const std::optional<QString>& column     = std::nullopt,
			QString*                      refusal    = nullptr,
			QString*                      pastedName = nullptr
		)
		{
			std::vector<UndoController::PastedValue> values = values_of ( json );

			return controller.paste_value_list ( JsonPointer::parse ( pointer ), values, name, route, column, refusal, pastedName );
		}
	};
}

class TestPasteTargetPlan : public QObject
{
	Q_OBJECT

private slots:

	// The decision.

	void a_scalar_target_is_refused_with_a_reason ();
	void an_object_takes_the_lists_own_name_and_deduplicates_only_on_collision ();
	void an_empty_array_takes_the_values_bare ();
	void every_shape_the_table_renders_single_column_is_one_dimensional ();
	void an_array_of_objects_takes_the_target_columns_name_where_the_route_has_one ();

	// The application.

	void a_column_pasted_into_an_object_arrives_as_one_member_holding_an_array ();
	void a_column_pasted_into_an_empty_array_creates_bare_elements ();
	void a_one_dimensional_array_is_overwritten_then_extended ();
	void an_array_of_objects_gains_a_column_filled_with_null_where_the_values_do_not_reach ();
	void an_existing_column_keeps_the_elements_the_values_do_not_reach ();
	void an_already_ragged_array_stays_as_ragged_as_it_was ();
	void appended_elements_carry_null_under_every_other_column ();
	void an_absent_value_leaves_an_existing_cell_and_writes_null_where_it_creates_one ();
	void the_whole_paste_is_one_undo_step ();
	void a_paste_that_creates_nothing_leaves_no_undo_step ();

	// The route decides (EDIT-16 as revised by Phase 15h.2).

	void the_two_routes_answer_differently_on_a_one_dimensional_array ();
	void a_one_dimensional_array_selected_in_the_tree_becomes_an_array_of_objects ();
	void the_existing_values_move_under_the_arrays_own_name ();
	void an_array_of_objects_deduplicates_rather_than_overwriting_on_the_node_route ();
	void a_conversion_is_one_undo_step ();

	// The header route's INSERT (EDITOR-18, revised 2026-09-23).

	void an_insert_goes_in_front_of_the_column_aimed_at ();
	void an_insert_never_writes_into_an_existing_cell ();
	void a_ragged_element_takes_the_new_member_where_the_table_shows_it ();
	void an_insert_into_a_single_column_array_reshapes_it_and_goes_first ();
	void an_insert_null_fills_a_short_source_and_grows_for_a_long_one ();
};

//=====================================================================================================================
// The decision
//=====================================================================================================================

void TestPasteTargetPlan::a_scalar_target_is_refused_with_a_reason ()
{
	const std::unique_ptr<JsonNode> scalar = JsonNode::make_number ( QStringLiteral ( "7" ) );

	const PasteTargetPlan plan = plan_paste_target ( scalar.get (), QStringLiteral ( "n" ), PasteRoute::Node );

	QVERIFY  ( plan.is_refused () );
	QVERIFY2 ( !plan.refusal.isEmpty (), "a refusal the user cannot see is not a refusal (VAL-05)" );

	// And nothing at all is refused rather than crashed into.

	QVERIFY ( plan_paste_target ( nullptr, QStringLiteral ( "n" ), PasteRoute::Node ).is_refused () );
}

void TestPasteTargetPlan::an_object_takes_the_lists_own_name_and_deduplicates_only_on_collision ()
{
	// THE SUFFIX IS A COLLISION MARKER, NOT A DECORATION (15h.2). An object with no `roles` member receives `roles`;
	// the suffix appears only where the name is taken, which is VAL-02's rule with BOTH halves reused rather than just
	// its sequence. Written as a PAIR, so neither half passes against a build that suffixes always or never.

	const std::unique_ptr<JsonNode> free = node_of ( QStringLiteral ( "{\"other\":1}" ) );

	QCOMPARE
	(
		plan_paste_target ( free.get (), QStringLiteral ( "roles" ), PasteRoute::Node ).name,
		QStringLiteral ( "roles" )
	);

	const std::unique_ptr<JsonNode> taken = node_of ( QStringLiteral ( "{\"roles\":1}" ) );

	QCOMPARE
	(
		plan_paste_target ( taken.get (), QStringLiteral ( "roles" ), PasteRoute::Node ).name,
		QStringLiteral ( "roles (copy)" )
	);

	// And the sequence runs on the BASE name, so a third paste is "(copy 2)" rather than "(copy) (copy)" -- the
	// reading VAL-02's wording could have supported.

	const std::unique_ptr<JsonNode> several =
		node_of ( QStringLiteral ( "{\"roles\":1,\"roles (copy)\":2,\"roles (copy 2)\":3}" ) );

	QCOMPARE
	(
		plan_paste_target ( several.get (), QStringLiteral ( "roles" ), PasteRoute::Node ).name,
		QStringLiteral ( "roles (copy 3)" )
	);
}

void TestPasteTargetPlan::an_empty_array_takes_the_values_bare ()
{
	const std::unique_ptr<JsonNode> empty = node_of ( QStringLiteral ( "[]" ) );

	const PasteTargetPlan plan = plan_paste_target ( empty.get (), QStringLiteral ( "name" ), PasteRoute::Node );

	QCOMPARE ( plan.kind, PasteTargetKind::EmptyArray );

	// The NAME is dropped, and that is the whole of the decision this case reverses: an empty array has no shape to
	// attach a name to, so the values stand as the elements.

	QVERIFY ( plan.name.isEmpty () );
}

void TestPasteTargetPlan::every_shape_the_table_renders_single_column_is_one_dimensional ()
{
	// EDIT-16's definition, asserted over all four shapes it names -- a scalar array, a mixed-kind array, an array of
	// arrays, and an array only PARTLY of objects. The last is the one a looser reading gets wrong.

	const QStringList singleColumn =
	{
		QStringLiteral ( "[\"a\",\"b\"]" ),
		QStringLiteral ( "[1,{\"a\":2}]" ),
		QStringLiteral ( "[[1],[2]]" ),
		QStringLiteral ( "[{\"a\":1},{\"b\":2},7]" )
	};

	for ( const QString& json : singleColumn )
	{
		const std::unique_ptr<JsonNode> array = node_of ( json );

		QVERIFY2 ( renders_single_column ( *array ), qPrintable ( json ) );

		// The SHAPE is what this case is about; what each route then does with it is the route cases below. Both
		// answers are asserted so a build that lost the distinction fails here too.

		QCOMPARE ( plan_paste_target ( array.get (), QStringLiteral ( "n" ), PasteRoute::Column ).kind,
		           PasteTargetKind::OneDimensionalArray );

		QCOMPARE ( plan_paste_target ( array.get (), QStringLiteral ( "n" ), PasteRoute::Node ).kind,
		           PasteTargetKind::ConvertToArrayOfObjects );
	}

	// And an array of objects is not.

	const std::unique_ptr<JsonNode> objects = node_of ( QStringLiteral ( "[{\"a\":1},{\"a\":2}]" ) );

	QVERIFY  ( !renders_single_column ( *objects ) );
	QCOMPARE ( plan_paste_target ( objects.get (), QStringLiteral ( "n" ), PasteRoute::Node ).kind,
	           PasteTargetKind::ArrayOfObjects );
}

void TestPasteTargetPlan::an_array_of_objects_takes_the_target_columns_name_where_the_route_has_one ()
{
	// The header route aimed at a named column, and that column's key wins -- overwriting it is what aiming at it
	// MEANS. From the tree nothing was aimed at, so the values arrive under the name they travel with.

	const std::unique_ptr<JsonNode> objects = node_of ( QStringLiteral ( "[{\"a\":1},{\"a\":2}]" ) );

	QCOMPARE ( plan_paste_target ( objects.get (), QStringLiteral ( "name" ), PasteRoute::Node ).name,
	           QStringLiteral ( "name" ) );

	QCOMPARE ( plan_paste_target ( objects.get (), QStringLiteral ( "name" ), PasteRoute::Column,
	                               QStringLiteral ( "label" ) ).name,
	           QStringLiteral ( "label" ) );
}

//=====================================================================================================================
// The application
//=====================================================================================================================

void TestPasteTargetPlan::a_column_pasted_into_an_object_arrives_as_one_member_holding_an_array ()
{
	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":{\"kept\":1}}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"zero\"", "\"one\"" }, QStringLiteral ( "name" ) ), EditOutcome::Applied );

	// `name`, NOT `name (copy)`: the object has no member of that name, so there is nothing to disambiguate from
	// (15h.2). Pasting a second time is what produces the suffix, which the next two lines assert rather than leave
	// to the decision-level case -- this is the route that actually writes it.

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":{\"kept\":1,\"name\":[\"zero\",\"one\"]}}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"two\"" }, QStringLiteral ( "name" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.text (),
	           QStringLiteral ( "{\"target\":{\"kept\":1,\"name\":[\"zero\",\"one\"],\"name (copy)\":[\"two\"]}}" ) );
}

void TestPasteTargetPlan::a_column_pasted_into_an_empty_array_creates_bare_elements ()
{
	// Requirement 2, and the reversal it carries: bare values, not objects keyed by the column's name.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[]}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"zero\"", "\"one\"", "\"two\"" }, QStringLiteral ( "name" ) ),
	           EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":[\"zero\",\"one\",\"two\"]}" ) );
}

void TestPasteTargetPlan::a_one_dimensional_array_is_overwritten_then_extended ()
{
	// THE COLUMN ROUTE. Overwrite-and-extend is what aiming at a one-dimensional array's single column means; from
	// the tree the same array is converted instead (15h.2), which is
	// the_two_routes_answer_differently_on_a_one_dimensional_array's claim.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[\"x\",\"y\"]}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"a\"", "\"b\"", "\"c\"" }, QStringLiteral ( "name" ), PasteRoute::Column ),
	           EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":[\"a\",\"b\",\"c\"]}" ) );

	// A SHORTER source fills what it covers and leaves the rest exactly as it was.

	fixture.load ( QStringLiteral ( "{\"target\":[\"x\",\"y\",\"z\"]}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"a\"" }, QStringLiteral ( "name" ), PasteRoute::Column ),
	           EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":[\"a\",\"y\",\"z\"]}" ) );
}

void TestPasteTargetPlan::an_array_of_objects_gains_a_column_filled_with_null_where_the_values_do_not_reach ()
{
	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[{\"a\":1},{\"a\":2},{\"a\":3}]}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"p\"", "\"q\"" }, QStringLiteral ( "b" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.text (),
	           QStringLiteral ( "{\"target\":[{\"a\":1,\"b\":\"p\"},{\"a\":2,\"b\":\"q\"},{\"a\":3,\"b\":null}]}" ) );
}

void TestPasteTargetPlan::an_existing_column_keeps_the_elements_the_values_do_not_reach ()
{
	// The distinction EDIT-16 makes load-bearing: a NEW column is filled with null, an EXISTING one is not -- filling
	// it would clear the tail and contradict EDITOR-18's shorter-source rule.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[{\"a\":1,\"b\":\"keep\"},{\"a\":2,\"b\":\"me\"},{\"a\":3,\"b\":\"too\"}]}" ) );

	// THE COLUMN ROUTE, which is now the only way to write onto a column the array already carries: from the tree the
	// same paste de-duplicates into `b (copy)` and leaves `b` alone (15h.2). The fill-scope distinction this case is
	// about therefore lives on the header route, which is where a user aims at a column meaning to replace it.

	QCOMPARE
	(
		fixture.paste ( "/target", { "\"p\"" }, QStringLiteral ( "b" ), PasteRoute::Column, QStringLiteral ( "b" ) ),
		EditOutcome::Applied
	);

	QCOMPARE ( fixture.text (),
	           QStringLiteral ( "{\"target\":[{\"a\":1,\"b\":\"p\"},{\"a\":2,\"b\":\"me\"},{\"a\":3,\"b\":\"too\"}]}" ) );
}

void TestPasteTargetPlan::an_already_ragged_array_stays_as_ragged_as_it_was ()
{
	// EDIT-16 in its own words. The array is RAGGED in the pasted column -- element 1 lacks "b" -- and the source is
	// shorter than the array, so element 1 is past the values AND missing the member. It must stay missing: a paste
	// that filled it would be doing Normalize's job (EDIT-11) inside a gesture that never asked for it.
	//
	// THE RAGGEDNESS IS WHAT MAKES THIS FALSIFIABLE. On a UNIFORM array the same rule is invisible, because an
	// insert over a member that already exists is refused by VAL-02 anyway -- so a build with the guard removed
	// produces an identical document and the case proves nothing. Found exactly that way, by a neutered build.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[{\"a\":1,\"b\":\"keep\"},{\"a\":2},{\"a\":3,\"b\":\"too\"}]}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"p\"" }, QStringLiteral ( "b" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.text (),
	           QStringLiteral ( "{\"target\":[{\"a\":1,\"b\":\"p\"},{\"a\":2},{\"a\":3,\"b\":\"too\"}]}" ) );
}

void TestPasteTargetPlan::appended_elements_carry_null_under_every_other_column ()
{
	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[{\"a\":1,\"b\":2}]}" ) );

	// The header route, the column `b` already existing -- see an_existing_column_keeps_the_elements... for why that
	// makes it the header's. What is asserted here is the APPENDED element, which carries null under every other
	// column the array has, and that is the same on both routes.

	QCOMPARE
	(
		fixture.paste ( "/target", { "\"p\"", "\"q\"" }, QStringLiteral ( "b" ), PasteRoute::Column, QStringLiteral ( "b" ) ),
		EditOutcome::Applied
	);

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":[{\"a\":1,\"b\":\"p\"},{\"a\":null,\"b\":\"q\"}]}" ) );
}

void TestPasteTargetPlan::an_absent_value_leaves_an_existing_cell_and_writes_null_where_it_creates_one ()
{
	// EDIT-16's two halves of the absent rule, which differ by whether the slot already exists.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[\"x\",\"y\",\"z\"]}" ) );

	// The COLUMN route: "leaves an existing cell" is a claim about overwriting in place, which is that route's job.

	QCOMPARE ( fixture.paste ( "/target", { "\"a\"", QString (), "\"c\"" }, QStringLiteral ( "n" ), PasteRoute::Column ),
	           EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":[\"a\",\"y\",\"c\"]}" ) );

	// Past the end there is no cell to leave alone, so the absent value writes null.

	fixture.load ( QStringLiteral ( "{\"target\":[\"x\"]}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"a\"", QString () }, QStringLiteral ( "n" ), PasteRoute::Column ),
	           EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":[\"a\",null]}" ) );
}

void TestPasteTargetPlan::the_whole_paste_is_one_undo_step ()
{
	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[{\"a\":1},{\"a\":2}]}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"p\"", "\"q\"", "\"r\"", "\"s\"" }, QStringLiteral ( "b" ) ),
	           EditOutcome::Applied );

	// Two overwrites and two appended elements: one gesture, one step.

	QCOMPARE ( fixture.controller.stack ()->count (), 1 );

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"target\":[{\"a\":1},{\"a\":2}]}" ) );
}

void TestPasteTargetPlan::a_paste_that_creates_nothing_leaves_no_undo_step ()
{
	// Every value absent over cells that already exist: nothing to do, and an undo step that undoes nothing would be
	// lesson D19's defect arriving by another route.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"target\":[\"x\",\"y\"]}" ) );

	// THE COLUMN ROUTE, deliberately. "Creates nothing" is a property of the overwrite branch: every value is absent
	// and every target cell already exists, so there is nothing to write. On the NODE route the same paste converts
	// the array (15h.2), which is emphatically something -- a different claim, covered by a_conversion_is_one_undo_step.

	QCOMPARE ( fixture.paste ( "/target", { QString (), QString () }, QStringLiteral ( "n" ), PasteRoute::Column ),
	           EditOutcome::Unchanged );
	QCOMPARE ( fixture.controller.stack ()->count (), 0 );
	QVERIFY  ( fixture.controller.is_clean () );

	// And a refused target reports the reason rather than silently doing nothing.

	QString refusal;

	fixture.load ( QStringLiteral ( "{\"target\":7}" ) );

	QCOMPARE ( fixture.paste ( "/target", { "\"a\"" }, QStringLiteral ( "n" ), PasteRoute::Node, std::nullopt, &refusal ),
	           EditOutcome::Rejected );

	QVERIFY ( !refusal.isEmpty () );
}

//=====================================================================================================================
// EDIT-16's cross-route invariant
//=====================================================================================================================

void TestPasteTargetPlan::the_two_routes_answer_differently_on_a_one_dimensional_array ()
{
	// SUPERSEDES `the_two_routes_agree_on_a_one_dimensional_array`, which asserted the opposite and was right until
	// 15h.2. That requirement held only while neither route could reshape the array; now a TREE selection names an
	// array and asks what it should become, while a COLUMN selection names a column and asks what it should hold.
	//
	// Both routes reach this target with NO column key -- a single-column table has a name but no member key -- so
	// this case is also what makes the explicit route parameter necessary rather than merely tidy: with the route
	// inferred from that absence, the two sides below are indistinguishable and one of them has to be wrong.

	Fixture treeRoute;
	treeRoute.load ( QStringLiteral ( "{\"data\":[\"x\",\"y\"]}" ) );

	QCOMPARE
	(
		treeRoute.paste ( "/data", { "\"a\"", "\"b\"" }, QStringLiteral ( "name" ), PasteRoute::Node ),
		EditOutcome::Applied
	);

	Fixture headerRoute;
	headerRoute.load ( QStringLiteral ( "{\"data\":[\"x\",\"y\"]}" ) );

	QCOMPARE
	(
		headerRoute.paste ( "/data", { "\"a\"", "\"b\"" }, QStringLiteral ( "name" ), PasteRoute::Column, std::nullopt ),
		EditOutcome::Applied
	);

	QCOMPARE ( treeRoute.text   (),
	           QStringLiteral ( "{\"data\":[{\"data\":\"x\",\"name\":\"a\"},{\"data\":\"y\",\"name\":\"b\"}]}" ) );

	QCOMPARE ( headerRoute.text (), QStringLiteral ( "{\"data\":[\"a\",\"b\"]}" ) );

	QVERIFY2 ( treeRoute.text () != headerRoute.text (),
	           "the two routes are REQUIRED to differ here, not merely permitted to" );
}

void TestPasteTargetPlan::a_one_dimensional_array_selected_in_the_tree_becomes_an_array_of_objects ()
{
	// The four shapes EDIT-16 calls one-dimensional, converted. The last two are the ones a looser implementation
	// gets wrong: an array of ARRAYS must wrap its elements rather than merge into them, and an array only PARTLY of
	// objects must leave the objects it already has ALONE rather than wrapping them a second time.

	Fixture scalars;
	scalars.load  ( QStringLiteral ( "{\"data\":[\"x\",\"y\"]}" ) );
	scalars.paste ( "/data", { "\"a\"", "\"b\"" }, QStringLiteral ( "name" ) );
	QCOMPARE ( scalars.text (), QStringLiteral ( "{\"data\":[{\"data\":\"x\",\"name\":\"a\"},{\"data\":\"y\",\"name\":\"b\"}]}" ) );

	Fixture mixed;
	mixed.load  ( QStringLiteral ( "{\"data\":[1,true]}" ) );
	mixed.paste ( "/data", { "\"a\"", "\"b\"" }, QStringLiteral ( "name" ) );
	QCOMPARE ( mixed.text (), QStringLiteral ( "{\"data\":[{\"data\":1,\"name\":\"a\"},{\"data\":true,\"name\":\"b\"}]}" ) );

	Fixture arrays;
	arrays.load  ( QStringLiteral ( "{\"data\":[[1],[2]]}" ) );
	arrays.paste ( "/data", { "\"a\"", "\"b\"" }, QStringLiteral ( "name" ) );
	QCOMPARE ( arrays.text (), QStringLiteral ( "{\"data\":[{\"data\":[1],\"name\":\"a\"},{\"data\":[2],\"name\":\"b\"}]}" ) );

	// PARTLY of objects: element 0 keeps its own members and gains the column; element 1 is wrapped first.

	Fixture partly;
	partly.load  ( QStringLiteral ( "{\"data\":[{\"k\":1},7]}" ) );
	partly.paste ( "/data", { "\"a\"", "\"b\"" }, QStringLiteral ( "name" ) );
	QCOMPARE ( partly.text (), QStringLiteral ( "{\"data\":[{\"k\":1,\"name\":\"a\"},{\"data\":7,\"name\":\"b\"}]}" ) );
}

void TestPasteTargetPlan::the_existing_values_move_under_the_arrays_own_name ()
{
	// Section 2.12's rule, which is column_naming::single_column_name unchanged -- the SAME rule that named the
	// column on the way out, so the two can never drift into naming one column two ways.

	Fixture named;
	named.load  ( QStringLiteral ( "{\"roles\":[\"x\"]}" ) );
	named.paste ( "/roles", { "\"a\"" }, QStringLiteral ( "name" ) );
	QCOMPARE ( named.text (), QStringLiteral ( "{\"roles\":[{\"roles\":\"x\",\"name\":\"a\"}]}" ) );

	// An array that is itself an ELEMENT has no key of its own, so the fallback is the literal `value`.

	Fixture unnamed;
	unnamed.load  ( QStringLiteral ( "{\"outer\":[[\"x\"]]}" ) );
	unnamed.paste ( "/outer/0", { "\"a\"" }, QStringLiteral ( "name" ) );
	QCOMPARE ( unnamed.text (), QStringLiteral ( "{\"outer\":[[{\"value\":\"x\",\"name\":\"a\"}]]}" ) );

	// And where the two names COLLIDE -- a `data` column pasted onto the `data` array -- the values already there
	// keep the plain name and the INCOMING column takes the suffix. They were there first.

	Fixture collision;
	collision.load  ( QStringLiteral ( "{\"data\":[\"x\"]}" ) );
	collision.paste ( "/data", { "\"a\"" }, QStringLiteral ( "data" ) );
	QCOMPARE ( collision.text (), QStringLiteral ( "{\"data\":[{\"data\":\"x\",\"data (copy)\":\"a\"}]}" ) );
}

void TestPasteTargetPlan::an_array_of_objects_deduplicates_rather_than_overwriting_on_the_node_route ()
{
	// 15h.2 reverses EDIT-16's overwrite branch on the NODE route: nothing was aimed at, so nothing asked for a
	// column to be replaced. The existing column must survive UNTOUCHED, which is the half a build that merely
	// renamed the incoming column would still get wrong.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"rows\":[{\"name\":\"old\"}]}" ) );

	QCOMPARE ( fixture.paste ( "/rows", { "\"new\"" }, QStringLiteral ( "name" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"rows\":[{\"name\":\"old\",\"name (copy)\":\"new\"}]}" ) );

	// A column the array carries only PARTLY is ragged rather than present, so it is not a collision -- the paste
	// completes it under its own name. Asked of the array as a whole, which is what array_carries_column states.

	Fixture ragged;
	ragged.load ( QStringLiteral ( "{\"rows\":[{\"name\":\"old\"},{\"other\":1}]}" ) );

	QCOMPARE ( ragged.paste ( "/rows", { "\"a\"", "\"b\"" }, QStringLiteral ( "name" ) ), EditOutcome::Applied );

	QCOMPARE ( ragged.text (), QStringLiteral ( "{\"rows\":[{\"name\":\"a\"},{\"other\":1,\"name\":\"b\"}]}" ) );

	// The HEADER route still overwrites the column it aimed at -- the opposite answer, from the same clipboard, and
	// the reason this is a route split rather than a rule change.

	Fixture header;
	header.load ( QStringLiteral ( "{\"rows\":[{\"name\":\"old\"}]}" ) );

	QCOMPARE
	(
		header.paste ( "/rows", { "\"new\"" }, QStringLiteral ( "name" ), PasteRoute::Column, QStringLiteral ( "name" ) ),
		EditOutcome::Applied
	);

	QCOMPARE ( header.text (), QStringLiteral ( "{\"rows\":[{\"name\":\"new\"}]}" ) );
}

void TestPasteTargetPlan::a_conversion_is_one_undo_step ()
{
	// The conversion is a reshape AND a column paste -- two operations over every element -- and the user made one
	// gesture. Q43's lazily-opened MacroScope covers both, and Q49's nesting rule is what stops the inner
	// paste_column_into_objects from closing the group early.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"data\":[\"x\",\"y\",\"z\"]}" ) );

	QCOMPARE ( fixture.paste ( "/data", { "\"a\"", "\"b\"", "\"c\"" }, QStringLiteral ( "name" ) ), EditOutcome::Applied );

	QCOMPARE ( fixture.controller.stack ()->count (), 1 );

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), QStringLiteral ( "{\"data\":[\"x\",\"y\",\"z\"]}" ) );
}

//=====================================================================================================================
// The header route's INSERT (EDITOR-18, revised 2026-09-23)
//=====================================================================================================================

void TestPasteTargetPlan::an_insert_goes_in_front_of_the_column_aimed_at ()
{
	// Paste on a selected column INSERTS a new column in front of it -- the user's choice over after -- and the column
	// aimed at keeps every value it had. Written as an opposing pair with the node route, which pastes the same values
	// under the same name LAST, so a build that ignored the position cannot pass both halves.

	const QString array = QStringLiteral ( "{\"rows\":[{\"a\":1,\"b\":2},{\"a\":3,\"b\":4}]}" );

	Fixture insert;
	insert.load ( array );

	QString pastedName;

	QCOMPARE
	(
		insert.paste
		(
			"/rows", { "\"x\"", "\"y\"" }, QStringLiteral ( "n" ), PasteRoute::InsertColumn, QStringLiteral ( "b" ),
			nullptr, &pastedName
		),
		EditOutcome::Applied
	);

	QCOMPARE ( insert.text (), QStringLiteral ( "{\"rows\":[{\"a\":1,\"n\":\"x\",\"b\":2},{\"a\":3,\"n\":\"y\",\"b\":4}]}" ) );
	QCOMPARE ( pastedName, QStringLiteral ( "n" ) );

	// One undo step for the column, whatever it touched.

	QCOMPARE ( insert.controller.stack ()->count (), 1 );

	Fixture node;
	node.load ( array );

	QCOMPARE ( node.paste ( "/rows", { "\"x\"", "\"y\"" }, QStringLiteral ( "n" ) ), EditOutcome::Applied );

	QCOMPARE ( node.text (), QStringLiteral ( "{\"rows\":[{\"a\":1,\"b\":2,\"n\":\"x\"},{\"a\":3,\"b\":4,\"n\":\"y\"}]}" ) );
}

void TestPasteTargetPlan::an_insert_never_writes_into_an_existing_cell ()
{
	// The node route asks whether EVERY element carries a name, so a ragged column's name reads as free and the paste
	// completes that column -- writing into the elements that already have it (pinned as the node route's answer in
	// an_array_of_objects_deduplicates_rather_than_overwriting_on_the_node_route). An INSERT must never write into a
	// cell that exists, so for it ONE element carrying the name is enough to take it: the same array, the same name,
	// the opposite answer.

	const std::unique_ptr<JsonNode> ragged = node_of ( QStringLiteral ( "[{\"name\":\"old\"},{\"other\":1}]" ) );

	QCOMPARE ( plan_paste_target ( ragged.get (), QStringLiteral ( "name" ), PasteRoute::Node ).name, QStringLiteral ( "name" ) );

	QCOMPARE
	(
		plan_paste_target ( ragged.get (), QStringLiteral ( "name" ), PasteRoute::InsertColumn, QStringLiteral ( "name" ) ).name,
		QStringLiteral ( "name (copy)" )
	);

	// And applied: the old value survives, and the element that lacked `name` takes the new member where the table
	// shows the column -- in front of the next column along, since it has no `name` to stand in front of.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"rows\":[{\"name\":\"old\"},{\"other\":1}]}" ) );

	QString pastedName;

	QCOMPARE
	(
		fixture.paste
		(
			"/rows", { "\"a\"", "\"b\"" }, QStringLiteral ( "name" ), PasteRoute::InsertColumn, QStringLiteral ( "name" ),
			nullptr, &pastedName
		),
		EditOutcome::Applied
	);

	QCOMPARE
	(
		fixture.text (),
		QStringLiteral ( "{\"rows\":[{\"name (copy)\":\"a\",\"name\":\"old\"},{\"name (copy)\":\"b\",\"other\":1}]}" )
	);

	// The name the values were written under is REPORTED, suffix and all, so the caller can find the new column
	// without working the name out a second time.

	QCOMPARE ( pastedName, QStringLiteral ( "name (copy)" ) );
}

void TestPasteTargetPlan::a_ragged_element_takes_the_new_member_where_the_table_shows_it ()
{
	// Element 1 lacks the column aimed at (`b`). Appending the new member there would put it after `c` in that
	// element -- harmless to the table, which orders columns by first appearance, but not where anyone reading the
	// element would expect it. It goes in front of the next column along that the element DOES have.

	Fixture fixture;
	fixture.load ( QStringLiteral ( "{\"rows\":[{\"a\":1,\"b\":2,\"c\":3},{\"a\":4,\"c\":6}]}" ) );

	QCOMPARE
	(
		fixture.paste ( "/rows", { "\"x\"", "\"y\"" }, QStringLiteral ( "n" ), PasteRoute::InsertColumn, QStringLiteral ( "b" ) ),
		EditOutcome::Applied
	);

	QCOMPARE
	(
		fixture.text (),
		QStringLiteral ( "{\"rows\":[{\"a\":1,\"n\":\"x\",\"b\":2,\"c\":3},{\"a\":4,\"n\":\"y\",\"c\":6}]}" )
	);
}

void TestPasteTargetPlan::an_insert_into_a_single_column_array_reshapes_it_and_goes_first ()
{
	// A single-column array has one column, so a second one means the array becomes an array of objects -- 15h.2's
	// reshape, the existing values moving under the array's own name -- with the inserted column FIRST, in front of
	// the one the header aimed at. The opposing half is Paste Over on the same array, which overwrites the values
	// and leaves it an array of values.

	const QString array = QStringLiteral ( "{\"data\":[\"p\",\"q\"]}" );

	Fixture insert;
	insert.load ( array );

	QCOMPARE ( insert.paste ( "/data", { "\"x\"", "\"y\"" }, QStringLiteral ( "n" ), PasteRoute::InsertColumn ), EditOutcome::Applied );

	QCOMPARE ( insert.text (), QStringLiteral ( "{\"data\":[{\"n\":\"x\",\"data\":\"p\"},{\"n\":\"y\",\"data\":\"q\"}]}" ) );

	Fixture over;
	over.load ( array );

	QCOMPARE ( over.paste ( "/data", { "\"x\"", "\"y\"" }, QStringLiteral ( "n" ), PasteRoute::Column ), EditOutcome::Applied );

	QCOMPARE ( over.text (), QStringLiteral ( "{\"data\":[\"x\",\"y\"]}" ) );

	// An array only PARTLY of objects is single-column too, and its object elements keep their members through the
	// reshape -- so an insert has to miss those names as well as the array's own.

	const std::unique_ptr<JsonNode> partly = node_of ( QStringLiteral ( "[\"p\",{\"n\":1}]" ) );

	QCOMPARE ( plan_paste_target ( partly.get (), QStringLiteral ( "n" ), PasteRoute::Node ).name, QStringLiteral ( "n" ) );
	QCOMPARE ( plan_paste_target ( partly.get (), QStringLiteral ( "n" ), PasteRoute::InsertColumn ).name, QStringLiteral ( "n (copy)" ) );
}

void TestPasteTargetPlan::an_insert_null_fills_a_short_source_and_grows_for_a_long_one ()
{
	// A NEW column exists on every element, so where the source runs short the rest are null -- and where it runs long
	// the array grows, the appended elements carrying null under every other column. Both are the node route's rules
	// for a new column (EDIT-16), which an inserted column is.

	const QString array = QStringLiteral ( "{\"rows\":[{\"a\":1},{\"a\":2}]}" );

	Fixture shortSource;
	shortSource.load ( array );

	QCOMPARE ( shortSource.paste ( "/rows", { "\"x\"" }, QStringLiteral ( "n" ), PasteRoute::InsertColumn, QStringLiteral ( "a" ) ), EditOutcome::Applied );

	QCOMPARE ( shortSource.text (), QStringLiteral ( "{\"rows\":[{\"n\":\"x\",\"a\":1},{\"n\":null,\"a\":2}]}" ) );

	Fixture longSource;
	longSource.load ( array );

	QCOMPARE
	(
		longSource.paste ( "/rows", { "\"x\"", "\"y\"", "\"z\"" }, QStringLiteral ( "n" ), PasteRoute::InsertColumn, QStringLiteral ( "a" ) ),
		EditOutcome::Applied
	);

	QCOMPARE
	(
		longSource.text (),
		QStringLiteral ( "{\"rows\":[{\"n\":\"x\",\"a\":1},{\"n\":\"y\",\"a\":2},{\"n\":\"z\",\"a\":null}]}" )
	);
}

QTEST_GUILESS_MAIN ( TestPasteTargetPlan )

#include "tst_paste_target_plan.moc"
