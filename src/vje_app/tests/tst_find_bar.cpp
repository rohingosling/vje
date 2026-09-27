//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for FindBar -- the docked find bar (FIND-01..03). What Find MEANS is pinned in tst_find_controller; what
//   is pinned here is the wiring, which is the only thing the bar owns: which gesture reaches which controller call,
//   and that the label is the controller's own report rather than a second opinion computed in the widget.
//
//   What is pinned:
//
//     - Typing runs the search live, and the label shows FindController::report() verbatim.
//     - Match Case re-runs the SAME query rather than starting a new one -- the field and the box are one query in two
//       widgets.
//     - The previous / next buttons step the controller, and are disabled with nothing to step through.
//     - Return steps forward and Shift+Return backward. That distinction is the reason the bar filters the field's key
//       events instead of using QLineEdit::returnPressed(), which cannot report the modifier.
//     - Esc dismisses (from the field and from the bar), hides, and announces it -- and the query SURVIVES, which is
//       what keeps F3 repeating with the bar out of the way.
//     - Re-opening restores the label from the controller, so a document that changed while the bar was hidden is
//       reported correctly rather than showing a stale count.
//
//   Offscreen, and it stays away from anything the offscreen platform silences: nothing here asserts keyboard focus
//   (lessons-learned Q10), and the key events are delivered to the widget directly rather than through the platform.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/FindBar.hpp"

#include "controllers/FindController.hpp"
#include "services/SelectionService.hpp"
#include "services/StatusService.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QToolButton>

#include <memory>

using namespace vje;

namespace
{
	const char* const FIXTURE_JSON = R"({
		"name": "Alpha",
		"nested": { "name": "alpha" },
		"items": [ "Alpha", "gamma" ]
	})";
}

class TestFindBar : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void typing_runs_the_search_and_the_label_is_the_controllers_report ();
	void match_case_re_runs_the_same_query ();
	void the_step_buttons_move_the_selection_and_disable_with_nothing_to_step ();
	void return_steps_forward_and_shift_return_steps_back ();
	void escape_dismisses_from_the_field_and_keeps_the_query ();
	void the_close_button_and_dismiss_announce_themselves_once ();
	void re_opening_re_reads_the_report ();
	void every_control_carries_an_accessible_name ();

private:

	void load ( const char* text );

	QString selection_text () const;

	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<StatusService>    status;
	std::unique_ptr<FindController>   controller;
	std::unique_ptr<FindBar>          bar;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestFindBar::init ()
{
	document   = std::make_unique<JsonDocument> ();
	selection  = std::make_unique<SelectionService> ();
	status     = std::make_unique<StatusService> ();
	controller = std::make_unique<FindController> ( document.get (), selection.get (), status.get () );

	// No icon library: the buttons fall back to their text, which is the form the header documents.

	bar = std::make_unique<FindBar> ( controller.get (), nullptr );

	load ( FIXTURE_JSON );
}

void TestFindBar::cleanup ()
{
	// Strict reverse construction order (lessons-learned Q1).

	bar.reset ();
	controller.reset ();
	status.reset ();
	selection.reset ();
	document.reset ();
}

void TestFindBar::load ( const char* text )
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	QVERIFY2 ( result.ok, "the fixture JSON must parse" );

	document->set_root ( std::move ( result.root ) );
}

QString TestFindBar::selection_text () const
{
	return selection->has_selection () ? selection->selection ().to_string () : QStringLiteral ( "<none>" );
}

//---------------------------------------------------------------------------------------------------------------------
// Cases
//---------------------------------------------------------------------------------------------------------------------

void TestFindBar::typing_runs_the_search_and_the_label_is_the_controllers_report ()
{
	bar->open ();

	QVERIFY ( !bar->isHidden () );

	bar->query_field ()->setText ( QStringLiteral ( "Alpha" ) );

	QCOMPARE ( controller->query (), QStringLiteral ( "Alpha" ) );
	QCOMPARE ( controller->match_count (), 3 );

	// The label is the controller's own string, not a count formatted a second time in the widget -- otherwise the bar
	// and the status bar could disagree about the same search (FIND-02 asks for both).

	QCOMPARE ( bar->count_label ()->text (), controller->report () );
	QCOMPARE ( bar->count_label ()->text (), QStringLiteral ( "1 of 3 matches" ) );

	// FIND-03's message reaches the same label.

	bar->query_field ()->setText ( QStringLiteral ( "nothing here" ) );

	QCOMPARE ( bar->count_label ()->text (), QStringLiteral ( "No matches" ) );
}

void TestFindBar::match_case_re_runs_the_same_query ()
{
	bar->open ();

	bar->query_field ()->setText ( QStringLiteral ( "alpha" ) );

	QCOMPARE ( controller->match_count (), 3 );

	// The box and the field are ONE query: ticking Match Case with text already typed narrows the existing search
	// rather than starting a new one with an empty needle.

	bar->match_case_box ()->setChecked ( true );

	QVERIFY ( controller->match_case () );
	QCOMPARE ( controller->query (), QStringLiteral ( "alpha" ) );
	QCOMPARE ( controller->match_count (), 1 );
	QCOMPARE ( bar->count_label ()->text (), QStringLiteral ( "1 of 1 match" ) );
}

void TestFindBar::the_step_buttons_move_the_selection_and_disable_with_nothing_to_step ()
{
	bar->open ();

	QList<QToolButton*> buttons = bar->findChildren<QToolButton*> ();

	QToolButton* previousButton = nullptr;
	QToolButton* nextButton     = nullptr;

	for ( QToolButton* const button : buttons )
	{
		if ( button->toolTip ().startsWith ( QStringLiteral ( "Previous" ) ) ) { previousButton = button; }
		if ( button->toolTip ().startsWith ( QStringLiteral ( "Next" ) ) )     { nextButton     = button; }
	}

	QVERIFY ( previousButton != nullptr );
	QVERIFY ( nextButton != nullptr );

	// Nothing to step through: disabled, not live-and-inert (the disabled-not-hidden rule, section 2.3).

	QVERIFY ( !previousButton->isEnabled () );
	QVERIFY ( !nextButton->isEnabled () );

	bar->query_field ()->setText ( QStringLiteral ( "Alpha" ) );

	QVERIFY ( previousButton->isEnabled () );
	QVERIFY ( nextButton->isEnabled () );
	QCOMPARE ( selection_text (), QStringLiteral ( "/name" ) );

	nextButton->click ();

	QCOMPARE ( selection_text (), QStringLiteral ( "/nested/name" ) );

	previousButton->click ();

	QCOMPARE ( selection_text (), QStringLiteral ( "/name" ) );
}

void TestFindBar::return_steps_forward_and_shift_return_steps_back ()
{
	bar->open ();

	bar->query_field ()->setText ( QStringLiteral ( "Alpha" ) );

	QCOMPARE ( controller->current_position (), 1 );

	QTest::keyClick ( bar->query_field (), Qt::Key_Return );

	QCOMPARE ( controller->current_position (), 2 );

	// The modifier is the whole reason the bar filters the field's key events: QLineEdit::returnPressed() reports that
	// Return arrived and not what came with it, so this direction would be unreachable through the signal.

	QTest::keyClick ( bar->query_field (), Qt::Key_Return, Qt::ShiftModifier );

	QCOMPARE ( controller->current_position (), 1 );
}

void TestFindBar::escape_dismisses_from_the_field_and_keeps_the_query ()
{
	bar->open ();

	bar->query_field ()->setText ( QStringLiteral ( "Alpha" ) );

	QSignalSpy dismissedSpy ( bar.get (), &FindBar::dismissed );

	QTest::keyClick ( bar->query_field (), Qt::Key_Escape );

	QVERIFY ( bar->isHidden () );
	QCOMPARE ( dismissedSpy.count (), 1 );

	// Dismissed is not cleared: F3 has to keep repeating the last search with the bar out of the way.

	QCOMPARE ( controller->query (), QStringLiteral ( "Alpha" ) );
	QCOMPARE ( controller->match_count (), 3 );
}

void TestFindBar::the_close_button_and_dismiss_announce_themselves_once ()
{
	bar->open ();

	QSignalSpy dismissedSpy ( bar.get (), &FindBar::dismissed );

	bar->dismiss ();

	QCOMPARE ( dismissedSpy.count (), 1 );

	// A second dismissal of an already-hidden bar is not an event. Without the guard the window would be handed the
	// keyboard again on every stray Esc, dragging focus out of whatever the user had moved on to.

	bar->dismiss ();

	QCOMPARE ( dismissedSpy.count (), 1 );
}

void TestFindBar::re_opening_re_reads_the_report ()
{
	bar->open ();

	bar->query_field ()->setText ( QStringLiteral ( "Alpha" ) );
	QCOMPARE ( bar->count_label ()->text (), QStringLiteral ( "1 of 3 matches" ) );

	bar->dismiss ();

	// The document moves while the bar is hidden. Nothing asks the controller anything in the meantime, which is the
	// laziness the design is built on -- so the label is only correct again because open() re-reads it.

	JsonNode* const items = document->resolve ( JsonPointer::parse ( QStringLiteral ( "/items" ) ) );

	QVERIFY ( items != nullptr );

	items->take_element ( 0 );

	document->notify_node_changed ( JsonPointer::parse ( QStringLiteral ( "/items" ) ), DocumentChange::NodeRemoved );

	bar->open ();

	QVERIFY ( !bar->isHidden () );
	QCOMPARE ( bar->count_label ()->text (), controller->report () );
	QCOMPARE ( controller->match_count (), 2 );

	// The query is still in the field, and open() selects it so the next keystroke replaces it.

	QCOMPARE ( bar->query_field ()->text (), QStringLiteral ( "Alpha" ) );
	QCOMPARE ( bar->query_field ()->selectedText (), QStringLiteral ( "Alpha" ) );
}

void TestFindBar::every_control_carries_an_accessible_name ()
{
	// NFR-05. The bar is a strip of controls with no labels to buddy, so each one has to say what it is itself -- and
	// the query field's placeholder is not that name, since it vanishes the moment the user types.

	QVERIFY2 ( !bar->query_field ()->accessibleName ().isEmpty (), "The find field has no accessible name" );
	QVERIFY2 ( !bar->count_label ()->accessibleName ().isEmpty (), "The match count has no accessible name" );

	// The buttons are named by their text, which they carry for exactly this reason (see FindBar's constructor).

	const QList<QToolButton*> buttons = bar->findChildren<QToolButton*> ();

	QVERIFY ( !buttons.isEmpty () );

	for ( const QToolButton* const button : buttons )
	{
		QVERIFY2
		(
			!button->accessibleName ().isEmpty () || !button->text ().isEmpty (),
			qPrintable ( QStringLiteral ( "A find-bar button announces nothing: %1" ).arg ( button->objectName () ) )
		);
	}
}

QTEST_MAIN ( TestFindBar )

#include "tst_find_bar.moc"
