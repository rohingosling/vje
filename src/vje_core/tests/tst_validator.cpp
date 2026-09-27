//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for Validator (VAL-01..03): RFC 8259 validation with a reported position; the duplicate-key CENSUS and
//   the introduced_duplicate comparison the Code View commit is built on (VAL-02 / SET-03a); the introduces_duplicate
//   edit guard with an ignore-self slot for rename; and JSON-number validation for Form input.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/Validator.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

using namespace vje;

class TestValidator : public QObject
{
	Q_OBJECT

private slots:

	void valid_text_passes ();
	void malformed_text_reports_position ();
	void duplicates_are_accepted_and_listed ();
	void the_census_counts_each_objects_duplicates ();
	void re_shaping_existing_duplicates_introduces_nothing ();
	void a_further_occurrence_of_an_existing_duplicate_is_introduced ();
	void a_second_objects_duplicate_is_introduced ();
	void removing_a_duplicate_is_never_an_introduction ();
	void introduces_duplicate_guard ();
	void introduces_duplicate_ignores_self ();
	void number_validation_data ();
	void number_validation ();
};

namespace
{
	// The census cases work on TREES rather than on text, because that is what the Code View commit compares: the
	// document it already has against the one the editor would install.

	std::unique_ptr<JsonNode> parse ( const char* text )
	{
		ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

		return std::move ( result.root );
	}
}

void TestValidator::valid_text_passes ()
{
	ValidationResult result = Validator::validate ( QStringLiteral ( "{\"a\":1}" ) );

	QVERIFY ( result.ok );
	QVERIFY ( result.root != nullptr );
}

void TestValidator::malformed_text_reports_position ()
{
	ValidationResult result = Validator::validate ( QStringLiteral ( "[1,,2]" ) );

	QVERIFY ( !result.ok );
	QVERIFY ( result.root == nullptr );
	QVERIFY ( !result.issue.message.isEmpty () );
	QCOMPARE ( result.issue.line, 1 );
	QVERIFY ( result.issue.column > 0 );
}

void TestValidator::duplicates_are_accepted_and_listed ()
{
	// validate() ACCEPTS duplicates and reports them, holding no policy of its own -- RFC 8259 permits them and
	// FILE-04 preserves them, so what to DO about them belongs to the caller.
	//
	// It used to take a rejectDuplicates flag, and the Code View commit passed true: fail on ANY duplicate in the
	// text. That made every commit impossible on a file that ARRIVED with duplicates, naming an object the user had
	// never touched (reported 2026-08-20). The flag is gone, so this case is now the whole of validate()'s behaviour
	// on a duplicate rather than one half of it.

	ValidationResult result = Validator::validate ( QStringLiteral ( "{\"a\":1,\"a\":2}" ) );

	QVERIFY  ( result.ok );
	QVERIFY  ( result.root != nullptr );
	QCOMPARE ( static_cast<int> ( result.duplicateKeys.size () ), 1 );

	// The position travels with it, which is what lets a refusal point at an occurrence rather than at the document.

	QVERIFY ( result.duplicateKeys.front ().line > 0 );
}

void TestValidator::the_census_counts_each_objects_duplicates ()
{
	// One entry per ( object, duplicated key ), carrying HOW MANY -- and nothing that is not duplicated. The count is
	// what makes "a fourth colour" detectable below, since a group of 4 matches no group of 3.

	std::unique_ptr<JsonNode> root = parse ( R"({"a":1,"a":2,"a":3,"b":1,"nested":{"c":1,"c":2}})" );

	const std::vector<DuplicateKeyGroup> census = Validator::duplicate_census ( *root );

	QCOMPARE ( static_cast<int> ( census.size () ), 2 );

	QCOMPARE ( census [ 0 ].key,   QStringLiteral ( "a" ) );
	QCOMPARE ( census [ 0 ].count, 3 );
	QCOMPARE ( census [ 1 ].key,   QStringLiteral ( "c" ) );
	QCOMPARE ( census [ 1 ].count, 2 );

	// A document with no duplicates has an EMPTY census, which is what makes containment trivially true for every
	// edit to an ordinary file.

	std::unique_ptr<JsonNode> clean = parse ( R"({"a":1,"b":[1,2,3]})" );

	QVERIFY ( Validator::duplicate_census ( *clean ).empty () );
}

void TestValidator::re_shaping_existing_duplicates_introduces_nothing ()
{
	// THE CASE THE DEFECT WAS REPORTED FROM, reduced. The duplicates sit inside an array element, and the edit DELETES
	// an earlier element -- which renumbers the survivor, so its containing object's JSON Pointer changes from /rows/1
	// to /rows/0 while the duplication itself is untouched.
	//
	// A pointer-keyed comparison would call that an introduction and refuse the commit, which is the same false
	// refusal reached by a different route. The census carries no position, so it cannot.

	std::unique_ptr<JsonNode> before = parse ( R"({"rows":[{"x":1},{"k":1,"k":2}]})" );
	std::unique_ptr<JsonNode> after  = parse ( R"({"rows":[{"k":1,"k":2}]})" );

	QVERIFY ( !Validator::introduced_duplicate ( *before, *after ).has_value () );

	// And REMOVING duplication is never an introduction either -- see the case below, which is where that claim
	// actually bites.

	std::unique_ptr<JsonNode> repaired = parse ( R"({"rows":[{"x":1},{"k":1}]})" );

	QVERIFY ( !Validator::introduced_duplicate ( *before, *repaired ).has_value () );
}

void TestValidator::removing_a_duplicate_is_never_an_introduction ()
{
	// THE CASE THE NEUTERED-BUILD PASS FOUND, and it was a live defect rather than a missing test. The first
	// implementation required after's ( key, count ) groups to be contained in before's, one for one -- so three
	// "colour" members becoming TWO matched no group of three and was reported as an introduction. A user tidying up
	// the very duplicates they were being warned about would have had the commit refused.
	//
	// The general rule is an INCREASE per key, which is why this reduces rather than clears: going to zero is the
	// easy case and passes under both readings, so it would not have caught it.

	std::unique_ptr<JsonNode> before = parse ( R"({"colour":1,"colour":2,"colour":3})" );
	std::unique_ptr<JsonNode> after  = parse ( R"({"colour":1,"colour":2})" );

	QVERIFY ( !Validator::introduced_duplicate ( *before, *after ).has_value () );

	// And moving a duplicated group between objects is not one either -- the same excess, somewhere else.

	std::unique_ptr<JsonNode> here  = parse ( R"({"one":{"k":1,"k":2},"two":{}})" );
	std::unique_ptr<JsonNode> there = parse ( R"({"one":{},"two":{"k":1,"k":2}})" );

	QVERIFY ( !Validator::introduced_duplicate ( *here, *there ).has_value () );

	// WHAT IS COUNTED IS THE SURPLUS, NOT THE OCCURRENCES, and this is the shape that tells the two apart: one object
	// holding "k" three times becomes two objects holding it twice each. Occurrences go 3 -> 4 and would read as an
	// introduction; the surplus is 2 either way, and 2 unreachable members became 2 unreachable members.
	//
	// That is the metric VAL-02 is actually about -- a JSON Pointer names the first member with a key, so what an
	// edit can make worse is how many members no pointer can reach.

	std::unique_ptr<JsonNode> together = parse ( R"({"one":{"k":1,"k":2,"k":3}})" );
	std::unique_ptr<JsonNode> split    = parse ( R"({"one":{"k":1,"k":2},"two":{"k":1,"k":2}})" );

	QVERIFY ( !Validator::introduced_duplicate ( *together, *split ).has_value () );
}

void TestValidator::a_further_occurrence_of_an_existing_duplicate_is_introduced ()
{
	// The half a per-key BOOLEAN would miss: the key was already duplicated, and the edit duplicated it harder. The
	// census compares ( key, count ) groups, so a group of 3 does not satisfy a group of 4.

	std::unique_ptr<JsonNode> before = parse ( R"({"colour":1,"colour":2,"colour":3})" );
	std::unique_ptr<JsonNode> after  = parse ( R"({"colour":1,"colour":2,"colour":3,"colour":4})" );

	const std::optional<QString> introduced = Validator::introduced_duplicate ( *before, *after );

	QVERIFY  ( introduced.has_value () );
	QCOMPARE ( introduced.value (), QStringLiteral ( "colour" ) );
}

void TestValidator::a_second_objects_duplicate_is_introduced ()
{
	// The other half: the same COUNT of the same key, in an object that did not have it. Multiset containment catches
	// it because `before` holds one group of 2 where `after` needs two.

	std::unique_ptr<JsonNode> before = parse ( R"({"one":{"k":1,"k":2},"two":{"k":1}})" );
	std::unique_ptr<JsonNode> after  = parse ( R"({"one":{"k":1,"k":2},"two":{"k":1,"k":2}})" );

	const std::optional<QString> introduced = Validator::introduced_duplicate ( *before, *after );

	QVERIFY  ( introduced.has_value () );
	QCOMPARE ( introduced.value (), QStringLiteral ( "k" ) );

	// A brand-new key duplicated in a document that had none at all is the obvious case, and it is here so the two
	// above cannot pass against an implementation that only ever compares counts of keys it already knew.

	std::unique_ptr<JsonNode> clean = parse ( R"({"a":1})" );
	std::unique_ptr<JsonNode> dirty = parse ( R"({"a":1,"a":2})" );

	QCOMPARE ( Validator::introduced_duplicate ( *clean, *dirty ).value (), QStringLiteral ( "a" ) );

	// INSIDE AN ARRAY, which is the walk the census needs and which no case above requires: every fixture here is
	// objects all the way down, so a build that never descended into an array would satisfy them all.

	std::unique_ptr<JsonNode> rows      = parse ( R"({"rows":[{"k":1}]})" );
	std::unique_ptr<JsonNode> duplicated = parse ( R"({"rows":[{"k":1,"k":2}]})" );

	QCOMPARE ( Validator::introduced_duplicate ( *rows, *duplicated ).value (), QStringLiteral ( "k" ) );
}

void TestValidator::introduces_duplicate_guard ()
{
	std::unique_ptr<JsonNode> object = JsonNode::make_object ();
	object->append_member ( QStringLiteral ( "a" ), JsonNode::make_null () );
	object->append_member ( QStringLiteral ( "b" ), JsonNode::make_null () );

	QVERIFY ( Validator::introduces_duplicate ( *object, QStringLiteral ( "a" ) ) );
	QVERIFY ( !Validator::introduces_duplicate ( *object, QStringLiteral ( "c" ) ) );
}

void TestValidator::introduces_duplicate_ignores_self ()
{
	std::unique_ptr<JsonNode> object = JsonNode::make_object ();
	object->append_member ( QStringLiteral ( "a" ), JsonNode::make_null () );
	object->append_member ( QStringLiteral ( "b" ), JsonNode::make_null () );

	// Renaming member 0 to "a" (its own key) is not a collision; renaming it to "b" is.

	QVERIFY ( !Validator::introduces_duplicate ( *object, QStringLiteral ( "a" ), 0 ) );
	QVERIFY ( Validator::introduces_duplicate ( *object, QStringLiteral ( "b" ), 0 ) );
}

void TestValidator::number_validation_data ()
{
	QTest::addColumn<QString> ( "text" );
	QTest::addColumn<bool> ( "valid" );

	QTest::newRow ( "int" )        << QStringLiteral ( "42" )     << true;
	QTest::newRow ( "negative" )   << QStringLiteral ( "-1.5" )   << true;
	QTest::newRow ( "exponent" )   << QStringLiteral ( "1e3" )    << true;
	QTest::newRow ( "trimmed" )    << QStringLiteral ( "  7  " )  << true;
	QTest::newRow ( "empty" )      << QStringLiteral ( "" )       << false;
	QTest::newRow ( "word" )       << QStringLiteral ( "abc" )    << false;
	QTest::newRow ( "trailing" )   << QStringLiteral ( "1x" )     << false;
	QTest::newRow ( "hex" )        << QStringLiteral ( "0x1F" )   << false;
	QTest::newRow ( "leading_dot" )<< QStringLiteral ( ".5" )     << false;
	QTest::newRow ( "plus" )       << QStringLiteral ( "+1" )     << false;
}

void TestValidator::number_validation ()
{
	QFETCH ( QString, text );
	QFETCH ( bool, valid );

	QCOMPARE ( Validator::is_valid_number ( text ), valid );
}

QTEST_APPLESS_MAIN ( TestValidator )

#include "tst_validator.moc"
