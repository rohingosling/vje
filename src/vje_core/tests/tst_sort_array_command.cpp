//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for EDIT-15's array sort: the total order over JSON values (editing/json_order), the stable
//   permutation it produces, and SortArrayCommand / UndoController::sort_array applying that permutation as ONE
//   undo step.
//
//   The order and the command are tested through DIFFERENT doors on purpose. The permutation is asserted directly,
//   because a permutation compared against a re-serialized document can be right about every element and wrong
//   about their order in ways the text hides when values repeat -- the same reason tst_tree_drop_plan replays its
//   moves rather than matching index pairs. The command is asserted against the serialized document, because what
//   it must get right is that the document ends up in that order and comes back exactly.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/json_order.hpp>
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

	struct Fixture
	{
		JsonDocument   document;
		UndoController controller { &document };

		void load ( const QString& json )
		{
			document.set_root ( node_of ( json ) );
			controller.clear ();
		}

		QString text () const
		{
			return JsonSerializer::serialize ( *document.root () );
		}

		JsonPointer array_pointer () const
		{
			return JsonPointer::parse ( QStringLiteral ( "/items" ) );
		}

		EditOutcome sort ( const std::optional<QString>& key, Qt::SortOrder order )
		{
			return controller.sort_array ( array_pointer (), key, order );
		}
	};

	// The permutation for one column of a standalone array, with no document or undo stack in the way.

	std::vector<int> permutation_of ( const QString& json, const std::optional<QString>& key, Qt::SortOrder order )
	{
		const std::unique_ptr<JsonNode> array = node_of ( json );

		return json_order::sort_permutation ( *array, key, order );
	}

	const std::optional<QString> BY_ELEMENT = std::nullopt;
}

class TestSortArrayCommand : public QObject
{
	Q_OBJECT

private slots:

	// json_order -- the comparison.

	void the_cross_kind_order_is_null_boolean_number_string_array_object ();
	void an_absent_value_compares_exactly_as_null_does ();
	void numbers_compare_numerically_rather_than_as_text ();
	void two_containers_of_the_same_kind_compare_equal ();

	// json_order -- the permutation.

	void equal_keys_keep_document_order_when_ascending ();
	void equal_keys_keep_document_order_when_descending_too ();
	void a_missing_member_sorts_with_the_nulls ();
	void an_element_that_is_not_an_object_has_no_member_to_sort_by ();
	void a_scalar_array_sorts_by_its_elements ();
	void an_empty_member_key_is_not_the_by_element_case ();

	// SortArrayCommand / UndoController::sort_array.

	void a_sort_reorders_the_document ();
	void a_sort_is_one_undo_step_whatever_it_moved ();
	void undo_restores_the_original_order_exactly ();
	void raw_number_tokens_survive_the_sort_untouched ();
	void a_ragged_array_sorts_rather_than_being_refused ();
	void an_array_already_in_that_order_is_unchanged ();
	void an_array_of_fewer_than_two_elements_is_unchanged ();
	void a_pointer_that_is_not_an_array_is_rejected ();
	void redo_puts_the_sorted_order_back ();
};

//=====================================================================================================================
// json_order -- the comparison
//=====================================================================================================================

void TestSortArrayCommand::the_cross_kind_order_is_null_boolean_number_string_array_object ()
{
	// EDIT-15's order, asserted as a chain of strict comparisons rather than as ranks, so it stays a claim about the
	// ORDER rather than about the implementation's numbering.

	const std::unique_ptr<JsonNode> nullValue    = JsonNode::make_null ();
	const std::unique_ptr<JsonNode> falseValue   = JsonNode::make_boolean ( false );
	const std::unique_ptr<JsonNode> trueValue    = JsonNode::make_boolean ( true );
	const std::unique_ptr<JsonNode> numberValue  = JsonNode::make_number ( QStringLiteral ( "0" ) );
	const std::unique_ptr<JsonNode> stringValue  = JsonNode::make_string ( QString () );
	const std::unique_ptr<JsonNode> arrayValue   = JsonNode::make_array ();
	const std::unique_ptr<JsonNode> objectValue  = JsonNode::make_object ();

	QVERIFY ( json_order::compare_values ( nullValue.get   (), falseValue.get  () ) < 0 );
	QVERIFY ( json_order::compare_values ( falseValue.get  (), trueValue.get   () ) < 0 );
	QVERIFY ( json_order::compare_values ( trueValue.get   (), numberValue.get () ) < 0 );
	QVERIFY ( json_order::compare_values ( numberValue.get (), stringValue.get () ) < 0 );
	QVERIFY ( json_order::compare_values ( stringValue.get (), arrayValue.get  () ) < 0 );
	QVERIFY ( json_order::compare_values ( arrayValue.get  (), objectValue.get () ) < 0 );

	// And it is a strict order, not merely a sequence of "less than" answers: the reverse of each is positive.

	QVERIFY ( json_order::compare_values ( objectValue.get (), nullValue.get () ) > 0 );
}

void TestSortArrayCommand::an_absent_value_compares_exactly_as_null_does ()
{
	const std::unique_ptr<JsonNode> nullValue  = JsonNode::make_null ();
	const std::unique_ptr<JsonNode> falseValue = JsonNode::make_boolean ( false );

	QCOMPARE ( json_order::compare_values ( nullptr, nullValue.get () ), 0 );
	QCOMPARE ( json_order::compare_values ( nullValue.get (), nullptr ), 0 );
	QCOMPARE ( json_order::compare_values ( nullptr, nullptr ),          0 );

	QVERIFY ( json_order::compare_values ( nullptr, falseValue.get () ) < 0 );
}

void TestSortArrayCommand::numbers_compare_numerically_rather_than_as_text ()
{
	// The three pairs that a text comparison gets wrong: 9 before 10, 1.50 equal to 1.5, and 1e3 above both.

	const std::unique_ptr<JsonNode> nine       = JsonNode::make_number ( QStringLiteral ( "9" ) );
	const std::unique_ptr<JsonNode> ten        = JsonNode::make_number ( QStringLiteral ( "10" ) );
	const std::unique_ptr<JsonNode> onePointFive = JsonNode::make_number ( QStringLiteral ( "1.5" ) );
	const std::unique_ptr<JsonNode> onePointFifty = JsonNode::make_number ( QStringLiteral ( "1.50" ) );
	const std::unique_ptr<JsonNode> thousand   = JsonNode::make_number ( QStringLiteral ( "1e3" ) );

	QVERIFY  ( json_order::compare_values ( nine.get (), ten.get () ) < 0 );
	QCOMPARE ( json_order::compare_values ( onePointFive.get (), onePointFifty.get () ), 0 );
	QVERIFY  ( json_order::compare_values ( ten.get (), thousand.get () ) < 0 );
}

void TestSortArrayCommand::two_containers_of_the_same_kind_compare_equal ()
{
	// Two subtrees have no order this application is willing to claim, so they tie and the stable sort keeps them as
	// it found them. Contents deliberately differ, so a comparison that looked inside them would not answer zero.

	const std::unique_ptr<JsonNode> shortArray = node_of ( QStringLiteral ( "[1]" ) );
	const std::unique_ptr<JsonNode> longArray  = node_of ( QStringLiteral ( "[1,2,3]" ) );
	const std::unique_ptr<JsonNode> objectA    = node_of ( QStringLiteral ( R"({"a":1})" ) );
	const std::unique_ptr<JsonNode> objectB    = node_of ( QStringLiteral ( R"({"z":9})" ) );

	QCOMPARE ( json_order::compare_values ( shortArray.get (), longArray.get () ), 0 );
	QCOMPARE ( json_order::compare_values ( objectA.get (),    objectB.get ()  ), 0 );
}

//=====================================================================================================================
// json_order -- the permutation
//=====================================================================================================================

void TestSortArrayCommand::equal_keys_keep_document_order_when_ascending ()
{
	// Every element carries the same key, so a stable sort must return the identity. An unstable sort is free to
	// return anything here, which is what makes this the case that pins stability rather than merely exercising it.

	const QString json = R"([{"k":1,"t":"a"},{"k":1,"t":"b"},{"k":1,"t":"c"},{"k":1,"t":"d"}])";

	const std::vector<int> permutation = permutation_of ( json, QStringLiteral ( "k" ), Qt::AscendingOrder );

	QCOMPARE ( permutation, ( std::vector<int> { 0, 1, 2, 3 } ) );
}

void TestSortArrayCommand::equal_keys_keep_document_order_when_descending_too ()
{
	// EDIT-15: descending negates the VALUE comparison only, so equal elements are still equal under it and keep
	// document order. A descending sort implemented by sorting ascending and reversing would return { 3, 2, 1, 0 }.

	const QString json = R"([{"k":1,"t":"a"},{"k":1,"t":"b"},{"k":1,"t":"c"},{"k":1,"t":"d"}])";

	const std::vector<int> permutation = permutation_of ( json, QStringLiteral ( "k" ), Qt::DescendingOrder );

	QCOMPARE ( permutation, ( std::vector<int> { 0, 1, 2, 3 } ) );

	// And with two distinct keys, the ties within each group still hold their document order while the groups swap.

	const QString grouped = R"([{"k":1,"t":"a"},{"k":2,"t":"b"},{"k":1,"t":"c"},{"k":2,"t":"d"}])";

	QCOMPARE
	(
		permutation_of ( grouped, QStringLiteral ( "k" ), Qt::DescendingOrder ),
		( std::vector<int> { 1, 3, 0, 2 } )
	);
}

void TestSortArrayCommand::a_missing_member_sorts_with_the_nulls ()
{
	// EDIT-15: a missing member sorts as null, so it lands below every boolean, number and string rather than the
	// command refusing the ragged column. Elements 1 and 3 lack "k" and element 2 carries an explicit null; all
	// three tie, so document order decides among them.

	const QString json = R"([{"k":5},{"x":0},{"k":null},{"x":1},{"k":2}])";

	const std::vector<int> permutation = permutation_of ( json, QStringLiteral ( "k" ), Qt::AscendingOrder );

	QCOMPARE ( permutation, ( std::vector<int> { 1, 2, 3, 4, 0 } ) );
}

void TestSortArrayCommand::an_element_that_is_not_an_object_has_no_member_to_sort_by ()
{
	// A mixed array: the scalar and the sub-array cannot carry "k" at all, so both are absent and sort as null.

	const QString json = R"([{"k":2},7,{"k":1},[9]])";

	const std::vector<int> permutation = permutation_of ( json, QStringLiteral ( "k" ), Qt::AscendingOrder );

	QCOMPARE ( permutation, ( std::vector<int> { 1, 3, 2, 0 } ) );
}

void TestSortArrayCommand::a_scalar_array_sorts_by_its_elements ()
{
	// The single-column table's one column IS the elements, which is what std::nullopt names.

	QCOMPARE
	(
		permutation_of ( R"(["pear","apple","fig"])", BY_ELEMENT, Qt::AscendingOrder ),
		( std::vector<int> { 1, 2, 0 } )
	);

	// And across kinds, without a member in sight.

	QCOMPARE
	(
		permutation_of ( R"([ "s", 3, null, true ])", BY_ELEMENT, Qt::AscendingOrder ),
		( std::vector<int> { 2, 3, 1, 0 } )
	);
}

void TestSortArrayCommand::an_empty_member_key_is_not_the_by_element_case ()
{
	// "" is a legal JSON member key, which is why the column is an optional rather than an empty-string sentinel.
	// Sorting by the member "" must read the members; sorting by the element must read the elements. Same array,
	// two different answers -- so a sentinel implementation cannot pass both halves.

	const QString json = R"([{"":3},{"":1},{"":2}])";

	QCOMPARE
	(
		permutation_of ( json, QStringLiteral ( "" ), Qt::AscendingOrder ),
		( std::vector<int> { 1, 2, 0 } )
	);

	// By element, all three are objects and therefore tie, so the identity comes back.

	QCOMPARE ( permutation_of ( json, BY_ELEMENT, Qt::AscendingOrder ), ( std::vector<int> { 0, 1, 2 } ) );
}

//=====================================================================================================================
// SortArrayCommand / UndoController::sort_array
//=====================================================================================================================

void TestSortArrayCommand::a_sort_reorders_the_document ()
{
	Fixture fixture;
	fixture.load ( R"({"items":[{"n":"c"},{"n":"a"},{"n":"b"}]})" );

	QCOMPARE ( fixture.sort ( QStringLiteral ( "n" ), Qt::AscendingOrder ), EditOutcome::Applied );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[{"n":"a"},{"n":"b"},{"n":"c"}]})" ) );

	QCOMPARE ( fixture.sort ( QStringLiteral ( "n" ), Qt::DescendingOrder ), EditOutcome::Applied );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[{"n":"c"},{"n":"b"},{"n":"a"}]})" ) );
}

void TestSortArrayCommand::a_sort_is_one_undo_step_whatever_it_moved ()
{
	// Eight elements, every one of them moving. Expressing the sort as a run of move_child commands would leave
	// seven or more steps on the stack; EDIT-15 says one gesture is one step.

	Fixture fixture;
	fixture.load ( R"({"items":[8,7,6,5,4,3,2,1]})" );

	QCOMPARE ( fixture.sort ( BY_ELEMENT, Qt::AscendingOrder ), EditOutcome::Applied );

	QCOMPARE ( fixture.controller.stack ()->count (), 1 );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[1,2,3,4,5,6,7,8]})" ) );
}

void TestSortArrayCommand::undo_restores_the_original_order_exactly ()
{
	// The elements are distinguishable from one another, so a permutation that is right about the multiset and wrong
	// about the arrangement -- which the inverse being computed the wrong way round would produce -- fails here.

	const QString original = R"({"items":[{"n":"delta"},{"n":"alpha"},{"n":"charlie"},{"n":"bravo"},{"n":"echo"}]})";

	Fixture fixture;
	fixture.load ( original );

	QCOMPARE ( fixture.sort ( QStringLiteral ( "n" ), Qt::AscendingOrder ), EditOutcome::Applied );
	QVERIFY  ( fixture.text () != original );

	fixture.controller.undo ();

	QCOMPARE ( fixture.text (), original );
}

void TestSortArrayCommand::raw_number_tokens_survive_the_sort_untouched ()
{
	// FILE-10. The sort orders by VALUE -- 1.50 below 1e3 -- while the tokens are carried through character for
	// character, so a save writes exactly what was loaded.

	Fixture fixture;
	fixture.load ( R"({"items":[1e3,1.50,-0.0,42]})" );

	QCOMPARE ( fixture.sort ( BY_ELEMENT, Qt::AscendingOrder ), EditOutcome::Applied );
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[-0.0,1.50,42,1e3]})" ) );
}

void TestSortArrayCommand::a_ragged_array_sorts_rather_than_being_refused ()
{
	Fixture fixture;
	fixture.load ( R"({"items":[{"k":2,"o":"x"},{"o":"y"},{"k":1,"o":"z"}]})" );

	QCOMPARE ( fixture.sort ( QStringLiteral ( "k" ), Qt::AscendingOrder ), EditOutcome::Applied );

	// The element lacking "k" leads (it sorts as null), and nothing about the elements themselves is rewritten --
	// the sort orders, it does not normalize (EDIT-11 is the command that fills a missing member in).

	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":[{"o":"y"},{"k":1,"o":"z"},{"k":2,"o":"x"}]})" ) );
}

void TestSortArrayCommand::an_array_already_in_that_order_is_unchanged ()
{
	// The reachable no-op: clicking the same header twice in the same direction. It must not push a command that
	// undoes nothing and must not dirty the document (lesson D19).

	Fixture fixture;
	fixture.load ( R"({"items":[1,2,3]})" );

	QCOMPARE ( fixture.sort ( BY_ELEMENT, Qt::AscendingOrder ), EditOutcome::Unchanged );
	QCOMPARE ( fixture.controller.stack ()->count (), 0 );
	QVERIFY  ( fixture.controller.is_clean () );

	// A column whose values are all equal is the same no-op reached the other way.

	fixture.load ( R"({"items":[{"k":1},{"k":1},{"k":1}]})" );

	QCOMPARE ( fixture.sort ( QStringLiteral ( "k" ), Qt::DescendingOrder ), EditOutcome::Unchanged );
	QCOMPARE ( fixture.controller.stack ()->count (), 0 );
}

void TestSortArrayCommand::an_array_of_fewer_than_two_elements_is_unchanged ()
{
	Fixture fixture;

	fixture.load ( R"({"items":[]})" );
	QCOMPARE ( fixture.sort ( BY_ELEMENT, Qt::AscendingOrder ), EditOutcome::Unchanged );

	fixture.load ( R"({"items":[7]})" );
	QCOMPARE ( fixture.sort ( BY_ELEMENT, Qt::AscendingOrder ), EditOutcome::Unchanged );

	QCOMPARE ( fixture.controller.stack ()->count (), 0 );
}

void TestSortArrayCommand::a_pointer_that_is_not_an_array_is_rejected ()
{
	Fixture fixture;
	fixture.load ( R"({"items":{"a":1},"other":5})" );

	QCOMPARE ( fixture.sort ( BY_ELEMENT, Qt::AscendingOrder ), EditOutcome::Rejected );

	QCOMPARE
	(
		fixture.controller.sort_array ( JsonPointer::parse ( QStringLiteral ( "/nowhere" ) ), BY_ELEMENT, Qt::AscendingOrder ),
		EditOutcome::Rejected
	);

	QCOMPARE ( fixture.controller.stack ()->count (), 0 );
}

void TestSortArrayCommand::redo_puts_the_sorted_order_back ()
{
	Fixture fixture;
	fixture.load ( R"({"items":["c","a","b"]})" );

	QCOMPARE ( fixture.sort ( BY_ELEMENT, Qt::AscendingOrder ), EditOutcome::Applied );

	fixture.controller.undo ();
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":["c","a","b"]})" ) );

	fixture.controller.redo ();
	QCOMPARE ( fixture.text (), QStringLiteral ( R"({"items":["a","b","c"]})" ) );
}

QTEST_GUILESS_MAIN ( TestSortArrayCommand )

#include "tst_sort_array_command.moc"
