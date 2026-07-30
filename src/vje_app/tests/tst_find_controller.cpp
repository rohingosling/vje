//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for FindController -- Find and Go To (FIND-01..04). The whole point of the class is that none of this
//   needs a display: the find bar is a text field and three buttons, and everything worth asserting about Find is here.
//
//   What is pinned:
//
//     - FIND-01: keys and scalar text both match; a node matching on both counts once; Match Case both ways; an empty
//       needle stands the controller down without reporting a failed search.
//     - FIND-02: the match order is DOCUMENT order, next and previous both wrap, each match is published as the
//       selection with SelectionOrigin::Find (which is what reveals it), and the report reads "4 of 17 matches".
//     - FIND-03: no matches reports "No matches" and leaves the selection untouched -- asserted by comparing the
//       selection before and after, not merely by the return value.
//     - The position rule on a re-query: the user keeps their place when the node they are on still matches, and lands
//       on the first match when it does not.
//     - The snapshot rule: an edit under an open query re-runs the search on the next question, keeps the current match
//       BY POINTER when a NEW MATCH appears ahead of it (which moves its position but not its pointer -- the shape that
//       plain index-clamping gets wrong, and the one the case was rewritten to after the first version passed against a
//       neutered build; lessons-learned D10), and never moves the selection by itself.
//     - FIND-04: a resolved pointer selects with SelectionOrigin::GoTo; the two failures are told apart and neither
//       touches the selection; the empty string is the root pointer and resolves.
//     - FIND-05: the copied text is exactly what Go To accepts -- asserted by feeding it straight back in -- and it is
//       PLAIN TEXT ONLY, so the next paste cannot insert a node.
//
//   It runs OFFSCREEN rather than headless, and only because of FIND-05: QClipboard is Qt Gui. Nothing here draws
//   anything, and the offscreen platform is enough for a real clipboard round trip (the same trade tst_clipboard_service
//   makes). Nothing in the suite asserts keyboard focus, which the offscreen platform grants to nothing (Q10).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/FindController.hpp"

#include "services/ClipboardService.hpp"
#include "services/SelectionService.hpp"
#include "services/StatusService.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include <memory>

using namespace vje;

//---------------------------------------------------------------------------------------------------------------------
// The fixture document. Chosen so the three match kinds are separable: "name" appears as a KEY twice and as VALUE text
// once ("codename"), "Alpha" appears as a value in two cases, and "alpha" only in lower case -- which is what makes the
// Match Case cases mean something.
//---------------------------------------------------------------------------------------------------------------------

namespace
{
	const char* const FIXTURE_JSON = R"({
		"name": "Alpha",
		"codename": "the name it ships under",
		"count": 42,
		"active": true,
		"nested":
		{
			"name": "alpha",
			"other": "beta"
		},
		"items": [ "Alpha", "gamma", 42 ]
	})";
}

class TestFindController : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void a_query_matches_keys_and_scalar_text_once_per_node ();
	void an_empty_query_stands_the_controller_down ();
	void match_case_narrows_the_result_set ();

	void matches_are_in_document_order_and_each_is_published_as_a_find_selection ();
	void next_and_previous_wrap_at_the_document_ends ();
	void the_report_names_the_position_and_the_count ();

	void a_query_with_no_matches_reports_it_and_leaves_the_selection_alone ();
	void stepping_with_no_matches_reports_without_moving_the_selection ();

	void a_re_query_keeps_the_place_when_the_current_node_still_matches ();
	void a_re_query_starts_over_when_the_current_node_no_longer_matches ();

	void an_edit_under_an_open_query_re_runs_the_search ();
	void an_insertion_above_the_current_match_keeps_it_by_pointer ();
	void an_edit_never_moves_the_selection_by_itself ();
	void a_document_load_drops_the_results_but_keeps_the_query ();

	void go_to_selects_a_resolvable_pointer ();
	void go_to_tells_the_two_failures_apart_and_changes_nothing ();
	void go_to_accepts_the_empty_string_as_the_root ();

	void the_copied_pointer_is_the_text_go_to_accepts ();
	void the_copied_pointer_is_plain_text_only ();
	void copying_the_root_pointer_succeeds_with_empty_text ();
	void copying_with_no_selection_does_nothing ();

private:

	void load ( const char* text );

	// The pointer text of the current selection, or a marker no pointer can produce -- so "unchanged" is asserted
	// against a value rather than against has_selection().

	QString selection_text () const;

	std::unique_ptr<JsonDocument>      document;
	std::unique_ptr<SelectionService>  selection;
	std::unique_ptr<StatusService>     status;
	std::unique_ptr<ClipboardService>  clipboard;
	std::unique_ptr<FindController>    controller;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestFindController::init ()
{
	document   = std::make_unique<JsonDocument> ();
	selection  = std::make_unique<SelectionService> ();
	status     = std::make_unique<StatusService> ();
	clipboard  = std::make_unique<ClipboardService> ( QGuiApplication::clipboard () );
	controller = std::make_unique<FindController>
	(
		document.get (), selection.get (), status.get (), clipboard.get ()
	);

	QGuiApplication::clipboard ()->clear ();

	load ( FIXTURE_JSON );
}

void TestFindController::cleanup ()
{
	// Strict reverse construction order (lessons-learned Q1): the controller is connected to the document, and a
	// QObject destructor is not passive.

	controller.reset ();
	clipboard.reset ();
	status.reset ();
	selection.reset ();
	document.reset ();
}

void TestFindController::load ( const char* text )
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	QVERIFY2 ( result.ok, "the fixture JSON must parse" );

	document->set_root ( std::move ( result.root ) );
}

QString TestFindController::selection_text () const
{
	return selection->has_selection () ? selection->selection ().to_string () : QStringLiteral ( "<none>" );
}

//---------------------------------------------------------------------------------------------------------------------
// FIND-01 -- what matches
//---------------------------------------------------------------------------------------------------------------------

void TestFindController::a_query_matches_keys_and_scalar_text_once_per_node ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "name" ), false ) );

	// /name       -- key match (its value "Alpha" does not contain the needle)
	// /codename   -- key match AND value match ("the name it ships under"), counted ONCE
	// /nested/name -- key match
	//
	// The one-per-node rule is what makes the count a node count (FIND-02), and /codename is the case that proves it.

	QCOMPARE ( controller->match_count (), 3 );
}

void TestFindController::an_empty_query_stands_the_controller_down ()
{
	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/count" ) ), SelectionOrigin::Tree );

	const QString before = selection_text ();

	QVERIFY ( !controller->set_query ( QString (), false ) );

	QCOMPARE ( controller->match_count (), 0 );
	QCOMPARE ( selection_text (), before );

	// An empty needle is not a failed search, so it must not report one -- the bar would be claiming the user's blank
	// field found nothing.

	QVERIFY ( controller->report ().isEmpty () );
}

void TestFindController::match_case_narrows_the_result_set ()
{
	// Case-insensitive: "Alpha" (/name), "alpha" (/nested/name), "Alpha" (/items/0).

	QVERIFY ( controller->set_query ( QStringLiteral ( "alpha" ), false ) );
	QCOMPARE ( controller->match_count (), 3 );

	// Case-sensitive on the lower-case spelling: only /nested/name.

	QVERIFY ( controller->set_query ( QStringLiteral ( "alpha" ), true ) );
	QCOMPARE ( controller->match_count (), 1 );
	QCOMPARE ( controller->current_match ().to_string (), QStringLiteral ( "/nested/name" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// FIND-02 -- navigation
//---------------------------------------------------------------------------------------------------------------------

void TestFindController::matches_are_in_document_order_and_each_is_published_as_a_find_selection ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "Alpha" ), false ) );

	// Document order, not insertion-into-a-set order: the value in /name, the one nested below it, then the array
	// element.

	QCOMPARE ( controller->current_match ().to_string (), QStringLiteral ( "/name" ) );

	// The selection IS how a match reaches the tree, the editor pane, and the status bar (NAV-01), and the origin is
	// what makes a match inside a collapsed branch reveal itself (EDITOR-04).

	QCOMPARE ( selection_text (), QStringLiteral ( "/name" ) );
	QCOMPARE ( static_cast<int> ( selection->origin () ), static_cast<int> ( SelectionOrigin::Find ) );

	QVERIFY ( controller->find_next () );
	QCOMPARE ( selection_text (), QStringLiteral ( "/nested/name" ) );

	QVERIFY ( controller->find_next () );
	QCOMPARE ( selection_text (), QStringLiteral ( "/items/0" ) );
}

void TestFindController::next_and_previous_wrap_at_the_document_ends ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "Alpha" ), false ) );
	QCOMPARE ( controller->current_position (), 1 );

	// Forward off the end comes back to the first.

	QVERIFY ( controller->find_next () );
	QVERIFY ( controller->find_next () );
	QCOMPARE ( controller->current_position (), 3 );

	QVERIFY ( controller->find_next () );
	QCOMPARE ( controller->current_position (), 1 );

	// Backward off the front goes to the last. Both directions are one expression in step(), which is what stops the
	// wrap being right at one end and wrong at the other.

	QVERIFY ( controller->find_previous () );
	QCOMPARE ( controller->current_position (), 3 );
}

void TestFindController::the_report_names_the_position_and_the_count ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "Alpha" ), false ) );
	QCOMPARE ( controller->report (), QStringLiteral ( "1 of 3 matches" ) );

	QVERIFY ( controller->find_next () );
	QCOMPARE ( controller->report (), QStringLiteral ( "2 of 3 matches" ) );

	// The singular is a separate string, so a lone match does not read "1 of 1 matches".

	QVERIFY ( controller->set_query ( QStringLiteral ( "gamma" ), false ) );
	QCOMPARE ( controller->report (), QStringLiteral ( "1 of 1 match" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// FIND-03 -- no matches
//---------------------------------------------------------------------------------------------------------------------

void TestFindController::a_query_with_no_matches_reports_it_and_leaves_the_selection_alone ()
{
	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/count" ) ), SelectionOrigin::Tree );

	const QString before = selection_text ();

	QVERIFY ( !controller->set_query ( QStringLiteral ( "nothing here" ), false ) );

	QCOMPARE ( controller->match_count (), 0 );
	QCOMPARE ( controller->report (), QStringLiteral ( "No matches" ) );
	QCOMPARE ( selection_text (), before );
}

void TestFindController::stepping_with_no_matches_reports_without_moving_the_selection ()
{
	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/count" ) ), SelectionOrigin::Tree );

	QVERIFY ( !controller->set_query ( QStringLiteral ( "nothing here" ), false ) );

	const QString before = selection_text ();

	QVERIFY ( !controller->find_next () );
	QVERIFY ( !controller->find_previous () );

	QCOMPARE ( selection_text (), before );
	QCOMPARE ( controller->report (), QStringLiteral ( "No matches" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// The position rule on a re-query
//---------------------------------------------------------------------------------------------------------------------

void TestFindController::a_re_query_keeps_the_place_when_the_current_node_still_matches ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "a" ), false ) );

	// Walk somewhere deliberate, then narrow the query so that node is still a match.

	QVERIFY ( controller->set_query ( QStringLiteral ( "alpha" ), false ) );
	QVERIFY ( controller->find_next () );

	QCOMPARE ( controller->current_match ().to_string (), QStringLiteral ( "/nested/name" ) );

	// Typing one more character must feel like narrowing, not restarting: the user stays on the node they are looking
	// at rather than being thrown back to the top of the document.

	QVERIFY ( controller->set_query ( QStringLiteral ( "alpha" ), true ) );
	QCOMPARE ( controller->current_match ().to_string (), QStringLiteral ( "/nested/name" ) );
}

void TestFindController::a_re_query_starts_over_when_the_current_node_no_longer_matches ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "Alpha" ), false ) );
	QVERIFY ( controller->find_next () );

	QCOMPARE ( controller->current_match ().to_string (), QStringLiteral ( "/nested/name" ) );

	// A different needle whose matches do not include where the user was: the first match is the only defensible
	// landing spot.

	QVERIFY ( controller->set_query ( QStringLiteral ( "42" ), false ) );
	QCOMPARE ( controller->current_position (), 1 );
	QCOMPARE ( controller->current_match ().to_string (), QStringLiteral ( "/count" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// The snapshot rule -- the document moving under a live query
//---------------------------------------------------------------------------------------------------------------------

void TestFindController::an_edit_under_an_open_query_re_runs_the_search ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "Alpha" ), false ) );
	QCOMPARE ( controller->match_count (), 3 );

	// Remove the array element that matched. The pointer list taken before this edit is now a claim about a node that
	// is gone, so the next question asked of the controller has to re-run the walk.

	JsonNode* const items = document->resolve ( JsonPointer::parse ( QStringLiteral ( "/items" ) ) );

	QVERIFY ( items != nullptr );

	items->take_element ( 0 );

	document->notify_node_changed ( JsonPointer::parse ( QStringLiteral ( "/items" ) ), DocumentChange::NodeRemoved );

	QCOMPARE ( controller->match_count (), 2 );
}

void TestFindController::an_insertion_above_the_current_match_keeps_it_by_pointer ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "Alpha" ), false ) );
	QVERIFY ( controller->find_next () );

	QCOMPARE ( controller->current_position (), 2 );
	QCOMPARE ( controller->current_match ().to_string (), QStringLiteral ( "/nested/name" ) );

	// A NEW MATCH is added ahead of the current one, so the current match's POSITION in the result list moves (2 -> 3)
	// while its POINTER does not. Keeping the index would silently move the user a match backwards -- which is the
	// whole reason the re-run re-anchors by pointer.

	document->root ()->insert_member ( 0, QStringLiteral ( "alphabet" ), JsonNode::make_string ( QStringLiteral ( "x" ) ) );

	document->notify_node_changed ( JsonPointer (), DocumentChange::NodeAdded );

	QCOMPARE ( controller->match_count (), 4 );

	QCOMPARE ( controller->current_position (), 3 );
	QCOMPARE ( controller->current_match ().to_string (), QStringLiteral ( "/nested/name" ) );
}

void TestFindController::an_edit_never_moves_the_selection_by_itself ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "Alpha" ), false ) );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/count" ) ), SelectionOrigin::Tree );

	const QString before = selection_text ();

	JsonNode* const items = document->resolve ( JsonPointer::parse ( QStringLiteral ( "/items" ) ) );

	QVERIFY ( items != nullptr );

	items->take_element ( 0 );

	document->notify_node_changed ( JsonPointer::parse ( QStringLiteral ( "/items" ) ), DocumentChange::NodeRemoved );

	// The re-run has to be silent. The user is editing, and a find that dragged the selection around under their hands
	// would be unusable -- so the walk is triggered (the count is fresh) but nothing is published.

	QCOMPARE ( controller->match_count (), 2 );
	QCOMPARE ( selection_text (), before );
}

void TestFindController::a_document_load_drops_the_results_but_keeps_the_query ()
{
	QVERIFY ( controller->set_query ( QStringLiteral ( "Alpha" ), false ) );

	load ( R"({ "unrelated": "text" })" );

	// The query survives, so F3 still repeats it against the new document; the previous document's matches do not.

	QCOMPARE ( controller->query (), QStringLiteral ( "Alpha" ) );
	QCOMPARE ( controller->match_count (), 0 );

	load ( R"({ "again": "Alpha" })" );

	// Matches again -- but none of them is CURRENT, because the load put the selection at the new root rather than at a
	// match. Claiming match 1 here would make the next F3 skip it.

	QCOMPARE ( controller->match_count (), 1 );
	QCOMPARE ( controller->current_position (), 0 );

	QVERIFY ( controller->find_next () );
	QCOMPARE ( controller->current_position (), 1 );
}

//---------------------------------------------------------------------------------------------------------------------
// FIND-04 -- Go To
//---------------------------------------------------------------------------------------------------------------------

void TestFindController::go_to_selects_a_resolvable_pointer ()
{
	QCOMPARE ( static_cast<int> ( controller->go_to ( QStringLiteral ( "/nested/other" ) ) ),
	           static_cast<int> ( GoToResult::Selected ) );

	QCOMPARE ( selection_text (), QStringLiteral ( "/nested/other" ) );

	// GoTo reveals, which is what opens a collapsed branch on the way to the node (EDITOR-04).

	QCOMPARE ( static_cast<int> ( selection->origin () ), static_cast<int> ( SelectionOrigin::GoTo ) );
}

void TestFindController::go_to_tells_the_two_failures_apart_and_changes_nothing ()
{
	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/count" ) ), SelectionOrigin::Tree );

	const QString before = selection_text ();

	// Not RFC 6901 text at all: a non-empty pointer must start with '/'.

	QCOMPARE ( static_cast<int> ( controller->go_to ( QStringLiteral ( "nested/other" ) ) ),
	           static_cast<int> ( GoToResult::MalformedPointer ) );

	// Well-formed, but this document has no such node.

	QCOMPARE ( static_cast<int> ( controller->go_to ( QStringLiteral ( "/nested/missing" ) ) ),
	           static_cast<int> ( GoToResult::Unresolvable ) );

	QCOMPARE ( selection_text (), before );

	// The two are told apart because they mean different things to the user, and each carries its own wording.

	QVERIFY ( !describe_go_to_result ( GoToResult::MalformedPointer ).isEmpty () );
	QVERIFY ( !describe_go_to_result ( GoToResult::Unresolvable ).isEmpty () );
	QVERIFY ( describe_go_to_result ( GoToResult::MalformedPointer ) != describe_go_to_result ( GoToResult::Unresolvable ) );
	QVERIFY ( describe_go_to_result ( GoToResult::Selected ).isEmpty () );
}

void TestFindController::go_to_accepts_the_empty_string_as_the_root ()
{
	// RFC 6901: "" IS the whole document, and it is the only way to name the root. Rejecting an empty field would make
	// one destination unreachable.

	QCOMPARE ( static_cast<int> ( controller->go_to ( QString () ) ), static_cast<int> ( GoToResult::Selected ) );

	QVERIFY ( selection->has_selection () );
	QVERIFY ( selection->selection ().is_root () );
}

//---------------------------------------------------------------------------------------------------------------------
// FIND-05 -- Copy JSON Pointer
//---------------------------------------------------------------------------------------------------------------------

void TestFindController::the_copied_pointer_is_the_text_go_to_accepts ()
{
	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/nested/other" ) ), SelectionOrigin::Tree );

	QVERIFY ( controller->copy_selection_pointer () );

	const QString copied = QGuiApplication::clipboard ()->text ();

	QCOMPARE ( copied, QStringLiteral ( "/nested/other" ) );

	// The claim is not "it copied a string" but "it copied the string Go To takes", so the case closes the loop rather
	// than comparing against a second spelling of the pointer. Move the selection away first, or a Go To that changed
	// nothing would look like a success.

	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	QCOMPARE ( static_cast<int> ( controller->go_to ( copied ) ), static_cast<int> ( GoToResult::Selected ) );
	QCOMPARE ( selection_text (), QStringLiteral ( "/nested/other" ) );
}

void TestFindController::the_copied_pointer_is_plain_text_only ()
{
	// A node copy leaves the private application/x-vje-json format on the clipboard. Copying a pointer afterwards must
	// REPLACE it, not sit alongside it -- otherwise the next Ctrl+V would paste the node the user copied ten minutes
	// ago while they believed they had just copied a path.

	JsonNode* const node = document->resolve ( JsonPointer::parse ( QStringLiteral ( "/nested" ) ) );

	QVERIFY ( node != nullptr );

	clipboard->copy_node ( *node, QStringLiteral ( "nested" ) );

	QVERIFY ( clipboard->value () != nullptr );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/count" ) ), SelectionOrigin::Tree );

	QVERIFY ( controller->copy_selection_pointer () );

	const QMimeData* const contents = QGuiApplication::clipboard ()->mimeData ();

	QVERIFY ( contents != nullptr );
	QVERIFY2 ( !contents->hasFormat ( QStringLiteral ( "application/x-vje-json" ) ),
	           "a pointer copy must not leave the private node format behind for the next paste to find" );

	QCOMPARE ( QGuiApplication::clipboard ()->text (), QStringLiteral ( "/count" ) );
}

void TestFindController::copying_the_root_pointer_succeeds_with_empty_text ()
{
	selection->set_selection ( JsonPointer (), SelectionOrigin::Tree );

	QSignalSpy messageSpy ( status.get (), &StatusService::message_posted );

	// The root's pointer IS the empty string (RFC 6901), so this success genuinely leaves the clipboard empty. It is
	// still a success -- and it round-trips, because an empty Go To field means the root.

	QVERIFY ( controller->copy_selection_pointer () );
	QVERIFY ( QGuiApplication::clipboard ()->text ().isEmpty () );

	// And it says so rather than reporting an ordinary copy, which against an empty clipboard would read as a failure.
	// Checked before the round trip below, which posts a message of its own.

	QCOMPARE ( messageSpy.count (), 1 );
	QVERIFY ( messageSpy.first ().first ().toString ().contains ( QStringLiteral ( "root" ) ) );

	QCOMPARE ( static_cast<int> ( controller->go_to ( QString () ) ), static_cast<int> ( GoToResult::Selected ) );
}

void TestFindController::copying_with_no_selection_does_nothing ()
{
	QGuiApplication::clipboard ()->setText ( QStringLiteral ( "untouched" ) );

	QVERIFY ( !selection->has_selection () );
	QVERIFY ( !controller->copy_selection_pointer () );

	// Refused, and the user's clipboard is left exactly as they had it -- a command with nothing to copy must not
	// wipe what is there.

	QCOMPARE ( QGuiApplication::clipboard ()->text (), QStringLiteral ( "untouched" ) );
}

QTEST_MAIN ( TestFindController )

#include "tst_find_controller.moc"
