//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for JsonPathView (QUERY-01, QUERY-05..07) -- the WIRING, offscreen.
//
//   WHAT IS NOT COVERED HERE, DELIBERATELY. Which constructs the grammar accepts and what each of them selects is
//   JsonPathQuery's, and tst_json_path_query already asserts all of it against the spec's worked document. Running the
//   grammar again through the widget would pin nothing new. What is asserted here is only what the VIEW adds:
//
//     - A query runs, and its results reach the list as pointer plus preview (QUERY-06).
//     - Choosing a result publishes it as a GoTo selection -- and a stale row that no longer resolves reports instead.
//     - An error selects the offending span in the query box, reports on the STATUS BAR, and leaves the previous
//       results STANDING (QUERY-05).
//     - Both panes are CARDS, and both are square whatever Interface style says (STYLE-02).
//     - An edit marks the results stale and the next question re-runs them, and a re-run never moves the selection
//       (QUERY-07). A document RESET drops them outright.
//     - Enter runs and Shift+Enter breaks the line (QUERY-01).
//     - The view ignores the tree selection, and its provider ignores the node it is handed.
//
//   Keyboard FOCUS is not asserted anywhere: the offscreen platform grants it to nothing (lessons-learned Q10).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "services/SelectionService.hpp"
#include "services/StatusService.hpp"
#include "views/JsonPathView.hpp"
#include "services/ClipboardService.hpp"
#include "views/Card.hpp"
#include "views/WorkspaceSplitter.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QImage>
#include <QSignalSpy>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <memory>

using namespace vje;

namespace
{
	const char* const SAMPLE_DOCUMENT = R"({
		"id": 1001,
		"name": "Alex Rivera",
		"roles": [ "admin", "editor" ],
		"profile": { "email": "alex@example.com" },
		"projects":
		[
			{ "name": "JSON Editor",    "priority": 1 },
			{ "name": "Data Migration", "priority": 2 }
		]
	})";

	std::unique_ptr<JsonNode> parse ( const char* json )
	{
		ParseResult result = JsonParser::parse ( QString::fromUtf8 ( json ) );

		return std::move ( result.root );
	}

	QStringList pointer_column ( const QTreeWidget& tree )
	{
		QStringList out;

		for ( int row = 0; row < tree.topLevelItemCount (); ++row )
		{
			out << tree.topLevelItem ( row )->text ( 0 );
		}

		return out;
	}

	QStringList value_column ( const QTreeWidget& tree )
	{
		QStringList out;

		for ( int row = 0; row < tree.topLevelItemCount (); ++row )
		{
			out << tree.topLevelItem ( row )->text ( 1 );
		}

		return out;
	}

	// The whole collaborator set, torn down in strict reverse construction order -- the undo stack outliving its
	// document is lessons-learned Q1, and it has bitten this project on Linux twice.

	struct Fixture
	{
		JsonDocument     document;
		UndoController   undo;
		SelectionService selection;
		StatusService    status;
		ClipboardService clipboard;
		JsonPathView     view;

		// What the view has told the STATUS BAR, which is where its reporting goes now (QUERY-06). Recorded from the
		// service's own signals rather than by reading a label, because there is no label any more -- and because this
		// is the seam MainWindow listens on, so what is recorded here is what the user would see.

		QStringList posted;
		int         clears = 0;

		// Every result set the view has announced, in order (QUERY-08). Recorded through the same hook MainWindow uses
		// for Copy JSONPath Result's enablement, because that is the only trigger it has -- a query changes neither the
		// selection, the document, the undo stack, the focus nor the clipboard.

		QVector<bool> announced;

		Fixture ()
		:
			undo ( &document ),
			clipboard ( QGuiApplication::clipboard () ),
			view ( &document, &selection, nullptr, &status, &clipboard )
		{
			view.set_results_changed_callback ( [ this ] () { announced.append ( view.has_results () ); } );

			// So that a refusal case can assert the clipboard was left ALONE rather than merely that it does not hold
			// what this view would have put there -- the two are indistinguishable against whatever the last case left.

			QGuiApplication::clipboard ()->clear ();

			QObject::connect ( &status, &StatusService::message_posted,
			                   [ this ] ( const QString& text, int ) { posted.append ( text ); } );

			QObject::connect ( &status, &StatusService::message_cleared, [ this ] () { ++clears; } );

			document.set_root ( parse ( SAMPLE_DOCUMENT ) );

			// The hook is installed BEFORE the document arrives, exactly as the provider installs it before the view
			// sees anything -- so loading this one legitimately announces an empty set, and the record is cleared
			// afterwards rather than the order being rearranged to avoid it. What a case asserts is what the case did.

			announced.clear ();
		}

		// The last thing the status bar was told, or an empty string if it was told nothing.

		QString last_posted () const
		{
			return posted.isEmpty () ? QString () : posted.last ();
		}

		void run ( const QString& query )
		{
			view.set_query_text ( query );
			view.run_query ();
		}
	};
}

class TestJsonPathView : public QObject
{
	Q_OBJECT

private slots:

	void a_query_runs_and_lists_its_results ();
	void a_result_row_shows_its_pointer_and_a_value_preview ();
	void choosing_a_result_publishes_it_as_a_go_to_selection ();
	void an_error_marks_the_query_and_leaves_the_previous_results_standing ();
	void an_empty_query_clears_the_results_and_reports_nothing ();
	void the_report_counts_in_the_singular_and_the_plural ();

	void an_edit_marks_the_results_stale_without_re_running_them ();
	void the_next_question_re_runs_a_stale_query ();
	void a_stale_refresh_never_moves_the_selection ();
	void choosing_a_result_is_a_question_and_re_runs_a_stale_query ();
	void one_double_click_is_one_choice ();
	void choosing_a_row_the_document_no_longer_resolves_reports_and_changes_nothing ();
	void a_reset_drops_the_results_and_keeps_the_query ();

	void enter_runs_the_query_and_shift_enter_breaks_the_line ();

	void the_query_splitter_wears_the_workspace_grip ();
	void each_query_pane_sits_in_a_card ();
	void the_query_cards_are_square_whatever_the_interface_style ();

	void the_view_ignores_the_tree_selection ();
	void the_provider_answers_about_the_document_and_not_about_the_node ();
	void the_query_view_is_registered_last_and_is_not_open_on_a_first_run ();

	// -- Copy JSONPath Result (QUERY-08) ---------------------------------------------------------------------------

	void the_copied_text_is_every_pointer_one_per_line ();
	void the_root_contributes_an_empty_line_rather_than_its_label ();
	void the_copied_text_is_what_go_to_accepts ();
	void copying_writes_plain_text_and_never_the_node_format ();
	void copying_refreshes_a_stale_set_first ();
	void copying_refuses_when_there_is_nothing_to_copy ();
	void has_results_follows_the_list_rather_than_the_run ();
	void every_change_to_the_result_set_is_announced ();
	void the_provider_hands_the_hook_to_every_view_it_builds ();

	// -- The results context menu (QUERY-09) -----------------------------------------------------------------------

	void the_row_commands_appear_only_over_a_row ();
	void the_whole_set_command_is_disabled_rather_than_absent ();
	void a_row_command_goes_to_the_node_that_row_names ();
	void a_row_command_copies_that_row_s_pointer_alone ();
	void a_row_naming_a_node_the_document_lost_reports_and_changes_nothing ();
};

//=======================================================================================================================
// Running a query (QUERY-06)
//=======================================================================================================================

void TestJsonPathView::a_query_runs_and_lists_its_results ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QCOMPARE ( pointer_column ( *fixture.view.results_view () ),
	           ( QStringList { QStringLiteral ( "/name" ),
	                           QStringLiteral ( "/projects/0/name" ),
	                           QStringLiteral ( "/projects/1/name" ) } ) );

	QCOMPARE ( static_cast<int> ( fixture.view.results ().size () ), 3 );
	QCOMPARE ( fixture.view.report (), QStringLiteral ( "3 results" ) );

	QVERIFY ( fixture.view.last_error ().message.isEmpty () );
	QVERIFY ( !fixture.view.results_are_stale () );
}

void TestJsonPathView::a_result_row_shows_its_pointer_and_a_value_preview ()
{
	Fixture fixture;

	// A scalar previews its text, a container the shared {...} / [...] placeholder, and the ROOT is named rather than
	// left blank -- its pointer is the empty string (RFC 6901), which would otherwise be an empty first column.

	fixture.run ( QStringLiteral ( "$['id','roles','profile']" ) );

	QCOMPARE ( value_column ( *fixture.view.results_view () ),
	           ( QStringList { QStringLiteral ( "1001" ),
	                           QStringLiteral ( "[...]" ),
	                           QStringLiteral ( "{...}" ) } ) );

	fixture.run ( QStringLiteral ( "$" ) );

	QCOMPARE ( pointer_column ( *fixture.view.results_view () ), QStringList { QStringLiteral ( "(root)" ) } );
	QCOMPARE ( value_column   ( *fixture.view.results_view () ), QStringList { QStringLiteral ( "{...}" ) } );
}

void TestJsonPathView::choosing_a_result_publishes_it_as_a_go_to_selection ()
{
	Fixture fixture;

	QSignalSpy spy ( &fixture.selection, &SelectionService::selection_changed );

	fixture.run ( QStringLiteral ( "$.projects[?(@.priority > 1)].name" ) );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 1 );

	// Running the query publishes nothing: a result list is chosen FROM, and producing it is not a gesture.

	QCOMPARE ( spy.count (), 0 );

	emit fixture.view.results_view ()->itemActivated ( fixture.view.results_view ()->topLevelItem ( 0 ), 0 );

	QCOMPARE ( spy.count (), 1 );

	QCOMPARE ( spy.at ( 0 ).at ( 0 ).value<JsonPointer> ().to_string (), QStringLiteral ( "/projects/1/name" ) );

	// GoTo, so the tree reveals into a collapsed branch and the status bar reports -- both of which already exist and
	// neither of which this phase re-implements.

	QCOMPARE ( spy.at ( 0 ).at ( 1 ).value<SelectionOrigin> (), SelectionOrigin::GoTo );
	QVERIFY  ( reveals_selection ( SelectionOrigin::GoTo ) );
}

void TestJsonPathView::an_error_marks_the_query_and_leaves_the_previous_results_standing ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 3 );

	// A typo, deliberately in the MIDDLE of the query rather than at its end, so a marker that merely put the caret
	// somewhere plausible would be visible here.

	fixture.run ( QStringLiteral ( "$.roles[::0].name" ) );

	// The previous results STAND. An empty list would read as "your query now matches nothing", which is an answer
	// about the document rather than a complaint about the query (QUERY-05).

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 3 );
	QCOMPARE ( static_cast<int> ( fixture.view.results ().size () ), 3 );

	QVERIFY ( !fixture.view.last_error ().message.isEmpty () );
	QCOMPARE ( fixture.view.report (), fixture.view.last_error ().message );

	// Reported on the STATUS BAR, which is where every other outcome in the application lands (QUERY-06).

	QCOMPARE ( fixture.last_posted (), fixture.view.last_error ().message );

	// And the offending text is SELECTED in the query box, which is what makes the message actionable rather than
	// merely informative -- and what the error's position exists for.

	const QTextCursor cursor = fixture.view.query_box ()->textCursor ();

	QVERIFY  ( cursor.hasSelection () );
	QCOMPARE ( cursor.selectionStart (), fixture.view.last_error ().position );
	QCOMPARE ( cursor.selectionEnd (),   fixture.view.last_error ().position + fixture.view.last_error ().length );
	QCOMPARE ( cursor.selectedText (),   QStringLiteral ( "0" ) );
}

void TestJsonPathView::an_empty_query_clears_the_results_and_reports_nothing ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 3 );

	// An empty query is not a failed search: it reports NOTHING rather than "No results", which would claim the
	// document had been looked at (QUERY-05).

	const int clearsBefore = fixture.clears;

	fixture.run ( QString () );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 0 );
	QVERIFY  ( fixture.view.report ().isEmpty () );
	QVERIFY  ( fixture.view.last_error ().message.isEmpty () );

	// And it CLEARS the status bar rather than posting nothing: leaving "3 results" standing after the query that
	// produced it was emptied would read as current (QUERY-05).

	QCOMPARE ( fixture.clears, clearsBefore + 1 );
}

void TestJsonPathView::the_report_counts_in_the_singular_and_the_plural ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$.id" ) );

	QCOMPARE ( fixture.view.report (), QStringLiteral ( "1 result" ) );

	fixture.run ( QStringLiteral ( "$.roles[*]" ) );

	QCOMPARE ( fixture.view.report (), QStringLiteral ( "2 results" ) );

	// A well-formed query that matches nothing IS a failed search, and says so -- unlike the empty query above.

	fixture.run ( QStringLiteral ( "$.missing" ) );

	QCOMPARE ( fixture.view.report (), QStringLiteral ( "No results" ) );

	// Every one of those reached the STATUS BAR, in order and in the same words -- report() states the wording and the
	// status bar is where it goes (QUERY-06).

	QCOMPARE ( fixture.posted,
	           ( QStringList { QStringLiteral ( "1 result" ),
	                           QStringLiteral ( "2 results" ),
	                           QStringLiteral ( "No results" ) } ) );
}

//=======================================================================================================================
// Staleness (QUERY-07)
//=======================================================================================================================

void TestJsonPathView::an_edit_marks_the_results_stale_without_re_running_them ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$.roles[*]" ) );

	QCOMPARE ( fixture.view.report (), QStringLiteral ( "2 results" ) );

	fixture.undo.add_child ( JsonPointer::parse ( QStringLiteral ( "/roles" ) ), JsonKind::String );

	// MARKED, not re-run: with the tab in the background nobody is looking, and an editing session that is not a query
	// session should cost a flag per edit rather than a walk of the document (NFR-03).

	QVERIFY  ( fixture.view.results_are_stale () );
	QCOMPARE ( static_cast<int> ( fixture.view.results ().size () ), 2 );
	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 2 );

	// The staleness is marked SILENTLY. The view may be in a background tab, and a message from there would land on
	// top of the one the edit that caused it just posted -- so nothing new reaches the status bar (QUERY-07).

	QCOMPARE ( fixture.last_posted (), QStringLiteral ( "2 results" ) );

	// It is still visible to the view itself, which is what makes the next question re-run.

	QVERIFY ( fixture.view.report ().contains ( QStringLiteral ( "document changed" ) ) );
}

void TestJsonPathView::the_next_question_re_runs_a_stale_query ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$.roles[*]" ) );

	fixture.undo.add_child ( JsonPointer::parse ( QStringLiteral ( "/roles" ) ), JsonKind::String );

	QVERIFY ( fixture.view.results_are_stale () );

	// The tab becoming visible is a question, and it is the one that makes the laziness pay rather than cost.

	fixture.view.view_activated ();

	QVERIFY  ( !fixture.view.results_are_stale () );
	QCOMPARE ( static_cast<int> ( fixture.view.results ().size () ), 3 );
	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 3 );
	QCOMPARE ( fixture.view.report (), QStringLiteral ( "3 results" ) );

	// And THAT is what reaches the status bar -- the answer that is true now, rather than the staleness that was true
	// a moment ago. Resolved rather than announced (QUERY-07).

	QCOMPARE ( fixture.last_posted (), QStringLiteral ( "3 results" ) );

	// A refresh re-runs the query the results CAME FROM, not whatever the user has since typed into the box: a stale
	// refresh must not silently answer a question that has not been asked.

	fixture.view.set_query_text ( QStringLiteral ( "$.id" ) );

	fixture.undo.add_child ( JsonPointer::parse ( QStringLiteral ( "/roles" ) ), JsonKind::String );

	fixture.view.view_activated ();

	QCOMPARE ( static_cast<int> ( fixture.view.results ().size () ), 4 );
}

void TestJsonPathView::a_stale_refresh_never_moves_the_selection ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QSignalSpy spy ( &fixture.selection, &SelectionService::selection_changed );

	fixture.undo.add_child ( JsonPointer::parse ( QStringLiteral ( "/roles" ) ), JsonKind::String );

	fixture.view.view_activated ();

	// Neither the edit nor the re-run publishes anything. Only an explicit choice of a result does (QUERY-07) -- a
	// query that dragged the selection around while the user was editing would be unusable.

	QCOMPARE ( spy.count (), 0 );
}

void TestJsonPathView::choosing_a_result_is_a_question_and_re_runs_a_stale_query ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$.roles[*]" ) );

	fixture.undo.add_child ( JsonPointer::parse ( QStringLiteral ( "/roles" ) ), JsonKind::String );

	QVERIFY ( fixture.view.results_are_stale () );

	QSignalSpy spy ( &fixture.selection, &SelectionService::selection_changed );

	emit fixture.view.results_view ()->itemActivated ( fixture.view.results_view ()->topLevelItem ( 0 ), 0 );

	// The chosen row is resolved against the document AS IT STANDS and published straight away -- the refresh does not
	// come between the click and its answer.

	QCOMPARE ( spy.count (), 1 );
	QCOMPARE ( spy.at ( 0 ).at ( 0 ).value<JsonPointer> ().to_string (), QStringLiteral ( "/roles/0" ) );

	// And the re-run happens on the NEXT turn of the event loop, because rebuilding the list synchronously would
	// destroy the item whose signal we were inside (architecture.md section 7).

	QVERIFY ( fixture.view.results_are_stale () );

	QCoreApplication::processEvents ();

	QVERIFY  ( !fixture.view.results_are_stale () );
	QCOMPARE ( static_cast<int> ( fixture.view.results ().size () ), 3 );
	QCOMPARE ( fixture.view.report (), QStringLiteral ( "3 results" ) );
}

void TestJsonPathView::one_double_click_is_one_choice ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$.id" ) );

	QSignalSpy spy ( &fixture.selection, &SelectionService::selection_changed );

	// What a real double click delivers: itemClicked twice and itemActivated once, all within one turn of the event
	// loop. They are one gesture, and SelectionService re-emits unconditionally (a re-selection carries a fresh reveal
	// intent), so without the guard the tree would be told to reveal the same node three times.

	QTreeWidgetItem* const row = fixture.view.results_view ()->topLevelItem ( 0 );

	emit fixture.view.results_view ()->itemClicked   ( row, 0 );
	emit fixture.view.results_view ()->itemClicked   ( row, 0 );
	emit fixture.view.results_view ()->itemActivated ( row, 0 );

	QCOMPARE ( spy.count (), 1 );

	// A LATER, separate choice is not suppressed -- the guard is per gesture, not per row.

	QCoreApplication::processEvents ();

	emit fixture.view.results_view ()->itemClicked ( row, 0 );

	QCOMPARE ( spy.count (), 2 );
}

void TestJsonPathView::choosing_a_row_the_document_no_longer_resolves_reports_and_changes_nothing ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$.roles[*]" ) );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 2 );

	// Delete both elements, so /roles/1 -- a row still on screen -- names nothing at all.

	fixture.undo.delete_node ( JsonPointer::parse ( QStringLiteral ( "/roles/1" ) ) );
	fixture.undo.delete_node ( JsonPointer::parse ( QStringLiteral ( "/roles/0" ) ) );

	QSignalSpy spy ( &fixture.selection, &SelectionService::selection_changed );

	emit fixture.view.results_view ()->itemActivated ( fixture.view.results_view ()->topLevelItem ( 1 ), 0 );

	// Reported in place, and nothing changes -- FIND-04's Unresolvable reached from the other direction.

	QCOMPARE ( spy.count (), 0 );
	QVERIFY  ( fixture.last_posted ().contains ( QStringLiteral ( "no longer" ) ) );
}

void TestJsonPathView::a_reset_drops_the_results_and_keeps_the_query ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 3 );

	// A reset is different IN KIND from an edit: the previous document's pointers name nothing, so the results are
	// dropped rather than marked stale.

	fixture.document.set_root ( parse ( "{\"other\":1}" ) );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 0 );
	QVERIFY  ( !fixture.view.results_are_stale () );
	QVERIFY  ( fixture.view.report ().isEmpty () );

	// The QUERY is kept -- it is the user's, and it is very likely the first thing they want to run against whatever
	// has just been loaded.

	QCOMPARE ( fixture.view.query_text (), QStringLiteral ( "$..name" ) );

	fixture.view.run_query ();

	QCOMPARE ( fixture.view.report (), QStringLiteral ( "No results" ) );
}

//=======================================================================================================================
// The query box (QUERY-01)
//=======================================================================================================================

void TestJsonPathView::enter_runs_the_query_and_shift_enter_breaks_the_line ()
{
	Fixture fixture;

	fixture.view.set_query_text ( QStringLiteral ( "$.roles[*]" ) );

	// Enter RUNS -- the other way round from a text editor, and the reason the query box can be a text box at all.

	QTest::keyClick ( fixture.view.query_box (), Qt::Key_Return );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 2 );
	QCOMPARE ( fixture.view.query_text (), QStringLiteral ( "$.roles[*]" ) );

	// Shift+Enter breaks the line, and the query is unchanged by it: whitespace between tokens is insignificant
	// (QUERY-02), so the same query re-run across two lines gives the same answer.

	fixture.view.query_box ()->moveCursor ( QTextCursor::Start );

	QTest::keyClick ( fixture.view.query_box (), Qt::Key_Return, Qt::ShiftModifier );

	QVERIFY ( fixture.view.query_text ().contains ( QLatin1Char ( '\n' ) ) );

	fixture.view.run_query ();

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 2 );
	QVERIFY  ( fixture.view.last_error ().message.isEmpty () );
}

//=======================================================================================================================
// The splitter (STYLE-04)
//=======================================================================================================================

void TestJsonPathView::the_query_splitter_wears_the_workspace_grip ()
{
	Fixture fixture;

	QSplitter* const splitter = fixture.view.splitter ();

	// The same class the workspace splitter is, so the grip comes from the same painter -- and the same handle width,
	// so it reads as the same control rather than merely the same colour.

	QVERIFY  ( qobject_cast<WorkspaceSplitter*> ( splitter ) != nullptr );
	QCOMPARE ( splitter->handleWidth (), config::editor::QUERY_SPLITTER_THICKNESS );

	// And the colour claim is checked in RENDERED PIXELS (lessons-learned Q12), against the palette-derived tones
	// style/tone states -- not against a second splitter, whose layout state would be the thing under test. Fusion
	// paints its own grip from two FIXED translucent overlays rather than from the palette, so a plain QSplitter
	// produces neither of these colours exactly, whatever the theme.

	fixture.view.resize ( 240, 240 );
	fixture.view.show ();

	const QImage shot = fixture.view.grab ().toImage ();

	const QRect handleRect ( splitter->mapTo ( &fixture.view, splitter->handle ( 1 )->geometry ().topLeft () ),
	                         splitter->handle ( 1 )->size () );

	QVERIFY ( handleRect.isValid () );
	QCOMPARE ( handleRect.height (), config::editor::QUERY_SPLITTER_THICKNESS );

	const QColor ground = QApplication::palette ().color ( QPalette::Window );

	const QRgb groundRgb = ground.rgb ();
	const QRgb mainRgb   = contrasting_tone ( ground, config::workspace::GRIP_MAIN_CONTRAST  ).rgb ();
	const QRgb bevelRgb  = contrasting_tone ( ground, config::workspace::GRIP_BEVEL_CONTRAST ).rgb ();

	// Compared as RGB rather than as QColor, because QColor::operator== compares the colour SPEC and contrasting_tone
	// returns an HSL colour (lessons-learned Q19).

	int gripPixels = 0;

	for ( int y = handleRect.top (); y <= handleRect.bottom (); ++y )
	{
		for ( int x = handleRect.left (); x <= handleRect.right (); ++x )
		{
			const QRgb here = shot.pixelColor ( x, y ).rgb ();

			QVERIFY2 ( ( here == groundRgb ) || ( here == mainRgb ) || ( here == bevelRgb ),
			           qPrintable ( QStringLiteral ( "unexpected grip colour %1 at (%2,%3)" )
			                        .arg ( QColor ( here ).name () ).arg ( x ).arg ( y ) ) );

			if ( here == mainRgb )
			{
				++gripPixels;
			}
		}
	}

	// Not vacuous: the grip is actually drawn, so an unpainted handle could not pass by carrying only the ground.

	QCOMPARE ( gripPixels,
	           ( config::workspace::GRIP_DOT_COUNT * config::workspace::GRIP_DOT_SIZE
	             * config::workspace::GRIP_DOT_SIZE ) - config::workspace::GRIP_DOT_COUNT );
}

void TestJsonPathView::each_query_pane_sits_in_a_card ()
{
	Fixture fixture;

	// STYLE-01/02/05: the results and the query box are each a pane, and a pane in this application sits in a Card --
	// which is where its fill, its one-pixel border and its corner discipline come from.

	QVERIFY ( fixture.view.results_card () != nullptr );
	QVERIFY ( fixture.view.query_card   () != nullptr );

	// Each card holds its pane, and the SPLITTER holds the cards -- so dragging the split moves two card surfaces
	// rather than two bare widgets inside one.

	QVERIFY ( fixture.view.results_card ()->isAncestorOf ( fixture.view.results_view () ) );
	QVERIFY ( fixture.view.query_card   ()->isAncestorOf ( fixture.view.query_box    () ) );

	QCOMPARE ( fixture.view.splitter ()->count (), 2 );
	QCOMPARE ( fixture.view.splitter ()->widget ( 0 ), static_cast<QWidget*> ( fixture.view.results_card () ) );
	QCOMPARE ( fixture.view.splitter ()->widget ( 1 ), static_cast<QWidget*> ( fixture.view.query_card   () ) );

	// The panes draw NO frame of their own: the card draws the border, and a sunken panel inside it would be a second
	// edge one pixel in from the first. CodeEditor and TextView already do this.

	QCOMPARE ( fixture.view.results_view ()->frameShape (), QFrame::NoFrame );
	QCOMPARE ( fixture.view.query_box    ()->frameShape (), QFrame::NoFrame );

	// And the view paints the BACKDROP, which is what makes a nested card correct rather than merely present: a Card
	// fills its corner wedges with QPalette::Window and tones its border from the same role, so it has to be standing
	// on that colour (Card.hpp).

	QVERIFY  ( fixture.view.autoFillBackground () );
	QCOMPARE ( fixture.view.backgroundRole (), QPalette::Window );
}

void TestJsonPathView::the_query_cards_are_square_whatever_the_interface_style ()
{
	// STYLE-02's fillet exists to soften a pane against the WINDOW BACKDROP, and a card nested inside another card is
	// not against it -- so these two are square under BOTH interface styles, where the editor card around them still
	// fillets under Fluent. Asserted against a real store set each way, because the value that would be wrong is
	// exactly the one a settings-driven implementation would produce.

	QTemporaryDir directory;

	QVERIFY ( directory.isValid () );

	SettingsStore store ( directory.filePath ( QStringLiteral ( "settings.json" ) ) );

	JsonDocument     document;
	SelectionService selection;

	for ( const QString& style : { settings_values::INTERFACE_STYLE_FLUENT, settings_values::INTERFACE_STYLE_CLASSIC } )
	{
		store.set_string ( settings_keys::INTERFACE_STYLE, style );

		JsonPathView view ( &document, &selection, &store );

		QVERIFY2 ( !view.results_card ()->top_corners_rounded (), qPrintable ( style ) );
		QVERIFY2 ( !view.query_card   ()->top_corners_rounded (), qPrintable ( style ) );

		// And a change WHILE OPEN does not move them either, which is what a leftover settings connection would do.

		store.set_string ( settings_keys::INTERFACE_STYLE, settings_values::INTERFACE_STYLE_FLUENT );

		QVERIFY2 ( !view.results_card ()->top_corners_rounded (), qPrintable ( style ) );
		QVERIFY2 ( !view.query_card   ()->top_corners_rounded (), qPrintable ( style ) );
	}

	// Not vacuous: a Card left alone DOES round under the default style, so the two assertions above are reading a
	// value this view set rather than one that was never going to be true.

	Card untouched;

	QVERIFY ( untouched.top_corners_rounded () );
}

//=======================================================================================================================
// Document-wide, not per-node (QUERY-01)
//=======================================================================================================================

void TestJsonPathView::the_view_ignores_the_tree_selection ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	const QString before = fixture.view.report ();

	// present() is a NO-OP. Arrowing down the tree would otherwise re-walk the document once per key press for an
	// answer that cannot have changed.

	fixture.view.present ( JsonPointer::parse ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );
	fixture.view.present ( JsonPointer (),                                          SelectionOrigin::GoTo );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 3 );
	QCOMPARE ( fixture.view.report (), before );
	QVERIFY  ( !fixture.view.results_are_stale () );
}

void TestJsonPathView::the_provider_answers_about_the_document_and_not_about_the_node ()
{
	JsonDocument document;

	const JsonPathViewProvider provider ( &document, nullptr, nullptr, nullptr );

	// No document, no tab -- whatever node it is handed.

	QVERIFY ( !provider.can_present ( nullptr ) );

	document.set_root ( parse ( SAMPLE_DOCUMENT ) );

	// With a document the answer is yes for EVERY node, including the null one EditorPane passes before the first
	// selection arrives. That last case is the one that matters: every other provider answers no to it, and a query
	// view that did would have no tab until something was selected.

	QVERIFY ( provider.can_present ( nullptr ) );
	QVERIFY ( provider.can_present ( document.root () ) );
	QVERIFY ( provider.can_present ( document.root ()->find_member ( QStringLiteral ( "id" ) ) ) );
	QVERIFY ( provider.can_present ( document.root ()->find_member ( QStringLiteral ( "roles" ) ) ) );
}

void TestJsonPathView::the_query_view_is_registered_last_and_is_not_open_on_a_first_run ()
{
	JsonDocument document;

	const JsonPathViewProvider provider ( &document, nullptr, nullptr, nullptr );

	QCOMPARE ( provider.view_id (), QString::fromUtf8 ( config::editor::view_ids::JSONPATH ) );
	QCOMPARE ( provider.display_order (), 3 );

	// Its id is spelled ONCE, in AppConfig, which is what the other three providers already do.

	QCOMPARE ( JsonPathViewProvider::VIEW_ID, QStringLiteral ( "jsonpath" ) );

	// And it is deliberately NOT in the first-run open set (QUERY-01): an existing user's stored set never gains a
	// view added later (VIEW-03), so a default here would make a fresh installation differ permanently from every
	// upgrade.

	bool listed = false;

	for ( const char* const viewId : config::editor::DEFAULT_OPEN_VIEWS )
	{
		listed = listed || ( QString::fromUtf8 ( viewId ) == JsonPathViewProvider::VIEW_ID );
	}

	QVERIFY ( !listed );

	// It still names a glyph, so the tab and the View menu entry carry the view's own mark the day the icon author
	// draws it -- and show their label alone until then (IEditorView.hpp's documented fallback).

	QVERIFY ( !provider.icon_name ().isEmpty () );
}

//=======================================================================================================================
// Copy JSONPath Result (QUERY-08)
//=======================================================================================================================

void TestJsonPathView::the_copied_text_is_every_pointer_one_per_line ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QCOMPARE
	(
		fixture.view.results_as_pointer_text (),
		QStringLiteral ( "/name\n/projects/0/name\n/projects/1/name" )
	);

	QVERIFY  ( fixture.view.copy_results () );
	QCOMPARE ( QGuiApplication::clipboard ()->text (), fixture.view.results_as_pointer_text () );

	// The report is the count in its own words, and the plural is spelled separately for the reason FindController's
	// is: a counter that cannot get its own plural right reads as broken.

	QCOMPARE ( fixture.last_posted (), QStringLiteral ( "Copied 3 JSON Pointers" ) );

	fixture.run ( QStringLiteral ( "$.id" ) );

	QVERIFY  ( fixture.view.copy_results () );
	QCOMPARE ( fixture.last_posted (), QStringLiteral ( "Copied 1 JSON Pointer" ) );
}

void TestJsonPathView::the_root_contributes_an_empty_line_rather_than_its_label ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$" ) );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 1 );

	// The ROW is labelled, because a blank first column reads as a rendering fault...

	QCOMPARE ( pointer_column ( *fixture.view.results_view () ), QStringList { QStringLiteral ( "(root)" ) } );

	// ...and what is COPIED is the pointer, which for the root is the empty string (RFC 6901). Copying "(root)" would
	// put text into the clipboard that Go To rejects, which is exactly the round trip this command exists to keep.

	QVERIFY  ( fixture.view.results_as_pointer_text ().isEmpty () );
	QVERIFY  ( fixture.view.copy_results () );
	QVERIFY  ( QGuiApplication::clipboard ()->text ().isEmpty () );
	QCOMPARE ( fixture.last_posted (), QStringLiteral ( "Copied 1 JSON Pointer" ) );
}

void TestJsonPathView::the_copied_text_is_what_go_to_accepts ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QVERIFY ( fixture.view.copy_results () );

	// THE LOOP CLOSED, which is the whole claim: every line is fed straight back through the parser FIND-04 uses and
	// must resolve against the document it came from. A separator that had to be stripped, or a display label in place
	// of a pointer, fails here rather than in a user's hands.

	const QStringList lines = QGuiApplication::clipboard ()->text ().split ( QChar ( '\n' ) );

	QCOMPARE ( lines.count (), 3 );

	for ( const QString& line : lines )
	{
		bool ok = false;

		const JsonPointer pointer = JsonPointer::parse ( line, &ok );

		QVERIFY2 ( ok, qPrintable ( line ) );
		QVERIFY2 ( fixture.document.resolve ( pointer ) != nullptr, qPrintable ( line ) );
	}
}

void TestJsonPathView::copying_writes_plain_text_and_never_the_node_format ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$.profile" ) );

	QVERIFY ( fixture.view.copy_results () );

	// FIND-05's rule, third caller. Writing the private application/x-vje-json format alongside the text would make
	// the very next Ctrl+V paste a NODE -- here, the whole profile object -- where a list of paths was asked for.

	const QMimeData* const contents = QGuiApplication::clipboard ()->mimeData ();

	QVERIFY ( contents != nullptr );
	QVERIFY ( !contents->hasFormat ( QStringLiteral ( "application/x-vje-json" ) ) );
	QVERIFY ( !contents->hasFormat ( QStringLiteral ( "application/x-vje-json-key" ) ) );
	QVERIFY ( contents->hasText () );
}

void TestJsonPathView::copying_refreshes_a_stale_set_first ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QCOMPARE ( fixture.view.results_as_pointer_text ().count ( QChar ( '\n' ) ) + 1, 3 );

	// A third project, so the RE-RUN's answer differs from the snapshot by a line rather than by a renumbering that
	// clamping would satisfy anyway (lesson D10 -- the shape the Phase 11 re-anchoring case first failed to have).

	fixture.undo.append_element
	(
		JsonPointer::parse ( QStringLiteral ( "/projects" ) ),
		parse ( R"({ "name": "Third", "priority": 3 })" ),
		QStringLiteral ( "Add Project" )
	);

	QVERIFY ( fixture.view.results_are_stale () );

	// Copying is one of QUERY-07's questions, so what lands on the clipboard is the answer for the document as it
	// stands rather than the one the pane happened to be showing.

	QVERIFY  ( fixture.view.copy_results () );
	QVERIFY  ( !fixture.view.results_are_stale () );
	QCOMPARE ( QGuiApplication::clipboard ()->text ().split ( QChar ( '\n' ) ).count (), 4 );
	QVERIFY  ( QGuiApplication::clipboard ()->text ().contains ( QStringLiteral ( "/projects/2/name" ) ) );
}

void TestJsonPathView::copying_refuses_when_there_is_nothing_to_copy ()
{
	Fixture fixture;

	// Never run at all.

	QVERIFY ( !fixture.view.has_results () );
	QVERIFY ( !fixture.view.copy_results () );
	QVERIFY ( QGuiApplication::clipboard ()->text ().isEmpty () );

	// Ran and matched nothing, which is a different state and the same answer -- there is still nothing to copy.

	fixture.run ( QStringLiteral ( "$.nothing" ) );

	QVERIFY ( !fixture.view.has_results () );
	QVERIFY ( !fixture.view.copy_results () );
	QVERIFY ( QGuiApplication::clipboard ()->text ().isEmpty () );

	// A REFUSAL LEAVES THE CLIPBOARD ALONE rather than clearing it: the user's previous copy is theirs, and a command
	// that could not run has no business destroying it.

	QGuiApplication::clipboard ()->setText ( QStringLiteral ( "someone else's text" ) );

	QVERIFY  ( !fixture.view.copy_results () );
	QCOMPARE ( QGuiApplication::clipboard ()->text (), QStringLiteral ( "someone else's text" ) );
}

void TestJsonPathView::has_results_follows_the_list_rather_than_the_run ()
{
	Fixture fixture;

	QVERIFY ( !fixture.view.has_results () );

	fixture.run ( QStringLiteral ( "$..name" ) );

	QVERIFY ( fixture.view.has_results () );

	// A query that RAN and matched nothing reports differently from one never run (report() distinguishes them), and
	// this question deliberately does not: neither has anything to copy.

	fixture.run ( QStringLiteral ( "$.nothing" ) );

	QVERIFY  ( !fixture.view.has_results () );
	QCOMPARE ( fixture.view.report (), QStringLiteral ( "No results" ) );

	// A reset drops the results outright -- the previous document's pointers name nothing (QUERY-07).

	fixture.run ( QStringLiteral ( "$..name" ) );

	QVERIFY ( fixture.view.has_results () );

	fixture.document.set_root ( parse ( "{\"other\":1}" ) );

	QVERIFY ( !fixture.view.has_results () );
}

void TestJsonPathView::every_change_to_the_result_set_is_announced ()
{
	Fixture fixture;

	QVERIFY ( fixture.announced.isEmpty () );

	// A run.

	fixture.run ( QStringLiteral ( "$..name" ) );

	QCOMPARE ( fixture.announced.count (), 1 );
	QCOMPARE ( fixture.announced.last (), true );

	// A STALE MARK IS NOT A CHANGE to the set: the list is untouched and there is still exactly as much to copy, so
	// announcing here would make the enablement recompute on every keystroke of an editing session (NFR-03).

	fixture.undo.set_number ( JsonPointer::parse ( QStringLiteral ( "/id" ) ), QStringLiteral ( "7" ) );

	QVERIFY  ( fixture.view.results_are_stale () );
	QCOMPARE ( fixture.announced.count (), 1 );

	// The stale refresh IS -- the pointers may have moved even where the count did not.

	fixture.view.view_activated ();

	QCOMPARE ( fixture.announced.count (), 2 );

	// An emptied query, and a document reset. Both leave nothing to copy and both must say so, or Edit > Copy
	// JSONPath Result stays enabled over a list that is gone.

	fixture.run ( QString () );

	QCOMPARE ( fixture.announced.count (), 3 );
	QCOMPARE ( fixture.announced.last (), false );

	fixture.run ( QStringLiteral ( "$..name" ) );
	fixture.document.set_root ( parse ( "{\"other\":1}" ) );

	QCOMPARE ( fixture.announced.count (), 5 );
	QCOMPARE ( fixture.announced.last (), false );
}

void TestJsonPathView::the_provider_hands_the_hook_to_every_view_it_builds ()
{
	JsonDocument document;

	document.set_root ( parse ( SAMPLE_DOCUMENT ) );

	JsonPathViewProvider provider ( &document, nullptr, nullptr, nullptr, nullptr );

	int calls = 0;

	provider.set_results_changed_callback ( [ &calls ] () { ++calls; } );

	// EditorPane destroys and rebuilds a view with its tab (VIEW-01), so the hook has to travel through the PROVIDER
	// -- a window that connected to one view would lose the connection the first time the tab was closed.

	std::unique_ptr<QWidget> parent ( new QWidget () );

	IEditorView* const first = provider.create_view ( parent.get () );

	JsonPathView* const firstView = qobject_cast<JsonPathView*> ( first->widget () );

	QVERIFY ( firstView != nullptr );

	firstView->set_query_text ( QStringLiteral ( "$..name" ) );
	firstView->run_query ();

	QCOMPARE ( calls, 1 );

	// The SECOND view built by the same provider, which is what a closed-and-reopened tab is.

	IEditorView* const second = provider.create_view ( parent.get () );

	JsonPathView* const secondView = qobject_cast<JsonPathView*> ( second->widget () );

	secondView->set_query_text ( QStringLiteral ( "$..name" ) );
	secondView->run_query ();

	QCOMPARE ( calls, 2 );
}

//=======================================================================================================================
// The results context menu (QUERY-09)
//=======================================================================================================================

void TestJsonPathView::the_row_commands_appear_only_over_a_row ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	// Built rather than shown: QMenu::exec blocks and cannot be driven offscreen, so the menu's SHAPE is assertable
	// only because populating it is separable from showing it (TreeViewPane's context menu is split the same way).

	QMenu overRow;

	fixture.view.populate_results_menu ( overRow, fixture.view.results_view ()->topLevelItem ( 1 ) );

	QStringList overRowLabels;

	for ( const QAction* const action : overRow.actions () )
	{
		overRowLabels << ( action->isSeparator () ? QStringLiteral ( "---" ) : action->text () );
	}

	// THE ROW COMMANDS FIRST, above a separator: they act on the row the menu was opened on, while the command below
	// acts on the whole list. Two subjects, narrower one first.

	const QStringList expected
	{
		QStringLiteral ( "&Go to Node" ),
		QStringLiteral ( "&Copy JSON Pointer" ),
		QStringLiteral ( "---" ),
		QStringLiteral ( "Copy JSONPath &Result" )
	};

	QCOMPARE ( overRowLabels, expected );

	// Over EMPTY SPACE there is no row to act on, so the two row commands are absent rather than present and dead --
	// a command with no subject at all is a different thing from one whose subject is momentarily unusable.

	QMenu overSpace;

	fixture.view.populate_results_menu ( overSpace, nullptr );

	QStringList overSpaceLabels;

	for ( const QAction* const action : overSpace.actions () )
	{
		overSpaceLabels << ( action->isSeparator () ? QStringLiteral ( "---" ) : action->text () );
	}

	QCOMPARE ( overSpaceLabels, QStringList { QStringLiteral ( "Copy JSONPath &Result" ) } );
}

void TestJsonPathView::the_whole_set_command_is_disabled_rather_than_absent ()
{
	Fixture fixture;

	// No results at all: the command is still THERE (disabled-not-hidden, Phase 9), because a menu whose contents
	// change with state cannot be learned.

	QMenu empty;

	fixture.view.populate_results_menu ( empty, nullptr );

	QCOMPARE ( empty.actions ().count (), 1 );
	QCOMPARE ( empty.actions ().first ()->text (), QStringLiteral ( "Copy JSONPath &Result" ) );
	QVERIFY  ( !empty.actions ().first ()->isEnabled () );

	// With results it is enabled -- so the assertion above is reading a state this view set rather than a menu action
	// that is never enabled at all.

	fixture.run ( QStringLiteral ( "$..name" ) );

	QMenu filled;

	fixture.view.populate_results_menu ( filled, nullptr );

	QVERIFY ( filled.actions ().first ()->isEnabled () );
}

void TestJsonPathView::a_row_command_goes_to_the_node_that_row_names ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QSignalSpy published ( &fixture.selection, &SelectionService::selection_changed );

	QMenu menu;

	fixture.view.populate_results_menu ( menu, fixture.view.results_view ()->topLevelItem ( 2 ) );

	menu.actions ().first ()->trigger ();

	QCOMPARE ( published.count (), 1 );
	QCOMPARE ( published.first ().at ( 0 ).value<JsonPointer> ().to_string (), QStringLiteral ( "/projects/1/name" ) );

	// GoTo, the same channel Edit > Go To publishes on -- which already reveals into a collapsed branch. The menu item
	// and a click on the row are one command, not two implementations of it.

	QCOMPARE ( published.first ().at ( 1 ).value<SelectionOrigin> (), SelectionOrigin::GoTo );
}

void TestJsonPathView::a_row_command_copies_that_row_s_pointer_alone ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$..name" ) );

	QMenu menu;

	fixture.view.populate_results_menu ( menu, fixture.view.results_view ()->topLevelItem ( 1 ) );

	menu.actions ().at ( 1 )->trigger ();

	// THAT ROW'S pointer and nothing else -- the whole point of the row commands being separate from the one below.

	QCOMPARE ( QGuiApplication::clipboard ()->text (), QStringLiteral ( "/projects/0/name" ) );

	// FindController's own words for the same outcome (FIND-05 reached from a result row instead of from the tree
	// selection), deliberately rather than a second spelling of it.

	QCOMPARE ( fixture.last_posted (), QStringLiteral ( "Copied /projects/0/name" ) );

	// The root's pointer is the empty string, so that one success genuinely leaves the clipboard empty and has to say
	// so in its own words rather than as "Copied ".

	QVERIFY  ( fixture.view.copy_result_pointer ( JsonPointer () ) );
	QVERIFY  ( QGuiApplication::clipboard ()->text ().isEmpty () );
	QCOMPARE ( fixture.last_posted (), QStringLiteral ( "Copied the document root pointer (empty)" ) );
}

void TestJsonPathView::a_row_naming_a_node_the_document_lost_reports_and_changes_nothing ()
{
	Fixture fixture;

	fixture.run ( QStringLiteral ( "$.projects[*]" ) );

	QCOMPARE ( fixture.view.results_view ()->topLevelItemCount (), 2 );

	// Both projects removed, so /projects/1 names nothing at all -- FIND-04's Unresolvable reached from the other
	// direction, a pointer taken before an edit that removed what it named.

	fixture.undo.delete_node ( JsonPointer::parse ( QStringLiteral ( "/projects/1" ) ) );
	fixture.undo.delete_node ( JsonPointer::parse ( QStringLiteral ( "/projects/0" ) ) );

	QSignalSpy published ( &fixture.selection, &SelectionService::selection_changed );

	QVERIFY  ( !fixture.view.go_to_result ( JsonPointer::parse ( QStringLiteral ( "/projects/1" ) ) ) );
	QCOMPARE ( published.count (), 0 );
	QCOMPARE ( fixture.last_posted (), QStringLiteral ( "That node is no longer in the document." ) );
}

QTEST_MAIN ( TestJsonPathView )

#include "tst_json_path_view.moc"
