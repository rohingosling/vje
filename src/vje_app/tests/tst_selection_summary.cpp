//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for selection_summary (spec section 2.8) -- the status bar's node-info pane, headlessly.
//
//   IT EXISTS BECAUSE THERE IS NO MainWindow HARNESS. TREE-09 made a multiple selection possible and the count is the
//   one thing that makes one legible on screen; pulling the decision out of update_status_selection is what makes it
//   assertable rather than a claim left to manual smoke.
//
//   THE CASE THAT MATTERS IS THE THIRD ONE. Reporting the count is only half the rule -- the other half is that it
//   REPLACES the primary's type, which the single-node cases below would happily pass without.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "selection_summary.hpp"

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <memory>

using namespace vje;

class TestSelectionSummary : public QObject
{
	Q_OBJECT

private slots:

	void one_node_is_described_by_its_type ();
	void a_branch_names_how_much_is_in_it ();
	void nothing_selected_says_nothing ();
	void several_nodes_are_reported_as_a_count ();
	void the_count_replaces_the_type_rather_than_joining_it ();
	void the_count_survives_a_primary_that_does_not_resolve ();

private:

	std::unique_ptr<JsonNode> parse ( const char* text ) const;
};

std::unique_ptr<JsonNode> TestSelectionSummary::parse ( const char* text ) const
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	return std::move ( result.root );
}

void TestSelectionSummary::one_node_is_described_by_its_type ()
{
	const std::unique_ptr<JsonNode> string  = parse ( R"("text")" );
	const std::unique_ptr<JsonNode> number  = parse ( "42" );
	const std::unique_ptr<JsonNode> boolean = parse ( "true" );
	const std::unique_ptr<JsonNode> null    = parse ( "null" );

	QCOMPARE ( selection_summary ( string .get (), 1 ), QStringLiteral ( "string" ) );
	QCOMPARE ( selection_summary ( number .get (), 1 ), QStringLiteral ( "number" ) );
	QCOMPARE ( selection_summary ( boolean.get (), 1 ), QStringLiteral ( "boolean" ) );
	QCOMPARE ( selection_summary ( null   .get (), 1 ), QStringLiteral ( "null" ) );
}

void TestSelectionSummary::a_branch_names_how_much_is_in_it ()
{
	const std::unique_ptr<JsonNode> object = parse ( R"({ "a": 1, "b": 2 })" );
	const std::unique_ptr<JsonNode> array  = parse ( "[ 1, 2, 3 ]" );

	// The separator is asserted by code point, so a source file saved in the wrong encoding fails here rather than
	// shipping a status bar with a question mark in it.

	const QChar middleDot ( 0x00B7 );

	QCOMPARE ( selection_summary ( object.get (), 1 ),
	           QStringLiteral ( "object " ) + middleDot + QStringLiteral ( " 2 members" ) );

	QCOMPARE ( selection_summary ( array.get (), 1 ),
	           QStringLiteral ( "array " ) + middleDot + QStringLiteral ( " 3 items" ) );

	// The singular forms are separate strings rather than an "s" bolted on, which is what %n buys and what a count of
	// one is the only way to see.

	const std::unique_ptr<JsonNode> singleMember  = parse ( R"({ "a": 1 })" );
	const std::unique_ptr<JsonNode> singleElement = parse ( "[ 1 ]" );

	QCOMPARE ( selection_summary ( singleMember.get (), 1 ),
	           QStringLiteral ( "object " ) + middleDot + QStringLiteral ( " 1 member" ) );

	QCOMPARE ( selection_summary ( singleElement.get (), 1 ),
	           QStringLiteral ( "array " ) + middleDot + QStringLiteral ( " 1 item" ) );
}

void TestSelectionSummary::nothing_selected_says_nothing ()
{
	// An empty pane, not the word "none": the status bar's panes are blank when they have nothing to report, and a
	// placeholder there would read as a node whose type could not be determined.

	QCOMPARE ( selection_summary ( nullptr, 0 ), QString () );
	QVERIFY  ( selection_summary ( nullptr, 0 ).isEmpty () );
}

void TestSelectionSummary::several_nodes_are_reported_as_a_count ()
{
	const std::unique_ptr<JsonNode> node = parse ( R"("text")" );

	QCOMPARE ( selection_summary ( node.get (), 2 ), QStringLiteral ( "2 nodes selected" ) );
	QCOMPARE ( selection_summary ( node.get (), 3 ), QStringLiteral ( "3 nodes selected" ) );
	QCOMPARE ( selection_summary ( node.get (), 17 ), QStringLiteral ( "17 nodes selected" ) );
}

void TestSelectionSummary::the_count_replaces_the_type_rather_than_joining_it ()
{
	// TREE-09's actual rule, and the one a build that merely APPENDED the count would pass every case above. With four
	// nodes selected, naming the primary's type describes less than what a multi-node command would act on.

	const std::unique_ptr<JsonNode> object = parse ( R"({ "a": 1, "b": 2 })" );

	const QString summary = selection_summary ( object.get (), 4 );

	QCOMPARE ( summary, QStringLiteral ( "4 nodes selected" ) );

	QVERIFY2 ( !summary.contains ( QStringLiteral ( "object" ) ), qPrintable ( summary ) );
	QVERIFY2 ( !summary.contains ( QStringLiteral ( "member" ) ), qPrintable ( summary ) );
}

void TestSelectionSummary::the_count_survives_a_primary_that_does_not_resolve ()
{
	// The count is answered without consulting the node, so a multiple selection stays legible in the window where the
	// primary momentarily names nothing -- the state every other pane blanks in.

	QCOMPARE ( selection_summary ( nullptr, 3 ), QStringLiteral ( "3 nodes selected" ) );

	// And a count of one is NOT the multiple form: one node with no resolvable primary has nothing to say, which is
	// what keeps "1 node selected" off the screen in the ordinary single-selection case.

	QCOMPARE ( selection_summary ( nullptr, 1 ), QString () );
}

QTEST_APPLESS_MAIN ( TestSelectionSummary )

#include "tst_selection_summary.moc"
