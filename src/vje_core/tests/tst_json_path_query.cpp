//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for JsonPathQuery (QUERY-01..07). This suite carries Phase 15d: the whole decidable half of the feature is
//   the grammar and its evaluation, and neither needs a display.
//
//   THE WORKED DOCUMENT IS THE SPEC'S. Every accepted construct is exercised against `test.json` from spec section 2.7,
//   so the examples in the grammar table and the assertions here cannot drift apart -- a construct whose example stops
//   producing what the table says fails a test rather than only reading oddly.
//
//   EVERY REJECTION IS ASSERTED WITH ITS POSITION, not merely as "it failed". QUERY-05 makes the position part of the
//   requirement (it is what the query box selects), so a parser that reports the right message at the wrong place is
//   a defect this suite has to see.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonPathQuery.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QtTest/QtTest>

#include <QElapsedTimer>

using namespace vje;

namespace
{
	std::unique_ptr<JsonNode> parse ( const QString& json )
	{
		ParseResult result = JsonParser::parse ( json );

		return std::move ( result.root );
	}

	// `test.json` from spec section 2.7, verbatim in content -- the document every worked example in the grammar table
	// is written against.

	std::unique_ptr<JsonNode> sample ()
	{
		return parse ( QStringLiteral (
			"{"
			"\"id\":1001,"
			"\"name\":\"Alex Rivera\","
			"\"active\":true,"
			"\"lastLogin\":null,"
			"\"roles\":[\"admin\",\"editor\"],"
			"\"profile\":{\"email\":\"alex.rivera@example.com\",\"age\":34,\"department\":\"Engineering\"},"
			"\"projects\":["
			"{\"name\":\"JSON Editor\",\"status\":\"in-progress\",\"priority\":1,\"tags\":[\"ui\",\"desktop\",\"json\"]},"
			"{\"name\":\"Data Migration\",\"status\":\"completed\",\"priority\":2,\"tags\":null}"
			"],"
			"\"settings\":{\"theme\":\"dark\",\"notifications\":{\"email\":true,\"push\":false}}"
			"}" ) );
	}

	QStringList run ( const JsonNode& root, const QString& query )
	{
		const JsonPathQuery compiled = JsonPathQuery::compile ( query );

		QStringList out;

		for ( const JsonPointer& pointer : compiled.evaluate ( root ) )
		{
			out << pointer.to_string ();
		}

		return out;
	}

	JsonPathError compile_error ( const QString& query )
	{
		const JsonPathQuery compiled = JsonPathQuery::compile ( query );

		return compiled.error ();
	}
}

class TestJsonPathQuery : public QObject
{
	Q_OBJECT

private slots:

	// -- The accepted grammar (QUERY-02) ----------------------------------------------------------------------------

	void the_root_selects_the_document_itself ();
	void child_by_name_walks_the_object ();
	void child_by_bracketed_name_reaches_keys_a_bare_name_cannot ();
	void an_index_counts_from_the_front_and_a_negative_one_from_the_end ();
	void a_slice_takes_its_bounds_its_defaults_and_its_step ();
	void a_slice_clamps_rather_than_refusing ();
	void a_slice_bound_is_clamped_rather_than_merely_skipped ();
	void a_wildcard_takes_every_element_and_every_member_value ();
	void recursive_descent_includes_the_node_it_starts_from ();
	void a_union_mixes_names_and_indices ();
	void whitespace_between_tokens_is_insignificant ();

	// -- Filters (QUERY-03) -----------------------------------------------------------------------------------------

	void a_filter_selects_among_the_children_of_the_node_it_is_applied_to ();
	void a_filter_compares_strings_and_tests_existence ();
	void a_filter_combines_expressions_with_and_or_and_not ();
	void a_number_compares_by_value_and_not_by_its_raw_token ();
	void an_operand_that_names_nothing_makes_every_comparison_false ();
	void operands_of_different_kinds_are_unequal_and_unordered ();
	void a_container_is_present_but_compares_equal_to_nothing ();

	// -- Results (QUERY-06) -----------------------------------------------------------------------------------------

	void results_come_back_in_document_order_whatever_order_the_selectors_ran_in ();
	void a_node_named_twice_is_one_result ();
	void a_name_selector_takes_the_first_of_a_duplicated_key ();

	// -- Rejections, each with its position (QUERY-04 / QUERY-05) ---------------------------------------------------

	void a_query_must_begin_with_the_root ();
	void an_unsupported_construct_is_reported_with_its_position ();
	void a_malformed_query_is_reported_with_its_position ();
	void a_malformed_escape_is_reported_at_the_backslash ();

	// -- The empty and invalid states -------------------------------------------------------------------------------

	void an_empty_query_is_invalid_and_reports_nothing ();
	void an_invalid_query_evaluates_to_nothing ();
	void run_compiles_and_evaluates_in_one_call ();
};

//=======================================================================================================================
// The accepted grammar (QUERY-02)
//=======================================================================================================================

void TestJsonPathQuery::the_root_selects_the_document_itself ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	// The root pointer is the empty string (RFC 6901), which is what makes "$" round-trip through Go To.

	QCOMPARE ( run ( *root, QStringLiteral ( "$" ) ), QStringList { QString () } );
}

void TestJsonPathQuery::child_by_name_walks_the_object ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( run ( *root, QStringLiteral ( "$.name" ) ),          QStringList { QStringLiteral ( "/name" ) } );
	QCOMPARE ( run ( *root, QStringLiteral ( "$.profile.email" ) ), QStringList { QStringLiteral ( "/profile/email" ) } );

	// A name that is not there is not an error; it simply selects nothing.

	QVERIFY ( run ( *root, QStringLiteral ( "$.missing" ) ).isEmpty () );

	// A hyphen is part of a bare name, so a hyphenated key needs no brackets.

	const std::unique_ptr<JsonNode> hyphenated = parse ( QStringLiteral ( "{\"first-name\":\"Alex\"}" ) );

	QCOMPARE ( run ( *hyphenated, QStringLiteral ( "$.first-name" ) ), QStringList { QStringLiteral ( "/first-name" ) } );
}

void TestJsonPathQuery::child_by_bracketed_name_reaches_keys_a_bare_name_cannot ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( run ( *root, QStringLiteral ( "$['name']" ) ), QStringList { QStringLiteral ( "/name" ) } );
	QCOMPARE ( run ( *root, QStringLiteral ( "$[\"name\"]" ) ), QStringList { QStringLiteral ( "/name" ) } );

	// The point of the bracketed form: a key with a space in it, which a bare name cannot spell at all.

	const std::unique_ptr<JsonNode> spaced = parse ( QStringLiteral ( "{\"last login\":\"today\"}" ) );

	QCOMPARE ( run ( *spaced, QStringLiteral ( "$['last login']" ) ), QStringList { QStringLiteral ( "/last login" ) } );
}

void TestJsonPathQuery::an_index_counts_from_the_front_and_a_negative_one_from_the_end ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( run ( *root, QStringLiteral ( "$.roles[0]" ) ),  QStringList { QStringLiteral ( "/roles/0" ) } );
	QCOMPARE ( run ( *root, QStringLiteral ( "$.roles[1]" ) ),  QStringList { QStringLiteral ( "/roles/1" ) } );
	QCOMPARE ( run ( *root, QStringLiteral ( "$.roles[-1]" ) ), QStringList { QStringLiteral ( "/roles/1" ) } );
	QCOMPARE ( run ( *root, QStringLiteral ( "$.roles[-2]" ) ), QStringList { QStringLiteral ( "/roles/0" ) } );

	// Out of range at either end selects nothing rather than clamping -- an index names one element or none.

	QVERIFY ( run ( *root, QStringLiteral ( "$.roles[2]" ) ).isEmpty () );
	QVERIFY ( run ( *root, QStringLiteral ( "$.roles[-3]" ) ).isEmpty () );

	// An index against an object selects nothing; it is not an error, and it is not a member position either.

	QVERIFY ( run ( *root, QStringLiteral ( "$.profile[0]" ) ).isEmpty () );
}

void TestJsonPathQuery::a_slice_takes_its_bounds_its_defaults_and_its_step ()
{
	const std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"a\":[0,1,2,3,4]}" ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[1:3]" ) ),
	           ( QStringList { QStringLiteral ( "/a/1" ), QStringLiteral ( "/a/2" ) } ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[3:]" ) ),
	           ( QStringList { QStringLiteral ( "/a/3" ), QStringLiteral ( "/a/4" ) } ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[:2]" ) ),
	           ( QStringList { QStringLiteral ( "/a/0" ), QStringLiteral ( "/a/1" ) } ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[::2]" ) ),
	           ( QStringList { QStringLiteral ( "/a/0" ), QStringLiteral ( "/a/2" ), QStringLiteral ( "/a/4" ) } ) );

	// Negative bounds count from the end, exactly as an index does.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[-2:]" ) ),
	           ( QStringList { QStringLiteral ( "/a/3" ), QStringLiteral ( "/a/4" ) } ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[:-3]" ) ),
	           ( QStringList { QStringLiteral ( "/a/0" ), QStringLiteral ( "/a/1" ) } ) );
}

void TestJsonPathQuery::a_slice_clamps_rather_than_refusing ()
{
	const std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"a\":[0,1]}" ) );

	// A bound past either end is clamped -- a slice describes a RANGE, and asking for more of it than exists is not
	// the same mistake as naming an element that is not there.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[0:10]" ) ),
	           ( QStringList { QStringLiteral ( "/a/0" ), QStringLiteral ( "/a/1" ) } ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[-10:]" ) ),
	           ( QStringList { QStringLiteral ( "/a/0" ), QStringLiteral ( "/a/1" ) } ) );

	// An inverted range is empty rather than reversed.

	QVERIFY ( run ( *root, QStringLiteral ( "$.a[1:0]" ) ).isEmpty () );
}

void TestJsonPathQuery::a_slice_bound_is_clamped_rather_than_merely_skipped ()
{
	// A finding from the neutering pass, and it is about what the clamp is FOR. Removing it changes no result --
	// an out-of-range element is nullptr and skipped either way -- so every assertion above passes without it. What it
	// bounds is the WORK: unclamped, "[-2000000000:]" walks two billion indices to produce two elements.
	//
	// This is therefore the one case that can see the clamp at all, and it sees it as time. The margin is not marginal:
	// the correct build does two iterations and the unclamped one does 2e9, so any threshold between microseconds and
	// minutes separates them.

	const std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"a\":[0,1]}" ) );

	QElapsedTimer timer;

	timer.start ();

	const QStringList results = run ( *root, QStringLiteral ( "$.a[-2000000000:2000000000]" ) );

	const qint64 elapsed = timer.elapsed ();

	QCOMPARE ( results, ( QStringList { QStringLiteral ( "/a/0" ), QStringLiteral ( "/a/1" ) } ) );

	QVERIFY2 ( elapsed < 2000, qPrintable ( QStringLiteral ( "took %1 ms" ).arg ( elapsed ) ) );
}

void TestJsonPathQuery::a_wildcard_takes_every_element_and_every_member_value ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( run ( *root, QStringLiteral ( "$.roles[*]" ) ),
	           ( QStringList { QStringLiteral ( "/roles/0" ), QStringLiteral ( "/roles/1" ) } ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.profile.*" ) ),
	           ( QStringList { QStringLiteral ( "/profile/email" ),
	                           QStringLiteral ( "/profile/age" ),
	                           QStringLiteral ( "/profile/department" ) } ) );

	// A scalar has no children, so a wildlcard on one selects nothing.

	QVERIFY ( run ( *root, QStringLiteral ( "$.name.*" ) ).isEmpty () );
}

void TestJsonPathQuery::recursive_descent_includes_the_node_it_starts_from ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	// The top-level "name" is found as well as the two nested ones, which is what "the node itself, then everything
	// under it" buys and what a descent that only looked downwards would miss.

	QCOMPARE ( run ( *root, QStringLiteral ( "$..name" ) ),
	           ( QStringList { QStringLiteral ( "/name" ),
	                           QStringLiteral ( "/projects/0/name" ),
	                           QStringLiteral ( "/projects/1/name" ) } ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$..email" ) ),
	           ( QStringList { QStringLiteral ( "/profile/email" ),
	                           QStringLiteral ( "/settings/notifications/email" ) } ) );

	// A descent into a bracketed selector is the same rule with the other spelling.

	QCOMPARE ( run ( *root, QStringLiteral ( "$..['status']" ) ),
	           ( QStringList { QStringLiteral ( "/projects/0/status" ),
	                           QStringLiteral ( "/projects/1/status" ) } ) );

	// "$..*" is every node except the root, since the root is the only node that is nobody's child.

	const QStringList everything = run ( *root, QStringLiteral ( "$..*" ) );

	QVERIFY ( !everything.contains ( QString () ) );
	QVERIFY ( everything.contains ( QStringLiteral ( "/settings/notifications/push" ) ) );
}

void TestJsonPathQuery::a_union_mixes_names_and_indices ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( run ( *root, QStringLiteral ( "$['name','active']" ) ),
	           ( QStringList { QStringLiteral ( "/name" ), QStringLiteral ( "/active" ) } ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.roles[0,1]" ) ),
	           ( QStringList { QStringLiteral ( "/roles/0" ), QStringLiteral ( "/roles/1" ) } ) );

	// Mixed kinds in one union: the index answers for an array and the name for an object, and neither refuses the
	// other's container.

	const std::unique_ptr<JsonNode> mixed = parse ( QStringLiteral ( "{\"a\":[9,8]}" ) );

	QCOMPARE ( run ( *mixed, QStringLiteral ( "$.a[0,'a']" ) ), QStringList { QStringLiteral ( "/a/0" ) } );
}

void TestJsonPathQuery::whitespace_between_tokens_is_insignificant ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	// The property the query box's multi-line text depends on (QUERY-01): a query broken across lines is one query.

	QCOMPARE ( run ( *root, QStringLiteral ( "$ . projects [ 0 ] . name" ) ),
	           QStringList { QStringLiteral ( "/projects/0/name" ) } );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects\n  [ 0 ]\n  .name" ) ),
	           QStringList { QStringLiteral ( "/projects/0/name" ) } );

	QCOMPARE ( run ( *root, QStringLiteral ( "  $.name  " ) ), QStringList { QStringLiteral ( "/name" ) } );
}

//=======================================================================================================================
// Filters (QUERY-03)
//=======================================================================================================================

void TestJsonPathQuery::a_filter_selects_among_the_children_of_the_node_it_is_applied_to ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.priority > 1)]" ) ),
	           QStringList { QStringLiteral ( "/projects/1" ) } );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.priority < 2)].name" ) ),
	           QStringList { QStringLiteral ( "/projects/0/name" ) } );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.priority >= 1)]" ) ),
	           ( QStringList { QStringLiteral ( "/projects/0" ), QStringLiteral ( "/projects/1" ) } ) );

	// Applied to an OBJECT the same rule reads its member values, which is what makes "@" alone useful.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.settings.notifications[?(@ == true)]" ) ),
	           QStringList { QStringLiteral ( "/settings/notifications/email" ) } );
}

void TestJsonPathQuery::a_filter_compares_strings_and_tests_existence ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.status == 'completed')]" ) ),
	           QStringList { QStringLiteral ( "/projects/1" ) } );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.status != 'completed')]" ) ),
	           QStringList { QStringLiteral ( "/projects/0" ) } );

	// Strings order lexicographically, so an inequality on them is meaningful rather than refused.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.status < 'd')]" ) ),
	           QStringList { QStringLiteral ( "/projects/1" ) } );

	// Existence: element 0's tags is an array (present), element 1's is null (which is not).

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.tags)]" ) ),
	           QStringList { QStringLiteral ( "/projects/0" ) } );
}

void TestJsonPathQuery::a_filter_combines_expressions_with_and_or_and_not ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.priority == 1 && @.tags)]" ) ),
	           QStringList { QStringLiteral ( "/projects/0" ) } );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.priority == 9 || @.status == 'completed')]" ) ),
	           QStringList { QStringLiteral ( "/projects/1" ) } );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(!@.tags)]" ) ),
	           QStringList { QStringLiteral ( "/projects/1" ) } );

	// Parentheses group, and they have to: without them "&&" binds tighter and the first element would answer too.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(!(@.priority == 1 || @.priority == 9))]" ) ),
	           QStringList { QStringLiteral ( "/projects/1" ) } );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.priority == 1 || @.priority == 2 && @.tags)]" ) ),
	           QStringList { QStringLiteral ( "/projects/0" ) } );
}

void TestJsonPathQuery::a_number_compares_by_value_and_not_by_its_raw_token ()
{
	// FILE-10 keeps "1.0" as the characters 1 . 0 -- which is what a save writes and what a token comparison would
	// see. An inequality asks about the VALUE, so all three of these are the number one (QUERY-03).

	const std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"a\":[{\"v\":1},{\"v\":1.0},{\"v\":1e0},{\"v\":2}]}" ) );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[?(@.v == 1)]" ) ),
	           ( QStringList { QStringLiteral ( "/a/0" ), QStringLiteral ( "/a/1" ), QStringLiteral ( "/a/2" ) } ) );

	// And the raw token survives the query untouched -- an engine that normalized numbers to compare them would be
	// visible here.

	QCOMPARE ( root->find_member ( QStringLiteral ( "a" ) )->array_element ( 1 )
	               ->find_member ( QStringLiteral ( "v" ) )->number_token (), QStringLiteral ( "1.0" ) );
}

void TestJsonPathQuery::an_operand_that_names_nothing_makes_every_comparison_false ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	// The reading that matters: "@.missing != 1" is FALSE, not true. The other reading makes a filter select the
	// elements it knows least about, which is never what was meant.

	QVERIFY ( run ( *root, QStringLiteral ( "$.projects[?(@.missing == 1)]" ) ).isEmpty () );
	QVERIFY ( run ( *root, QStringLiteral ( "$.projects[?(@.missing != 1)]" ) ).isEmpty () );
	QVERIFY ( run ( *root, QStringLiteral ( "$.projects[?(@.missing > 1)]" ) ).isEmpty () );
	QVERIFY ( run ( *root, QStringLiteral ( "$.projects[?(@.missing)]" ) ).isEmpty () );

	// Negating an existence test on an absent member IS true -- "it is not there" is a thing that can be asked.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(!@.missing)]" ) ),
	           ( QStringList { QStringLiteral ( "/projects/0" ), QStringLiteral ( "/projects/1" ) } ) );
}

void TestJsonPathQuery::operands_of_different_kinds_are_unequal_and_unordered ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	// A number is never equal to the string that spells it...

	QVERIFY ( run ( *root, QStringLiteral ( "$.projects[?(@.priority == '1')]" ) ).isEmpty () );

	// ...and is unequal to it, which is the one operator that answers true across kinds.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.priority != '1')]" ) ),
	           ( QStringList { QStringLiteral ( "/projects/0" ), QStringLiteral ( "/projects/1" ) } ) );

	// Ordering across kinds is false rather than an invented total order.

	QVERIFY ( run ( *root, QStringLiteral ( "$.projects[?(@.priority < 'a')]" ) ).isEmpty () );

	// null equals null and orders against nothing, itself included.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.projects[?(@.tags == null)]" ) ),
	           QStringList { QStringLiteral ( "/projects/1" ) } );

	QVERIFY ( run ( *root, QStringLiteral ( "$.projects[?(@.tags < null)]" ) ).isEmpty () );
}

void TestJsonPathQuery::a_container_is_present_but_compares_equal_to_nothing ()
{
	const std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"a\":[{\"v\":[]},{\"v\":{}},{\"v\":1}]}" ) );

	// Present, so an existence test finds both containers...

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[?(@.v)]" ) ),
	           ( QStringList { QStringLiteral ( "/a/0" ), QStringLiteral ( "/a/1" ), QStringLiteral ( "/a/2" ) } ) );

	// ...and equal to nothing, so an equality against one is false whatever is on the other side.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a[?(@.v == 1)]" ) ), QStringList { QStringLiteral ( "/a/2" ) } );

	// Including another container, which is the case that separates "a container is its own kind of thing" from
	// "containers happen to be the same KIND as each other". Two empty arrays are unequal here, because equality on
	// subtrees is a question this engine deliberately does not answer.

	const std::unique_ptr<JsonNode> pairs = parse ( QStringLiteral (
		"{\"a\":[{\"v\":[],\"w\":[]},{\"v\":1,\"w\":1}]}" ) );

	QCOMPARE ( run ( *pairs, QStringLiteral ( "$.a[?(@.v != @.w)]" ) ), QStringList { QStringLiteral ( "/a/0" ) } );
	QCOMPARE ( run ( *pairs, QStringLiteral ( "$.a[?(@.v == @.w)]" ) ), QStringList { QStringLiteral ( "/a/1" ) } );
}

//=======================================================================================================================
// Results (QUERY-06)
//=======================================================================================================================

void TestJsonPathQuery::results_come_back_in_document_order_whatever_order_the_selectors_ran_in ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	// "id" is the document's first member and "settings" its last. Written either way round the RESULT is the same,
	// because the order comes from one pre-order walk of the document rather than from the order the union visited
	// its selectors in.

	const QStringList expected { QStringLiteral ( "/id" ), QStringLiteral ( "/settings" ) };

	QCOMPARE ( run ( *root, QStringLiteral ( "$['id','settings']" ) ), expected );
	QCOMPARE ( run ( *root, QStringLiteral ( "$['settings','id']" ) ), expected );
}

void TestJsonPathQuery::a_node_named_twice_is_one_result ()
{
	// A BEHAVIOUR PIN rather than a guard, and worth saying so: evaluation ends in a set of nodes, so a duplicate is
	// impossible by construction and no neuter short of rewriting evaluate() can make this case fail. What it defends
	// is the shape of the answer surviving a later change to how results are produced.

	const std::unique_ptr<JsonNode> root = sample ();

	// A union naming the same member twice...

	QCOMPARE ( run ( *root, QStringLiteral ( "$['id','id']" ) ), QStringList { QStringLiteral ( "/id" ) } );

	// ...and a descent reaching the same node by two routes. "$..profile.email" reaches /profile/email from the root
	// and from /profile alike; two identical rows would be two things to click that do the same thing.

	QCOMPARE ( run ( *root, QStringLiteral ( "$..profile.email" ) ), QStringList { QStringLiteral ( "/profile/email" ) } );
}

void TestJsonPathQuery::a_name_selector_takes_the_first_of_a_duplicated_key ()
{
	// RFC 8259 permits duplicate keys and VJE preserves them on load. A JSON Pointer names the FIRST, everywhere in
	// this application -- so a name selector does too, since returning both would produce two results whose pointer
	// text is identical and which therefore both resolve to the same node.

	const std::unique_ptr<JsonNode> root = parse ( QStringLiteral ( "{\"a\":1,\"a\":2}" ) );

	QCOMPARE ( root->member_count (), 2 );

	QCOMPARE ( run ( *root, QStringLiteral ( "$.a" ) ),  QStringList { QStringLiteral ( "/a" ) } );
	QCOMPARE ( run ( *root, QStringLiteral ( "$..a" ) ), QStringList { QStringLiteral ( "/a" ) } );

	// The wildcard, which names nothing, still reaches both -- and both are told apart by their pointers, because a
	// pointer built by POSITION is not the same thing as one resolved by name.

	QCOMPARE ( run ( *root, QStringLiteral ( "$.*" ) ).size (), 2 );
}

//=======================================================================================================================
// Rejections, each with its position (QUERY-04 / QUERY-05)
//=======================================================================================================================

void TestJsonPathQuery::a_query_must_begin_with_the_root ()
{
	const JsonPathError error = compile_error ( QStringLiteral ( "projects.name" ) );

	QVERIFY ( !error.message.isEmpty () );
	QCOMPARE ( error.position, 0 );
	QCOMPARE ( error.length,   1 );

	QVERIFY ( !JsonPathQuery::compile ( QStringLiteral ( "projects.name" ) ).is_valid () );
}

void TestJsonPathQuery::an_unsupported_construct_is_reported_with_its_position ()
{
	// QUERY-04's list, one row each: the construct, the offset the message is anchored at, and a fragment of the
	// wording. The offsets are what the query box selects, so they are part of the requirement rather than an
	// implementation detail (QUERY-05).

	struct Rejection
	{
		QString query;
		int     position;
		QString fragment;
	};

	const QList<Rejection> rejections
	{
		{ QStringLiteral ( "$.a[(@.length-1)]" ),    4,  QStringLiteral ( "Script expressions"    ) },
		{ QStringLiteral ( "$.a[?(length(@) > 1)]" ), 6, QStringLiteral ( "Functions"              ) },
		{ QStringLiteral ( "$.a[?(@.b =~ 'x')]" ),   10, QStringLiteral ( "Regular-expression"     ) },
		{ QStringLiteral ( "$^.a" ),                  1, QStringLiteral ( "parent operator"        ) },
		{ QStringLiteral ( "$.a[?($.b == 1)]" ),      6, QStringLiteral ( "starts at '@'"          ) },
		{ QStringLiteral ( "$.a[?(@[?(@.c)])]" ),     8, QStringLiteral ( "filter is not supported" ) },
		{ QStringLiteral ( "$.a[?(@..b)]" ),          7, QStringLiteral ( "Recursive descent"      ) },
		{ QStringLiteral ( "$.a[?(@[*])]" ),          8, QStringLiteral ( "wildcard"               ) },
		{ QStringLiteral ( "$.a[?(@.b + 1 > 2)]" ),  10, QStringLiteral ( "Arithmetic"             ) },
		{ QStringLiteral ( "$.a[?(@[-1] == 1)]" ),    8, QStringLiteral ( "negative index"         ) },
		{ QStringLiteral ( "$.a[?(@[0:1] == 1)]" ),   9, QStringLiteral ( "slice is not supported"  ) },
		{ QStringLiteral ( "$.a[::0]" ),              6, QStringLiteral ( "positive integer"       ) },
		{ QStringLiteral ( "$.a[::-1]" ),             6, QStringLiteral ( "positive integer"       ) },
		{ QStringLiteral ( "$[*,0]" ),                1, QStringLiteral ( "union"                  ) }
	};

	for ( const Rejection& rejection : rejections )
	{
		const JsonPathError error = compile_error ( rejection.query );

		QVERIFY2 ( !error.message.isEmpty (), qPrintable ( rejection.query ) );

		QVERIFY2 ( error.message.contains ( rejection.fragment ),
		           qPrintable ( rejection.query + QStringLiteral ( " -> " ) + error.message ) );

		QVERIFY2 ( error.position == rejection.position,
		           qPrintable ( rejection.query + QStringLiteral ( " -> position " ) + QString::number ( error.position ) ) );

		// Something is always marked, except at end-of-input -- an error the box cannot show is an error the user has
		// to find for themselves.

		QVERIFY2 ( error.length > 0, qPrintable ( rejection.query ) );
	}
}

void TestJsonPathQuery::a_malformed_query_is_reported_with_its_position ()
{
	struct Rejection
	{
		QString query;
		int     position;
		int     length;
	};

	const QList<Rejection> rejections
	{
		{ QStringLiteral ( "$.a[" ),        3, 1 },   // Unterminated '['.
		{ QStringLiteral ( "$.a['x" ),      4, 2 },   // Unterminated string.
		{ QStringLiteral ( "$.." ),         3, 0 },   // Dangling '..' -- end of input, so nothing to mark.
		{ QStringLiteral ( "$.[0]" ),       1, 2 },   // '.[' is not a spelling of anything.
		{ QStringLiteral ( "$.a[?(@.b ==)]" ), 12, 1 },   // A comparison with nothing on its right.
		{ QStringLiteral ( "$.a[?(@.b = 1)]" ), 10, 1 },   // A single '=' is a near-miss worth naming.
		{ QStringLiteral ( "$!a" ),         1, 1 }    // Not a step at all.
	};

	for ( const Rejection& rejection : rejections )
	{
		const JsonPathError error = compile_error ( rejection.query );

		QVERIFY2 ( !error.message.isEmpty (), qPrintable ( rejection.query ) );

		QVERIFY2 ( error.position == rejection.position,
		           qPrintable ( rejection.query + QStringLiteral ( " -> position " ) + QString::number ( error.position ) ) );

		QVERIFY2 ( error.length == rejection.length,
		           qPrintable ( rejection.query + QStringLiteral ( " -> length " ) + QString::number ( error.length ) ) );
	}
}

void TestJsonPathQuery::a_malformed_escape_is_reported_at_the_backslash ()
{
	// The string literal's escapes go through json_escapes -- the codebase's one escape table -- and the offset comes
	// back in the BODY's coordinates, so the parser has to translate it. Reporting at the literal's opening quote
	// instead would pass a weaker test and be useless in a long key.

	const JsonPathError error = compile_error ( QStringLiteral ( "$['a\\q']" ) );

	QVERIFY ( error.message.contains ( QStringLiteral ( "escape" ) ) );
	QCOMPARE ( error.position, 4 );

	// A well-formed escape is not an error, and it decodes: the key here is a tab.

	const std::unique_ptr<JsonNode> tabbed = parse ( QStringLiteral ( "{\"a\\tb\":1}" ) );

	QCOMPARE ( run ( *tabbed, QStringLiteral ( "$['a\\tb']" ) ), QStringList { QStringLiteral ( "/a\tb" ) } );
}

//=======================================================================================================================
// The empty and invalid states
//=======================================================================================================================

void TestJsonPathQuery::an_empty_query_is_invalid_and_reports_nothing ()
{
	// An empty query is not a mistake, so there is nothing to report (QUERY-05). It is still invalid, which is what
	// stops it evaluating to "everything".

	for ( const QString& text : { QString (), QStringLiteral ( "   " ), QStringLiteral ( "\n" ) } )
	{
		const JsonPathQuery query = JsonPathQuery::compile ( text );

		QVERIFY ( !query.is_valid () );
		QVERIFY ( query.error ().message.isEmpty () );
	}
}

void TestJsonPathQuery::an_invalid_query_evaluates_to_nothing ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	// Evaluating an invalid query answers nothing rather than refusing, so a caller that has already reported the
	// error does not have to guard the call.

	const JsonPathQuery query = JsonPathQuery::compile ( QStringLiteral ( "$.a[" ) );

	QVERIFY ( !query.is_valid () );
	QVERIFY ( query.evaluate ( *root ).empty () );

	QCOMPARE ( query.text (), QStringLiteral ( "$.a[" ) );
}

void TestJsonPathQuery::run_compiles_and_evaluates_in_one_call ()
{
	const std::unique_ptr<JsonNode> root = sample ();

	JsonPathError error;

	const std::vector<JsonPointer> results = JsonPathQuery::run ( *root, QStringLiteral ( "$.roles[*]" ), &error );

	QVERIFY ( error.message.isEmpty () );
	QCOMPARE ( static_cast<int> ( results.size () ), 2 );

	// A failure fills the error and returns nothing; a success CLEARS it, so a caller reusing one error value does
	// not report the previous query's mistake.

	const std::vector<JsonPointer> failed = JsonPathQuery::run ( *root, QStringLiteral ( "oops" ), &error );

	QVERIFY ( failed.empty () );
	QVERIFY ( !error.message.isEmpty () );

	JsonPathQuery::run ( *root, QStringLiteral ( "$" ), &error );

	QVERIFY ( error.message.isEmpty () );
}

QTEST_APPLESS_MAIN ( TestJsonPathQuery )

#include "tst_json_path_query.moc"
