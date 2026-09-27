//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include "services/StatusService.hpp"
#include "views/NodeContextActions.hpp"
#include "views/PaneHeader.hpp"
#include "views/TreeNodeDelegate.hpp"
#include "views/TreeViewPane.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QSignalSpy>
#include <QtTest/QtTest>

#include <QAction>
#include <QItemSelectionModel>
#include <QLayout>
#include <QMenu>
#include <QTreeView>

#include <algorithm>
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

	// For the multiple-selection cases (TREE-09): four SCALAR elements under one parent, so a range can be built with no
	// expansion in the middle of it, plus two members under a DIFFERENT parent for the cross-parent refusal.

	const char* const MULTI_SELECT_DOCUMENT = R"({
		"left": { "p": 1, "q": 2 },
		"items": [ 10, 20, 30, 40 ],
		"right": 3
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

	// Command-driven selection and the context-menu targeting rule (Phase 9 smoke corrections).

	void a_service_selection_with_tree_origin_moves_the_highlight ();
	void the_context_row_rule_brings_the_service_to_the_clicked_row ();

	// Deleting the tree's current row (the Phase 9 smoke crash and its silent sibling).

	void deleting_the_current_row_with_one_following_sibling_survives ();
	void deleting_the_current_first_member_publishes_the_true_next_key ();
	void deleting_the_current_array_element_publishes_the_survivors_true_index ();
	void deleting_the_current_first_element_of_two_publishes_a_resolvable_pointer ();
	void the_reported_smoke_sequence_add_add_delete_survives ();

	// The pane's own chrome (STYLE-13).

	void the_pane_is_titled_above_the_tree ();
	void clicking_the_band_is_a_focus_move_and_nothing_else ();

	// The expand / collapse commands (TREE-05 / TREE-06).

	void the_subtree_commands_answer_for_the_current_row ();
	void the_shared_context_menu_carries_the_node_commands_in_order ();
	void an_unwired_convert_group_leaves_no_empty_submenu ();
	void the_tree_and_its_band_carry_accessible_names ();
	void the_tree_paints_through_the_change_mark_delegate ();

	// Multiple selection (TREE-09).

	void a_single_selection_is_a_set_of_one ();
	void the_set_signal_fires_only_when_the_set_changes ();
	void ctrl_click_adds_a_sibling_to_the_set ();
	void shift_click_extends_the_set_to_a_range ();
	void the_set_comes_back_in_document_order ();
	void an_unmodified_click_collapses_the_set ();
	void the_keyboard_alone_builds_a_set ();
	void the_post_delete_landing_follows_nav_03 ();
	void a_cross_parent_extension_is_refused_and_reported ();
	void a_refused_extension_leaves_the_view_showing_the_previous_selection ();
	void an_inbound_single_selection_collapses_the_set ();
	void deleting_a_node_inside_the_selection_leaves_a_coherent_set ();

	// Drag-to-reorder (EDIT-10, Phase 15f).

	void a_drop_publishes_the_reorder_it_asks_for ();
	void a_multi_node_drop_carries_the_whole_selection ();
	void a_drop_where_the_selection_already_is_publishes_an_empty_plan ();
	void a_drop_among_another_parents_children_publishes_nothing ();

private:

	std::unique_ptr<JsonNode> parse ( const char* text ) const;
	void                      load  ( const char* text );

	bool        expanded ( const QString& path ) const;
	void        expand   ( const QString& path );
	QModelIndex index_of ( const QString& path ) const;

	// The gestures TREE-09 is built out of, driven through Qt's own translation of them rather than by writing the
	// selection flags ourselves -- so what is under test is the pane's reaction to a real Ctrl / Shift click, not our
	// own idea of what one produces.

	void click_row ( const QString& path, Qt::KeyboardModifiers modifiers = Qt::NoModifier );

	QStringList service_selection_set () const;   // What the service holds.
	QStringList view_selected_rows    () const;   // What the view is showing, in document order.

	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<StatusService>    status;
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
	status    = std::make_unique<StatusService> ();
	pane      = std::make_unique<TreeViewPane> ( document.get (), selection.get (), nullptr, status.get () );
}

void TestTreeViewPane::cleanup ()
{
	// Strict reverse dependency order -- UndoController's destructor writes through the
	// document, so the document must outlive it.

	pane.reset ();
	status.reset ();
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

void TestTreeViewPane::click_row ( const QString& path, Qt::KeyboardModifiers modifiers )
{
	QTreeView* const view = pane->view ();

	const QModelIndex index = index_of ( path );

	QVERIFY2 ( index.isValid (), qPrintable ( path ) );

	// QAbstractItemView lays its rows out lazily (a queued doItemsLayout), and nothing here spins an event loop -- so a
	// row has no visual rect to click until the layout is forced. Offscreen needs no window, only a size.

	view->resize ( 400, 600 );
	view->doItemsLayout ();

	const QRect rowRect = view->visualRect ( index );

	QVERIFY2 ( !rowRect.isEmpty (), qPrintable ( path ) );

	QTest::mouseClick ( view->viewport (), Qt::LeftButton, modifiers, rowRect.center () );
}

QStringList TestTreeViewPane::service_selection_set () const
{
	QStringList pointers;

	for ( const JsonPointer& entry : selection->selection_set () )
	{
		pointers.append ( entry.to_string () );
	}

	return pointers;
}

QStringList TestTreeViewPane::view_selected_rows () const
{
	QModelIndexList rows = pane->view ()->selectionModel ()->selectedRows ();

	std::sort
	(
		rows.begin (), rows.end (),
		[] ( const QModelIndex& left, const QModelIndex& right ) { return left.row () < right.row (); }
	);

	QStringList pointers;

	for ( const QModelIndex& row : rows )
	{
		pointers.append ( pane->model ()->pointer_for_index ( row ).to_string () );
	}

	return pointers;
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

//---------------------------------------------------------------------------------------------------------------------
// The expand / collapse commands (TREE-05 / TREE-06)
//---------------------------------------------------------------------------------------------------------------------

void TestTreeViewPane::the_subtree_commands_answer_for_the_current_row ()
{
	// Expand / Collapse Subtree act on the tree's CURRENT ROW, so their enabled state must be asked of that row rather
	// than of the selection -- the two legitimately differ, because a no-reveal selection leaves the highlight put
	// (EDITOR-04). This is the question the View menu and the context menu both put to the pane.

	QVERIFY ( !pane->current_row_is_branch () );        // No document, so no current row to expand.

	load ( SAMPLE_DOCUMENT );

	// A load leaves the file node current (TREE-01), which IS a branch -- so the pair is available immediately.

	QVERIFY ( pane->current_row_is_branch () );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/keep" ) ) );

	QVERIFY ( pane->current_row_is_branch () );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/keep/inner/leaf" ) ) );

	QVERIFY ( !pane->current_row_is_branch () );        // A scalar has no subtree to expand.

	// And the commands themselves still act on that row: expanding the subtree of a branch opens its descendants.

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/keep" ) ) );
	pane->expand_current_subtree ();

	QVERIFY ( expanded ( QStringLiteral ( "/keep" ) ) );
	QVERIFY ( expanded ( QStringLiteral ( "/keep/inner" ) ) );

	pane->collapse_current_subtree ();

	QVERIFY ( !expanded ( QStringLiteral ( "/keep" ) ) );
	QVERIFY ( !expanded ( QStringLiteral ( "/keep/inner" ) ) );
}

void TestTreeViewPane::the_shared_context_menu_carries_the_node_commands_in_order ()
{
	// The context menu itself cannot be driven -- QMenu::exec blocks on a nested event loop -- but the POPULATION is an
	// ordinary function over a QMenu, and the order is what keeps being specified by hand. So it is stated here: change
	// the order and this says so, rather than a user noticing the menu has quietly rearranged itself.
	//
	// The tree appends its own four expand / collapse commands BELOW this set (and the Form View, which shares the same
	// population, does not) -- that separation is why they are not in the list here.

	QAction cut       ( QStringLiteral ( "Cut" ) );
	QAction copy      ( QStringLiteral ( "Copy" ) );
	QAction paste     ( QStringLiteral ( "Paste" ) );
	QAction duplicate ( QStringLiteral ( "Duplicate Node" ) );
	QAction remove    ( QStringLiteral ( "Delete Node" ) );
	QAction rename    ( QStringLiteral ( "Rename Key..." ) );
	QAction moveUp    ( QStringLiteral ( "Move Up" ) );
	QAction moveDown  ( QStringLiteral ( "Move Down" ) );
	QAction toArray   ( QStringLiteral ( "Convert Objects to Array" ) );
	QAction toObjects ( QStringLiteral ( "Convert Array to Objects" ) );
	QAction normalize ( QStringLiteral ( "Normalize Array Elements" ) );

	QAction toString  ( QStringLiteral ( "String"  ) );
	QAction toNumber  ( QStringLiteral ( "Number"  ) );
	QAction toBoolean ( QStringLiteral ( "Boolean" ) );
	QAction toNull    ( QStringLiteral ( "Null"    ) );

	NodeContextActions actions;

	actions.cut            = &cut;
	actions.copy           = &copy;
	actions.paste          = &paste;
	actions.duplicate      = &duplicate;
	actions.remove         = &remove;
	actions.rename         = &rename;
	actions.moveUp         = &moveUp;
	actions.moveDown       = &moveDown;
	actions.objectsToArray = &toArray;
	actions.arrayToObjects = &toObjects;
	actions.normalizeArray = &normalize;

	actions.convertToString  = &toString;
	actions.convertToNumber  = &toNumber;
	actions.convertToBoolean = &toBoolean;
	actions.convertToNull    = &toNull;

	QMenu menu;

	populate_node_context_menu ( &menu, actions );

	QStringList entries;

	for ( const QAction* const entry : menu.actions () )
	{
		entries.append ( entry->isSeparator () ? QStringLiteral ( "-" ) : entry->text () );
	}

	const QStringList expected =
	{
		QStringLiteral ( "Cut" ), QStringLiteral ( "Copy" ), QStringLiteral ( "Paste" ),
		QStringLiteral ( "-" ),
		QStringLiteral ( "Duplicate Node" ), QStringLiteral ( "Delete Node" ), QStringLiteral ( "Rename Key..." ),
		QStringLiteral ( "-" ),
		QStringLiteral ( "Add &Child" ), QStringLiteral ( "Add &Sibling" ),
		QStringLiteral ( "-" ),
		QStringLiteral ( "Move Up" ), QStringLiteral ( "Move Down" ),
		QStringLiteral ( "-" ),
		QStringLiteral ( "Convert Objects to Array" ), QStringLiteral ( "Convert Array to Objects" ),
		QStringLiteral ( "Normalize Array Elements" ),
		QStringLiteral ( "-" ),
		QStringLiteral ( "Con&vert To" )
	};

	QCOMPARE ( entries, expected );

	// EDIT-09's four, in the order the Document menu carries them. They are inside a submenu rather than flat, which is
	// itself the decision: four more items in a menu already fourteen long would bury the commands above them.

	QMenu* const convertMenu = menu.actions ().last ()->menu ();

	QVERIFY ( convertMenu != nullptr );

	QStringList convertEntries;

	for ( const QAction* const entry : convertMenu->actions () )
	{
		convertEntries.append ( entry->text () );
	}

	QCOMPARE ( convertEntries, QStringList ( { QStringLiteral ( "String" ),  QStringLiteral ( "Number" ),
	                                          QStringLiteral ( "Boolean" ), QStringLiteral ( "Null" ) } ) );
}

// The population is defined over an OPTIONAL action set -- a null member is left out, which is what lets a pane be
// built bare in a test. For a submenu that means the submenu itself must disappear rather than appearing empty, and an
// empty "Convert To" would be a dead end a user can open and be told nothing by.

void TestTreeViewPane::an_unwired_convert_group_leaves_no_empty_submenu ()
{
	NodeContextActions actions;

	QAction remove ( QStringLiteral ( "Delete Node" ) );

	actions.remove = &remove;

	QMenu menu;

	populate_node_context_menu ( &menu, actions );

	for ( const QAction* const entry : menu.actions () )
	{
		QVERIFY2
		(
			entry->menu () == nullptr || !entry->menu ()->actions ().isEmpty () || entry->text ().startsWith ( QStringLiteral ( "Add" ) ),
			qPrintable ( QStringLiteral ( "\"%1\" is an empty submenu" ).arg ( entry->text () ) )
		);

		QVERIFY2
		(
			!entry->text ().contains ( QStringLiteral ( "vert To" ) ),
			"Convert To is offered with none of its commands wired"
		);
	}
}

void TestTreeViewPane::the_tree_and_its_band_carry_accessible_names ()
{
	// NFR-05. Two controls, and the band is the one that needs saying: it PAINTS its title rather than hosting a
	// QLabel, so there is no text for the accessibility framework to find on its own.

	QTreeView* const tree = pane->findChild<QTreeView*> ();

	QVERIFY ( tree != nullptr );
	QVERIFY2 ( !tree->accessibleName ().isEmpty (), "The document tree has no accessible name" );

	PaneHeader* const band = pane->findChild<PaneHeader*> ();

	QVERIFY ( band != nullptr );
	QCOMPARE ( band->accessibleName (), band->title () );
}

void TestTreeViewPane::the_tree_paints_through_the_change_mark_delegate ()
{
	// TREE-10. What the dot looks like is tst_tree_node_delegate's, painted on a bare view; this is the other half --
	// that the pane's own tree paints through that delegate, so the dot the suite measures is the one the user sees.

	QTreeView* const tree = pane->findChild<QTreeView*> ();

	QVERIFY ( tree != nullptr );
	QVERIFY ( qobject_cast<TreeNodeDelegate*> ( tree->itemDelegate () ) != nullptr );
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

//---------------------------------------------------------------------------------------------------------------------
// Command-driven selection and the context-menu targeting rule (Phase 9 smoke corrections)
//---------------------------------------------------------------------------------------------------------------------

// The command layer (a menu add, a delete, a move) writes its result to the service with origin Tree, because the
// result must reveal (NAV-03). The pane used to SKIP every inbound Tree-origin selection as its own echo -- an origin
// test standing in for authorship -- so the highlight stopped following command results, the view and the service
// drifted apart, and the context menu then acted on a stale node. The echo is now suppressed by mechanism (a flag
// around the pane's own publication), and a genuine third-party Tree-origin write must be applied like any other
// revealing navigation.

void TestTreeViewPane::a_service_selection_with_tree_origin_moves_the_highlight ()
{
	load ( SAMPLE_DOCUMENT );

	QVERIFY ( !expanded ( QStringLiteral ( "/keep" ) ) );

	selection->set_selection ( pointer ( QStringLiteral ( "/keep/inner/leaf" ) ), SelectionOrigin::Tree );

	// Applied AND revealed: the highlight follows, and the collapsed ancestors open on the way.

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/keep/inner/leaf" ) );

	QVERIFY ( expanded ( QStringLiteral ( "/keep" ) ) );

	// And it did not bounce: the service still holds what the command wrote.

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/keep/inner/leaf" ) );
	QCOMPARE ( selection->origin (), SelectionOrigin::Tree );
}

void TestTreeViewPane::the_context_row_rule_brings_the_service_to_the_clicked_row ()
{
	load ( SAMPLE_DOCUMENT );

	// A right-click on a DIFFERENT row moves the current row, which publishes through the ordinary bridge.

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/keep" ) ) );

	pane->select_context_row ( index_of ( QStringLiteral ( "/transform" ) ) );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/transform" ) );
	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/transform" ) );

	// A right-click on the row that is ALREADY current must still bring the service to it: a no-reveal FormField
	// write-back tracks a form field while the tree highlight stays put, and the menu's commands read the SERVICE --
	// without the re-publish they would act on the form's field instead of the clicked node.

	selection->set_selection ( pointer ( QStringLiteral ( "/keep/inner/leaf" ) ), SelectionOrigin::FormField );

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/transform" ) );   // The highlight stayed put (the branch is collapsed)...

	pane->select_context_row ( index_of ( QStringLiteral ( "/transform" ) ) );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/transform" ) );   // ...and the menu targets it.
	QCOMPARE ( selection->origin (), SelectionOrigin::Tree );
}

//---------------------------------------------------------------------------------------------------------------------
// Deleting the tree's current row (the Phase 9 smoke crash and its silent sibling)
//---------------------------------------------------------------------------------------------------------------------

// The change protocol mutates the document BEFORE the model diffs, and Qt's QItemSelectionModel moves a dying current
// row to a sibling DURING beginRemoveRows -- when the sibling's shadow rowIndex is still stale. The selection bridge
// asks for that sibling's pointer right then. Deriving the member key by indexing the DOCUMENT with the stale SHADOW
// position walked off the end of the shrunk member list (a crash) when the removed row had exactly one following
// sibling, and answered with the WRONG member's key whenever the removal shifted positions. The shadow now stores its
// own keys and answers from them alone.

void TestTreeViewPane::deleting_the_current_row_with_one_following_sibling_survives ()
{
	// The reported crash shape: delete the current row of a two-member object. Mid-window the stale rowIndex (1)
	// indexed a one-member key list -- out of range.

	load ( R"({ "test": { "id": 1, "name": "x" } })" );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/test/id" ) ) );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/test/id" ) );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/test/id" ) ) ), EditOutcome::Applied );

	// The bridge published the SURVIVING sibling the selection model fell back to -- by its true key.

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/test/name" ) );
	QVERIFY  ( document->resolve ( selection->selection () ) != nullptr );
}

void TestTreeViewPane::deleting_the_current_first_member_publishes_the_true_next_key ()
{
	// The same defect's silent form: with three members the stale index stays IN range and simply names the wrong
	// sibling -- delete "a" while it is current, and the fallback row ("b") used to publish as "/t/c".

	load ( R"({ "t": { "a": 1, "b": 2, "c": 3 } })" );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/t/a" ) ) );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/t/a" ) ) ), EditOutcome::Applied );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/t/b" ) );
}

// The ARRAY side of the same window. An element's pointer token is its POSITION, so unlike the object side there is no
// identity the shadow can store to answer mid-window: the fallback sibling's shadow row is pre-removal while the
// document is already post-removal, and any pointer derived right there is stale by the size of the removed run. The
// pane therefore defers its publication past the removal -- to rowsRemoved, after the shadow has renumbered -- and
// must name the survivor at its true, post-removal index. Menu delete self-heals this through select_after_removal;
// undo / redo of an insert or removal is the path with no correction behind it, and it rides exactly this publication.

void TestTreeViewPane::deleting_the_current_array_element_publishes_the_survivors_true_index ()
{
	// The silent shape: four elements, delete the current second one. The fallback row is "y", which now lives at
	// /arr/1 -- its stale pre-removal row (2) would silently name "z".

	load ( R"({ "arr": [ "w", "x", "y", "z" ] })" );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/arr/1" ) ) );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/arr/1" ) ) ), EditOutcome::Applied );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/arr/1" ) );
	QCOMPARE ( document->resolve ( selection->selection () )->string_value (), QStringLiteral ( "y" ) );
}

void TestTreeViewPane::deleting_the_current_first_element_of_two_publishes_a_resolvable_pointer ()
{
	// The unresolvable shape: two elements, delete the current first. The fallback row is "y", now the array's only
	// element at /arr/0 -- its stale pre-removal row (1) names nothing at all in a one-element array.

	load ( R"({ "arr": [ "x", "y" ] })" );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/arr/0" ) ) );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/arr/0" ) ) ), EditOutcome::Applied );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/arr/0" ) );
	QCOMPARE ( document->resolve ( selection->selection () )->string_value (), QStringLiteral ( "y" ) );
}

void TestTreeViewPane::the_reported_smoke_sequence_add_add_delete_survives ()
{
	// The user's exact report, driven the way MainWindow drives it: create test, add id under it, add name under it,
	// right-click id, delete. Each add writes its result to the service with origin Tree (NAV-03), each right-click
	// runs the context-row rule, and the delete acts on the service selection.

	load ( R"({})" );

	QCOMPARE ( undo->add_child ( JsonPointer (), JsonKind::Object, QStringLiteral ( "test" ) ), EditOutcome::Applied );
	selection->set_selection ( pointer ( QStringLiteral ( "/test" ) ), SelectionOrigin::Tree );

	pane->select_context_row ( index_of ( QStringLiteral ( "/test" ) ) );

	QCOMPARE ( undo->add_child ( selection->selection (), JsonKind::Number, QStringLiteral ( "id" ) ), EditOutcome::Applied );
	selection->set_selection ( pointer ( QStringLiteral ( "/test/id" ) ), SelectionOrigin::Tree );

	pane->select_context_row ( index_of ( QStringLiteral ( "/test" ) ) );

	// The clicked row is what the menu acts on -- this is the mis-target the user reported, with test2 standing in
	// for any stale selection.

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/test" ) );

	QCOMPARE ( undo->add_child ( selection->selection (), JsonKind::String, QStringLiteral ( "name" ) ), EditOutcome::Applied );
	selection->set_selection ( pointer ( QStringLiteral ( "/test/name" ) ), SelectionOrigin::Tree );

	pane->select_context_row ( index_of ( QStringLiteral ( "/test/id" ) ) );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/test/id" ) );

	QCOMPARE ( undo->delete_node ( selection->selection () ), EditOutcome::Applied );

	// No crash, id gone, name intact, and the selection landed on a node that exists.

	JsonNode* const test = document->resolve ( pointer ( QStringLiteral ( "/test" ) ) );

	QVERIFY ( test != nullptr );
	QVERIFY ( !test->has_member ( QStringLiteral ( "id" ) ) );
	QVERIFY ( test->has_member ( QStringLiteral ( "name" ) ) );

	QVERIFY ( document->resolve ( selection->selection () ) != nullptr );
}

//---------------------------------------------------------------------------------------------------------------------
// Multiple selection (TREE-09)
//
// The gestures themselves are Qt's -- ExtendedSelection turns Ctrl, Shift, Ctrl+Space and Shift+arrow into selection
// commands with no help from this pane. What is pinned here is the pane's part: that the SET reaches the service beside
// the primary, in document order; that the primary is the row the gesture landed on; and the one rule this pane owns,
// that the set lies within a single parent, with a crossing gesture refused WHOLE rather than reduced to the part that
// fits.
//---------------------------------------------------------------------------------------------------------------------

void TestTreeViewPane::a_single_selection_is_a_set_of_one ()
{
	// TREE-04's half of the arrangement: nothing about ordinary single selection changed, and the set is simply the
	// primary. Every existing collaborator keeps reading selection() and sees exactly what it saw before.

	load ( MULTI_SELECT_DOCUMENT );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/right" ) ) );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/right" ) );
	QCOMPARE ( selection->selection_count (), 1 );
	QCOMPARE ( service_selection_set (), QStringList { QStringLiteral ( "/right" ) } );
}

void TestTreeViewPane::the_set_signal_fires_only_when_the_set_changes ()
{
	// The two signals answer different questions, which is what earns the second one. The PRIMARY re-emits
	// unconditionally because its origin carries a fresh reveal intent every time; the SET has no intent to carry, so
	// re-announcing an identical one would be noise a consumer has to filter out itself.

	load ( MULTI_SELECT_DOCUMENT );

	QSignalSpy sets ( selection.get (), &SelectionService::selection_set_changed );
	QSignalSpy primaries ( selection.get (), &SelectionService::selection_changed );

	const QList<JsonPointer> pair { pointer ( QStringLiteral ( "/left/p" ) ), pointer ( QStringLiteral ( "/left/q" ) ) };

	selection->set_multiple_selection ( pair, pair.at ( 1 ), SelectionOrigin::Tree );

	QCOMPARE ( sets.count (), 1 );
	QCOMPARE ( primaries.count (), 1 );

	// The same set with a different primary -- which is an ordinary Shift+click back down a range the user has already
	// built. The primary moved; the set did not.

	selection->set_multiple_selection ( pair, pair.at ( 0 ), SelectionOrigin::Tree );

	QCOMPARE ( sets.count (), 1 );
	QCOMPARE ( primaries.count (), 2 );
	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/left/p" ) );

	// And a clear empties the set as well as the primary, so a consumer watching only the set is not left holding two
	// pointers into a document nobody has selected anything in.

	selection->clear ();

	QCOMPARE ( sets.count (), 2 );
	QVERIFY  ( selection->selection_set ().isEmpty () );
	QCOMPARE ( selection->selection_count (), 0 );
}

void TestTreeViewPane::ctrl_click_adds_a_sibling_to_the_set ()
{
	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/0" ) );
	click_row ( QStringLiteral ( "/items/2" ), Qt::ControlModifier );

	QCOMPARE ( service_selection_set (),
	           ( QStringList { QStringLiteral ( "/items/0" ), QStringLiteral ( "/items/2" ) } ) );

	QCOMPARE ( selection->selection_count (), 2 );

	// The primary is where the gesture landed, which is what keeps the highlight, the editor pane and the Node path
	// pane from disagreeing about which node is the subject.

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/items/2" ) );

	// A second Ctrl+click on the same row takes it back out again -- the toggle, not an ever-growing set.

	click_row ( QStringLiteral ( "/items/2" ), Qt::ControlModifier );

	QCOMPARE ( service_selection_set (), QStringList { QStringLiteral ( "/items/0" ) } );
}

void TestTreeViewPane::shift_click_extends_the_set_to_a_range ()
{
	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/1" ) );
	click_row ( QStringLiteral ( "/items/3" ), Qt::ShiftModifier );

	QCOMPARE ( service_selection_set (),
	           ( QStringList { QStringLiteral ( "/items/1" ), QStringLiteral ( "/items/2" ), QStringLiteral ( "/items/3" ) } ) );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/items/3" ) );
}

void TestTreeViewPane::the_set_comes_back_in_document_order ()
{
	// QItemSelectionModel returns its ranges in the order they were BUILT, so clicking bottom-up would otherwise hand
	// the multi-node commands a set in click order -- and 15f's removals have to run in descending index order, which
	// is a rule about document positions rather than about which row the user happened to reach for first.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/3" ) );
	click_row ( QStringLiteral ( "/items/1" ), Qt::ControlModifier );
	click_row ( QStringLiteral ( "/items/2" ), Qt::ControlModifier );

	QCOMPARE ( service_selection_set (),
	           ( QStringList { QStringLiteral ( "/items/1" ), QStringLiteral ( "/items/2" ), QStringLiteral ( "/items/3" ) } ) );

	// ...while the PRIMARY is still the last row clicked, which document order says nothing about.

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/items/2" ) );
}

void TestTreeViewPane::an_unmodified_click_collapses_the_set ()
{
	// As in any list: the modifiers build a set and a plain gesture starts a new one. Manual smoke item 3 is the same
	// claim for the arrow keys, which Qt answers the same way.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/0" ) );
	click_row ( QStringLiteral ( "/items/1" ), Qt::ControlModifier );

	QCOMPARE ( selection->selection_count (), 2 );

	click_row ( QStringLiteral ( "/items/3" ) );

	QCOMPARE ( service_selection_set (), QStringList { QStringLiteral ( "/items/3" ) } );
	QCOMPARE ( selection->selection_count (), 1 );
}

void TestTreeViewPane::the_keyboard_alone_builds_a_set ()
{
	// NFR-05, and the manual smoke's fifth item: Ctrl+arrow moves the highlight without disturbing the selection and
	// Ctrl+Space toggles the row it is on, so a multiple selection needs no mouse at all.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/0" ) );

	QTreeView* const view = pane->view ();

	QTest::keyClick ( view, Qt::Key_Down,  Qt::ControlModifier );
	QTest::keyClick ( view, Qt::Key_Down,  Qt::ControlModifier );
	QTest::keyClick ( view, Qt::Key_Space, Qt::ControlModifier );

	QCOMPARE ( service_selection_set (),
	           ( QStringList { QStringLiteral ( "/items/0" ), QStringLiteral ( "/items/2" ) } ) );

	// Shift+arrow extends from there, which is the other half of the keyboard route.

	QTest::keyClick ( view, Qt::Key_Down, Qt::ShiftModifier );

	QVERIFY2 ( service_selection_set ().contains ( QStringLiteral ( "/items/3" ) ),
	           qPrintable ( service_selection_set ().join ( QLatin1Char ( ' ' ) ) ) );
}

void TestTreeViewPane::the_post_delete_landing_follows_nav_03 ()
{
	// NAV-03's landing rule became the PANE's with ExtendedSelection. QAbstractItemView moved a dying current row to
	// the FOLLOWING sibling, but that block is guarded on SingleSelection -- and with it gone QItemSelectionModel's own
	// fallback takes the row ABOVE instead, so deleting a middle element quietly landed on its predecessor. All three
	// branches are asserted, because only the first of them was ever exercised by an existing case.

	// 1. The following sibling.

	load ( R"({ "arr": [ "w", "x", "y", "z" ] })" );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/arr/1" ) ) );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/arr/1" ) ) ), EditOutcome::Applied );
	QCOMPARE ( document->resolve ( selection->selection () )->string_value (), QStringLiteral ( "y" ) );

	// 2. The previous one, when the removed row was last.

	load ( R"({ "arr": [ "w", "x", "y" ] })" );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/arr/2" ) ) );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/arr/2" ) ) ), EditOutcome::Applied );
	QCOMPARE ( document->resolve ( selection->selection () )->string_value (), QStringLiteral ( "x" ) );

	// 3. The parent, when the removed row was the only one.

	load ( R"({ "arr": [ "only" ] })" );

	pane->view ()->setCurrentIndex ( index_of ( QStringLiteral ( "/arr/0" ) ) );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/arr/0" ) ) ), EditOutcome::Applied );
	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/arr" ) );
}

void TestTreeViewPane::a_cross_parent_extension_is_refused_and_reported ()
{
	// TREE-09's one constraint. Refused WHOLE: the previous selection stands, so the set the multi-node commands read
	// never contains nodes from two parents -- and the refusal is reported, because "nothing happened" is the one
	// outcome a user cannot read off the screen.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/0" ) );
	click_row ( QStringLiteral ( "/items/1" ), Qt::ControlModifier );

	QSignalSpy messages ( status.get (), &StatusService::message_posted );

	// /left is a child of the root, not of /items.

	click_row ( QStringLiteral ( "/left" ), Qt::ControlModifier );

	QCOMPARE ( service_selection_set (),
	           ( QStringList { QStringLiteral ( "/items/0" ), QStringLiteral ( "/items/1" ) } ) );

	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/items/1" ) );

	QCOMPARE ( messages.count (), 1 );
	QVERIFY2 ( messages.first ().at ( 0 ).toString ().contains ( QStringLiteral ( "one parent" ) ),
	           qPrintable ( messages.first ().at ( 0 ).toString () ) );
}

void TestTreeViewPane::a_refused_extension_leaves_the_view_showing_the_previous_selection ()
{
	// The other half of "refused whole", and the half a service-only assertion cannot see: Qt has ALREADY changed the
	// view's selection by the time the pane is told, so declining to publish is not enough -- the view has to be put
	// back, or the highlight would show a set nothing downstream agrees with.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/1" ) );
	click_row ( QStringLiteral ( "/items/2" ), Qt::ControlModifier );

	click_row ( QStringLiteral ( "/right" ), Qt::ControlModifier );

	QCOMPARE ( view_selected_rows (),
	           ( QStringList { QStringLiteral ( "/items/1" ), QStringLiteral ( "/items/2" ) } ) );

	// The highlight goes back with the selection, so the row the next single-node command acts on is the one the tree
	// is pointing at.

	QCOMPARE ( pane->model ()->pointer_for_index ( pane->view ()->currentIndex () ).to_string (),
	           QStringLiteral ( "/items/2" ) );
}

void TestTreeViewPane::an_inbound_single_selection_collapses_the_set ()
{
	// Everything that writes the service today writes ONE node (Go To, Find, a command's result), and every one of them
	// means "the selection is now this". So a single-node write collapses the set rather than leaving a stale one
	// behind for the multi-node commands to act on.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/0" ) );
	click_row ( QStringLiteral ( "/items/1" ), Qt::ControlModifier );

	QCOMPARE ( selection->selection_count (), 2 );

	selection->set_selection ( pointer ( QStringLiteral ( "/right" ) ), SelectionOrigin::GoTo );

	QCOMPARE ( service_selection_set (), QStringList { QStringLiteral ( "/right" ) } );
	QCOMPARE ( view_selected_rows (),   QStringList { QStringLiteral ( "/right" ) } );
}

void TestTreeViewPane::deleting_a_node_inside_the_selection_leaves_a_coherent_set ()
{
	// Q22's shape with a set in it. An element's pointer token IS its position, so removing /items/0 renames every
	// element after it -- and the surviving selection has to come back describing where those elements are NOW, not
	// where they were when they were selected.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	click_row ( QStringLiteral ( "/items/1" ) );
	click_row ( QStringLiteral ( "/items/2" ), Qt::ControlModifier );

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/items/0" ) ) ), EditOutcome::Applied );

	// No crash, and every pointer in the set still resolves -- to the two elements that were selected, which are now
	// one position lower.

	for ( const JsonPointer& entry : selection->selection_set () )
	{
		QVERIFY2 ( document->resolve ( entry ) != nullptr, qPrintable ( entry.to_string () ) );
	}

	QCOMPARE ( service_selection_set (),
	           ( QStringList { QStringLiteral ( "/items/0" ), QStringLiteral ( "/items/1" ) } ) );

	JsonNode* const first = document->resolve ( pointer ( QStringLiteral ( "/items/0" ) ) );

	QVERIFY  ( first != nullptr );
	QCOMPARE ( first->number_token (), QStringLiteral ( "20" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Drag-to-reorder (EDIT-10)
//
//   These drive apply_drop rather than a real drag, and that is the whole reason apply_drop is public: QDrag::exec
//   blocks on a nested event loop no offscreen test can drive, which is the QMenu::exec problem that made
//   select_context_row public in Phase 9. What Qt decides -- where the indicator sits, which row the mouse is over --
//   is manual smoke; what the PANE decides is here.
//
//   The emission is captured through a plain lambda connection rather than QSignalSpy, so the argument types need no
//   metatype registration to be read back.
//---------------------------------------------------------------------------------------------------------------------

namespace
{
	struct CapturedDrop
	{
		int                 emissions = 0;
		JsonPointer         parentPointer;
		QList<TreeDropMove> moves;
		QList<int>          landing;
	};
}

void TestTreeViewPane::a_drop_publishes_the_reorder_it_asks_for ()
{
	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	CapturedDrop captured;

	QObject::connect
	(
		pane.get (), &TreeViewPane::nodes_reorder_requested,
		[ &captured ] ( const JsonPointer& parentPointer, const QList<TreeDropMove>& moves, const QList<int>& landing )
		{
			++captured.emissions;

			captured.parentPointer = parentPointer;
			captured.moves         = moves;
			captured.landing       = landing;
		}
	);

	// Element 2 dragged to the top of its own array -- a MIDDLE element to an edge rather than the other way round,
	// so a plan that had lost the drop position could not pass by leaving it where it was.

	click_row ( QStringLiteral ( "/items/2" ) );

	pane->apply_drop ( index_of ( QStringLiteral ( "/items" ) ), 0 );

	QCOMPARE ( captured.emissions, 1 );
	QCOMPARE ( captured.parentPointer.to_string (), QStringLiteral ( "/items" ) );
	QCOMPARE ( captured.moves.size (), 1 );
	QCOMPARE ( captured.moves.first ().fromIndex, 2 );
	QCOMPARE ( captured.moves.first ().toIndex, 0 );
	QCOMPARE ( captured.landing, QList<int> { 0 } );

	// The pane published a request and edited NOTHING. An undo stack is not a view's to write to, and a drop that
	// changed the document here would do it outside the macro the command layer opens.

	QVERIFY ( !undo->can_undo () );
	QVERIFY ( !document->is_dirty () );
}

void TestTreeViewPane::a_multi_node_drop_carries_the_whole_selection ()
{
	// EDIT-10's "a drag carries the whole selection", which is why the drag needed no rule of its own about what it
	// moves. The selection is non-contiguous on purpose: it is allowed to have gaps and must land as one run.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	CapturedDrop captured;

	QObject::connect
	(
		pane.get (), &TreeViewPane::nodes_reorder_requested,
		[ &captured ] ( const JsonPointer&, const QList<TreeDropMove>& moves, const QList<int>& landing )
		{
			++captured.emissions;

			captured.moves   = moves;
			captured.landing = landing;
		}
	);

	click_row ( QStringLiteral ( "/items/1" ) );
	click_row ( QStringLiteral ( "/items/3" ), Qt::ControlModifier );

	pane->apply_drop ( index_of ( QStringLiteral ( "/items" ) ), 0 );

	QCOMPARE ( captured.emissions, 1 );
	QCOMPARE ( captured.landing, ( QList<int> { 0, 1 } ) );

	// Replayed, the plan puts the two dragged elements at the front in document order -- 20 then 40, not 40 then 20,
	// and not the order the user happened to Ctrl-click them in.

	QList<int> order { 0, 1, 2, 3 };

	for ( const TreeDropMove& move : captured.moves )
	{
		order.move ( move.fromIndex, move.toIndex );
	}

	QCOMPARE ( order, ( QList<int> { 1, 3, 0, 2 } ) );
}

void TestTreeViewPane::a_drop_where_the_selection_already_is_publishes_an_empty_plan ()
{
	// The no-op is PUBLISHED rather than swallowed here, which is the reason apply_drop emits instead of acting: it is
	// the one reorder no-op the application can reach, and VAL-05 says a no-op is reported -- which is the command
	// layer's job, not a pane's.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );

	CapturedDrop captured;

	QObject::connect
	(
		pane.get (), &TreeViewPane::nodes_reorder_requested,
		[ &captured ] ( const JsonPointer&, const QList<TreeDropMove>& moves, const QList<int>& )
		{
			++captured.emissions;

			captured.moves = moves;
		}
	);

	click_row ( QStringLiteral ( "/items/2" ) );

	pane->apply_drop ( index_of ( QStringLiteral ( "/items" ) ), 2 );

	QCOMPARE ( captured.emissions, 1 );
	QVERIFY  ( captured.moves.isEmpty () );
}

void TestTreeViewPane::a_drop_among_another_parents_children_publishes_nothing ()
{
	// EDIT-10 reorders WITHIN a parent. JsonTreeModel::canDropMimeData refuses this before the drop, so it is
	// unreachable through the user interface -- but the pane checks the selection as it stands NOW rather than the
	// parent the drag started from, and a tree rebuilt under a drag can make the two differ.

	load ( MULTI_SELECT_DOCUMENT );
	expand ( QStringLiteral ( "/items" ) );
	expand ( QStringLiteral ( "/left" ) );

	int emissions = 0;

	QObject::connect
	(
		pane.get (), &TreeViewPane::nodes_reorder_requested,
		[ &emissions ] ( const JsonPointer&, const QList<TreeDropMove>&, const QList<int>& ) { ++emissions; }
	);

	click_row ( QStringLiteral ( "/items/1" ) );

	pane->apply_drop ( index_of ( QStringLiteral ( "/left" ) ), 0 );

	QCOMPARE ( emissions, 0 );

	// And an invalid drop parent -- Qt's "onto the viewport" -- is refused rather than treated as the root.

	pane->apply_drop ( QModelIndex (), 0 );

	QCOMPARE ( emissions, 0 );
}
