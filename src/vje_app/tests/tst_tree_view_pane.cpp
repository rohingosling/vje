//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for TreeViewPane -- the behaviour that lives in the VIEW rather than the model,
//   and so cannot be reached by tst_json_tree_model:
//
//     - View-state preservation across a projection rebuild (TREE-07, NAV-03). The model can only announce a rebuild;
//       it is the pane that saves expansion and selection by JSON Pointer and puts back whatever still resolves. That
//       promise is the whole reason a Code View commit does not dump the user back at the root, and it was previously
//       asserted only by eye.
//     - The reveal-intent rule (EDITOR-04): an explicit navigation expands to show its target, a form-field
//       write-back does not.
//     - The selection bridge in both directions, including its guard against feeding back on itself.
//
//   Runs under the offscreen QPA platform. QTreeView tracks expansion without ever being shown, so these cases need no
//   display -- which is what makes them CI-safe on both platforms rather than manual smoke.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonTreeModel.hpp"
#include "services/SelectionService.hpp"
#include "views/PaneHeader.hpp"
#include "views/TreeViewPane.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QSignalSpy>
#include <QtTest/QtTest>

#include <QLayout>
#include <QTreeView>

#include <memory>

using namespace vje;

namespace
{
	// A document with two independent expandable branches, so a change to one can be shown NOT to disturb the other.

	const char* const SAMPLE_DOCUMENT = R"({
		"keep": { "inner": { "leaf": 1 } },
		"transform": { "first": { "x": 1 }, "second": { "y": 2 } },
		"items": [ { "a": 1 }, { "b": 2 } ]
	})";

	JsonPointer pointer ( const QString& text )
	{
		return JsonPointer::parse ( text );
	}
}

class TestTreeViewPane : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	// Selection bridge.

	void tree_selection_writes_the_service ();
	void service_selection_moves_the_tree ();
	void revealing_origin_expands_to_the_target ();
	void form_field_origin_does_not_expand ();
	void keyboard_navigation_publishes_no_gesture ();
	void form_field_origin_inside_a_collapsed_branch_leaves_the_highlight_put ();
	void form_field_origin_moves_the_highlight_when_the_row_is_on_show ();

	// View-state preservation (TREE-07, NAV-03).

	void root_commit_preserves_surviving_expansion ();
	void root_commit_drops_expansion_that_no_longer_resolves ();
	void non_root_replacement_preserves_what_still_resolves ();
	void selection_falls_back_to_the_nearest_surviving_ancestor ();

	// The pane's own chrome (STYLE-13).

	void the_pane_is_titled_above_the_tree ();
	void clicking_the_band_is_a_focus_move_and_nothing_else ();

private:

	std::unique_ptr<JsonNode> parse ( const char* text ) const;
	void                      load  ( const char* text );

	bool        expanded ( const QString& path ) const;
	void        expand   ( const QString& path );
	QModelIndex index_of ( const QString& path ) const;

	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<TreeViewPane>     pane;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestTreeViewPane::init ()
{
	document  = std::make_unique<JsonDocument> ();
	undo      = std::make_unique<UndoController> ( document.get () );
	selection = std::make_unique<SelectionService> ();
	pane      = std::make_unique<TreeViewPane> ( document.get (), selection.get (), nullptr );
}

void TestTreeViewPane::cleanup ()
{
	// Strict reverse dependency order -- UndoController's destructor writes through the
	// document, so the document must outlive it.

	pane.reset ();
	selection.reset ();
	undo.reset ();
	document.reset ();
}

std::unique_ptr<JsonNode> TestTreeViewPane::parse ( const char* text ) const
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	return std::move ( result.root );
}

void TestTreeViewPane::load ( const char* text )
{
	document->set_root ( parse ( text ) );
}

QModelIndex TestTreeViewPane::index_of ( const QString& path ) const
{
	return pane->model ()->index_for_pointer ( pointer ( path ) );
}

bool TestTreeViewPane::expanded ( const QString& path ) const
{
	const QModelIndex index = index_of ( path );

	return index.isValid () && pane->view ()->isExpanded ( index );
}

void TestTreeViewPane::expand ( const QString& path )
{
	const QModelIndex index = index_of ( path );

	QVERIFY2 ( index.isValid (), qPrintable ( path ) );

	pane->view ()->setExpanded ( index, true );
}

//---------------------------------------------------------------------------------------------------------------------
// Selection bridge
//---------------------------------------------------------------------------------------------------------------------

void TestTreeViewPane::tree_selection_writes_the_service ()
{
	// NAV-01, outbound: the tree is the master and the service is where everything else reads the selection from.

	load ( SAMPLE_DOCUMENT );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/keep" ) ) );

	QVERIFY  ( selection->has_selection () );
	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/keep" ) );
	QCOMPARE ( selection->origin (), SelectionOrigin::Tree );
}

void TestTreeViewPane::service_selection_moves_the_tree ()
{
	// Inbound, and crucially WITHOUT bouncing back: the pane must not rewrite the service with origin Tree in response
	// to a selection the service itself just announced.

	load ( SAMPLE_DOCUMENT );

	selection->set_selection ( pointer ( QStringLiteral ( "/items/1" ) ), SelectionOrigin::GoTo );

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/items/1" ) );

	// The origin survived the round trip, which it would not have had the pane written its own.

	QCOMPARE ( selection->origin (), SelectionOrigin::GoTo );
}

void TestTreeViewPane::revealing_origin_expands_to_the_target ()
{
	// EDITOR-04: Go To / Find / paste / drill-in expand whatever it takes to show the node.

	load ( SAMPLE_DOCUMENT );

	QVERIFY ( !expanded ( QStringLiteral ( "/keep" ) ) );

	selection->set_selection ( pointer ( QStringLiteral ( "/keep/inner/leaf" ) ), SelectionOrigin::GoTo );

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/keep/inner/leaf" ) );
}

void TestTreeViewPane::form_field_origin_does_not_expand ()
{
	// The other half of EDITOR-04, and the one that matters for feel: a form-field write-back must leave a collapsed
	// branch collapsed, or the tree jumps around while the user is typing.

	load ( SAMPLE_DOCUMENT );

	QVERIFY ( !expanded ( QStringLiteral ( "/keep" ) ) );

	selection->set_selection ( pointer ( QStringLiteral ( "/keep/inner" ) ), SelectionOrigin::FormField );

	QVERIFY ( !expanded ( QStringLiteral ( "/keep" ) ) );
}

void TestTreeViewPane::form_field_origin_inside_a_collapsed_branch_leaves_the_highlight_put ()
{
	// The regression behind the arrow-key bug. Asserting only "the branch did not expand" is not enough, because the
	// expansion was not something the PANE did -- it was Qt's. Setting the current index to a row inside a collapsed
	// branch makes QAbstractItemView::currentChanged auto-scroll to it, and QTreeView::scrollTo expands every collapsed
	// ancestor on the way. A never-shown test widget skips that auto-scroll entirely (it is guarded by isVisible), so
	// the old assertion passed here while the running application expanded the branch under the user's Down arrow.
	//
	// So assert the thing the pane actually controls: the current index does not move to a row that is not on show.
	// That holds whether or not the widget is visible, which is exactly why it is the right assertion.

	load ( SAMPLE_DOCUMENT );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/keep" ) ) );

	QVERIFY ( !expanded ( QStringLiteral ( "/keep" ) ) );

	selection->set_selection ( pointer ( QStringLiteral ( "/keep/inner" ) ), SelectionOrigin::FormField );

	QVERIFY ( !expanded ( QStringLiteral ( "/keep" ) ) );

	// The highlight stayed on the node the user is standing on, so the next Up / Down still walks the visible rows.

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/keep" ) );

	// ...and the selection service still tracks the field, which is what the status bar reads (EDITOR-04).

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/keep/inner" ) );
}

void TestTreeViewPane::keyboard_navigation_publishes_no_gesture ()
{
	// The gesture / selection split (EDITOR-04), asserted at its source. Moving the current row -- which is all a Down
	// arrow does -- announces a SELECTION and nothing else. If it also published a gesture, the editor pane would take
	// the caret out of the tree on the first scalar the user arrowed past.

	load ( SAMPLE_DOCUMENT );

	QSignalSpy clickSpy     ( pane.get (), &TreeViewPane::node_clicked );
	QSignalSpy activateSpy  ( pane.get (), &TreeViewPane::node_activated );
	QSignalSpy selectionSpy ( selection.get (), &SelectionService::selection_changed );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/keep" ) ) );

	QCOMPARE ( selectionSpy.count (), 1 );
	QCOMPARE ( clickSpy    .count (), 0 );
	QCOMPARE ( activateSpy .count (), 0 );
}

void TestTreeViewPane::form_field_origin_moves_the_highlight_when_the_row_is_on_show ()
{
	// The complementary half: "only when already visible" is a real condition, not a way of saying "never". Once the
	// branch is open, the write-back does move the highlight -- it just never opens the branch itself.

	load ( SAMPLE_DOCUMENT );

	expand ( QString () );
	expand ( QStringLiteral ( "/keep" ) );

	selection->set_selection ( pointer ( QStringLiteral ( "/keep/inner" ) ), SelectionOrigin::FormField );

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/keep/inner" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// View-state preservation (TREE-07, NAV-03)
//---------------------------------------------------------------------------------------------------------------------

void TestTreeViewPane::root_commit_preserves_surviving_expansion ()
{
	// The TREE-07 promise, stated plainly: a Code View commit replaces the whole document, but the user keeps their
	// position. Everything whose pointer still resolves is still open afterwards.

	load ( SAMPLE_DOCUMENT );

	expand ( QString () );
	expand ( QStringLiteral ( "/keep" ) );
	expand ( QStringLiteral ( "/keep/inner" ) );
	expand ( QStringLiteral ( "/transform" ) );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/keep/inner/leaf" ) ) );

	// A commit that keeps those paths and adds one.

	QCOMPARE
	(
		undo->replace_subtree
		(
			JsonPointer (),
			parse ( R"({ "keep": { "inner": { "leaf": 99 } }, "transform": { "first": { "x": 1 } }, "added": true })" ),
			QStringLiteral ( "Commit" )
		),
		EditOutcome::Applied
	);

	QVERIFY2 ( expanded ( QStringLiteral ( "/keep" ) ),       "the commit collapsed a surviving branch" );
	QVERIFY2 ( expanded ( QStringLiteral ( "/keep/inner" ) ), "the commit collapsed a surviving nested branch" );
	QVERIFY2 ( expanded ( QStringLiteral ( "/transform" ) ),  "the commit collapsed a surviving sibling" );

	// And the selection stayed put rather than resetting to the root (NAV-03).

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/keep/inner/leaf" ) );
}

void TestTreeViewPane::root_commit_drops_expansion_that_no_longer_resolves ()
{
	// The complement: a branch the commit deleted cannot be restored, and must not resurrect anything.

	load ( SAMPLE_DOCUMENT );

	expand ( QString () );
	expand ( QStringLiteral ( "/transform" ) );
	expand ( QStringLiteral ( "/transform/first" ) );

	QCOMPARE
	(
		undo->replace_subtree
		(
			JsonPointer (),
			parse ( R"({ "keep": { "inner": {} } })" ),
			QStringLiteral ( "Commit" )
		),
		EditOutcome::Applied
	);

	QVERIFY ( !index_of ( QStringLiteral ( "/transform" ) ).isValid () );
	QVERIFY ( expanded ( QString () ) );
}

void TestTreeViewPane::non_root_replacement_preserves_what_still_resolves ()
{
	// A NON-root replacement (here EDIT-11, normalize array elements) runs through the same capture/restore pair as a
	// commit. The replaced node's interior is NOT unconditionally collapsed: element pointers that still resolve after
	// the transform are re-expanded, and only paths the transform actually removed are lost.

	load ( SAMPLE_DOCUMENT );

	expand ( QString () );
	expand ( QStringLiteral ( "/items" ) );
	expand ( QStringLiteral ( "/items/0" ) );
	expand ( QStringLiteral ( "/keep" ) );

	QCOMPARE ( undo->normalize_array ( pointer ( QStringLiteral ( "/items" ) ) ), EditOutcome::Applied );

	// The replaced node itself, and the element inside it, are both still open.

	QVERIFY2 ( expanded ( QStringLiteral ( "/items" ) ),   "the replaced node was left collapsed" );
	QVERIFY2 ( expanded ( QStringLiteral ( "/items/0" ) ), "an element inside the replaced node was left collapsed" );

	// An untouched branch elsewhere is undisturbed.

	QVERIFY ( expanded ( QStringLiteral ( "/keep" ) ) );
}

void TestTreeViewPane::selection_falls_back_to_the_nearest_surviving_ancestor ()
{
	// NAV-03's fallback rule, end to end: the selected node is deleted by a commit, so the selection lands on the
	// deepest ancestor that survived -- not at the root.

	load ( SAMPLE_DOCUMENT );

	expand ( QString () );
	expand ( QStringLiteral ( "/transform" ) );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/transform/first/x" ) ) );

	QCOMPARE
	(
		undo->replace_subtree
		(
			JsonPointer (),
			parse ( R"({ "transform": { "second": { "y": 2 } } })" ),
			QStringLiteral ( "Commit" )
		),
		EditOutcome::Applied
	);

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/transform" ) );

	// The rest of the application is told where it actually landed, and told in a way that does not re-expand.

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/transform" ) );
	QCOMPARE ( selection->origin (), SelectionOrigin::Programmatic );
}

//=====================================================================================================================
// The pane's own chrome (STYLE-13)
//=====================================================================================================================

void TestTreeViewPane::the_pane_is_titled_above_the_tree ()
{
	// Ordering is the assertion worth making: a header is only a header if it is ABOVE the thing it names, and a layout
	// index is the one part of that nothing else in the suite would notice going wrong.

	PaneHeader* const header = pane->header ();

	QVERIFY ( header != nullptr );

	QCOMPARE ( header->title (), QStringLiteral ( "Explorer" ) );

	QLayout* const paneLayout = pane->layout ();

	QVERIFY ( paneLayout != nullptr );

	QCOMPARE ( paneLayout->indexOf ( header ),       0 );
	QCOMPARE ( paneLayout->indexOf ( pane->view () ), 1 );
}

QTEST_MAIN ( TestTreeViewPane )

#include "tst_tree_view_pane.moc"

//---------------------------------------------------------------------------------------------------------------------
// The Explorer band (STYLE-13)
//---------------------------------------------------------------------------------------------------------------------

// Clicking the band hands the keyboard to the TREE (PaneHeader::clicked -> TreeViewPane::take_focus). That the focus
// lands there cannot be asserted offscreen -- the platform grants focus to nothing -- so what is
// pinned here is the other half of the claim, which is assertable and is the half that would break silently: the click
// is a focus move and NOTHING else. It must not select, expand, collapse, or publish a selection.

void TestTreeViewPane::clicking_the_band_is_a_focus_move_and_nothing_else ()
{
	load ( R"({ "keep": { "inner": 1 }, "other": 2 })" );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/other" ) ) );

	const QModelIndex currentBefore  = pane->view ()->currentIndex ();
	const bool        expandedBefore = expanded ( QStringLiteral ( "/keep" ) );

	QSignalSpy selections ( selection.get (), &SelectionService::selection_changed );

	PaneHeader* const band = pane->header ();

	band->resize ( 200, band->sizeHint ().height () );

	QTest::mouseClick ( band, Qt::LeftButton, Qt::NoModifier, QPoint ( 40, band->height () / 2 ) );

    QCOMPARE ( pane->view ()->currentIndex (), currentBefore );
	QCOMPARE ( expanded ( QStringLiteral ( "/keep" ) ), expandedBefore );
	QCOMPARE ( selections.count (), 0 );
}
