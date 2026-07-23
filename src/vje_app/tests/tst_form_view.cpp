//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for FormView and FormGridController -- the behaviour that lives in the VIEW
//   rather than in either model, and so cannot be reached by tst_json_form_model or tst_json_table_model:
//
//     - The PRESENTATION RULE (EDITOR-02). A scalar selection presents its PARENT with the field indicated, an object
//       or array presents itself, and a scalar document ROOT is the one scalar that presents itself. That resolution is
//       pure logic and is asserted directly rather than through the widgets it drives.
//     - COLUMN STABILITY (EDITOR-03). Committing a much longer value must not resize a column. This is the
//       "no width flap on value refresh" item the development plan lists as manual smoke -- it is deterministic, so it
//       is pinned here instead.
//     - The SELECTION ASYMMETRY (EDITOR-04). A form field writes its focus back to the selection service; a table cell
//       deliberately does not. Both halves are asserted, because the second is a rule that looks like an omission.
//     - ENTER AS AN ACTIVATION KEY (EDITOR-03). Enter opens the editor on a scalar and drills in on a container -- it
//       is never a navigation key, which is the single largest departure from QTableView's defaults.
//     - The EDIT-ON HAND-OVER (SET-05). A tree-originated selection hands over the caret under Single click and does
//       not under Double click -- and Double click is now the default, so the out-of-box case is asserted too.
//     - The FIELD WRITE-BACK AS AN ECHO (EDITOR-04). Landing on a container field must not drill the form into it; the
//       reported symptom was a pane that emptied and arrow keys that stopped working.
//     - TWO REACHABLE, EDITABLE COLUMNS (EDITOR-02, EDIT-02). Left / Right cross between key and value, Up / Down hold
//       their column, and a gesture edits the cell it was made on -- including renaming a key in place, which an
//       array's key must do even though its value drills in.
//
//   Runs under the offscreen QPA platform. Every gesture here is delivered as a real key or a real current-index
//   change, so what is exercised is the widget wiring rather than a paraphrase of it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "models/JsonFormModel.hpp"
#include "models/JsonTableModel.hpp"
#include "services/SelectionService.hpp"
#include "services/SettingsStore.hpp"
#include "views/FormView.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QLineEdit>
#include <QTableView>
#include <QTemporaryDir>

#include <memory>

using namespace vje;

namespace
{
	const char* const SAMPLE_DOCUMENT = R"({
		"id": 1001,
		"name": "Alex Rivera",
		"roles": [ "admin", "editor" ],
		"projects":
		[
			{ "name": "JSON Editor",    "status": "in-progress", "tags": [ "ui" ] },
			{ "name": "Data Migration", "status": "completed",   "tags": null }
		]
	})";

	JsonPointer pointer ( const QString& text )
	{
		return JsonPointer::parse ( text );
	}
}

class TestFormView : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	// The presentation rule (EDITOR-02).

	void a_container_presents_itself ();
	void a_scalar_presents_its_parent_with_the_field_indicated ();
	void a_scalar_root_presents_itself_as_a_lone_form ();
	void an_unresolvable_pointer_presents_nothing ();

	// Presentation, end to end.

	void presenting_an_object_shows_the_form ();
	void presenting_an_array_shows_the_table ();
	void a_scalar_selection_makes_its_field_current ();

	// Column stability (EDITOR-03).

	void committing_a_longer_value_does_not_resize_columns ();
	void no_column_is_wider_than_the_maximum ();
	void a_single_column_array_does_not_span_the_pane ();

	// The selection asymmetry (EDITOR-04).

	void a_form_field_writes_its_focus_back_to_the_selection ();
	void a_table_cell_does_not_write_the_selection_back ();

	// Activation (EDITOR-03, EDITOR-05).

	void enter_opens_the_editor_on_a_scalar_cell ();
	void enter_drills_into_a_container_cell ();

	// The Edit-on hand-over (SET-05).

	void presenting_a_selection_never_opens_an_editor ();
	void a_tree_click_hands_over_the_caret_on_single_click ();
	void a_tree_click_withholds_the_caret_on_double_click ();
	void a_gesture_on_a_container_hands_over_nothing ();

	// The field write-back is an echo, not a navigation (EDITOR-04).

	void stepping_onto_a_container_field_leaves_the_form_in_place ();
	void the_arrow_keys_step_past_a_container_field ();
	void a_drill_in_gesture_still_moves_the_form ();

	// Two columns, both reachable (EDITOR-02).

	void presenting_lands_the_highlight_on_the_value_column ();
	void left_and_right_move_between_the_key_and_its_value ();
	void up_and_down_stay_in_the_column_the_user_chose ();
	void tab_is_not_a_grid_key ();

	// Editing a key in place (EDIT-02).

	void enter_on_a_key_opens_an_editor_on_the_key ();
	void enter_on_a_value_opens_an_editor_on_the_value ();
	void enter_on_a_containers_key_renames_rather_than_drilling_in ();
	void the_edit_on_default_withholds_the_caret ();

private:

	void build_view              ( const QString& editOnValue );
	void load                    ( const char* text );
	void connect_selection_loop  ();

	QLineEdit* open_editor_in ( QTableView* gridView ) const;

	QTemporaryDir                     settingsDirectory;
	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<SettingsStore>    settings;
	std::unique_ptr<FormView>         view;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::init ()
{
	document  = std::make_unique<JsonDocument> ();
	undo      = std::make_unique<UndoController> ( document.get () );
	selection = std::make_unique<SelectionService> ();

	build_view ( settings_values::EDIT_ON_SINGLE_CLICK );

	load ( SAMPLE_DOCUMENT );
}

void TestFormView::cleanup ()
{
	// Strict reverse dependency order: the view's models observe the document, and the undo
	// controller writes through it, so the document is destroyed last.

	view.reset ();
	settings.reset ();
	selection.reset ();
	undo.reset ();
	document.reset ();
}

void TestFormView::build_view ( const QString& editOnValue )
{
	view.reset ();
	settings.reset ();

	settings = std::make_unique<SettingsStore>
	(
		settingsDirectory.filePath ( QStringLiteral ( "settings.json" ) )
	);

	settings->set_string ( settings_keys::FORM_EDIT_ON, editOnValue );

	view = std::make_unique<FormView> ( document.get (), undo.get (), selection.get (), settings.get (), nullptr );

	// Offscreen, but shown: QTableView needs a geometry before it will open an editor widget.

	view->resize ( 800, 600 );
	view->show ();
}

void TestFormView::load ( const char* text )
{
	ParseResult result = JsonParser::parse ( QString::fromUtf8 ( text ) );

	document->set_root ( std::move ( result.root ) );
}

void TestFormView::connect_selection_loop ()
{
	// What EditorPane does in the application: every selection change is routed straight back to the visible view.
	// Wiring it here is what makes the cases below REPRODUCE the reported failure rather than paraphrase it -- the bug
	// lived in the round trip, and neither end of it looks wrong on its own.

	connect
	(
		selection.get (), &SelectionService::selection_changed,
		view.get (),      [ this ] ( const JsonPointer& target, SelectionOrigin origin )
		{
			view->present ( target, origin );
		}
	);
}

QLineEdit* TestFormView::open_editor_in ( QTableView* gridView ) const
{
	return gridView->viewport ()->findChild<QLineEdit*> ();
}

//---------------------------------------------------------------------------------------------------------------------
// The presentation rule (EDITOR-02)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::a_container_presents_itself ()
{
	const FormPresentation objectResult = resolve_presentation ( document.get (), JsonPointer () );

	QCOMPARE ( objectResult.mode, FormPresentation::Mode::ObjectForm );
	QVERIFY  ( objectResult.container.is_root () );
	QVERIFY  ( !objectResult.hasFocus );

	const FormPresentation arrayResult = resolve_presentation
	(
		document.get (), pointer ( QStringLiteral ( "/projects" ) )
	);

	QCOMPARE ( arrayResult.mode, FormPresentation::Mode::ArrayTable );
	QCOMPARE ( arrayResult.container.to_string (), QStringLiteral ( "/projects" ) );
	QVERIFY  ( !arrayResult.hasFocus );
}

void TestFormView::a_scalar_presents_its_parent_with_the_field_indicated ()
{
	// The rule that makes the tree and the editor pane feel joined up: clicking a leaf shows the form it belongs to,
	// not an empty view of a single value.

	const FormPresentation inObject = resolve_presentation
	(
		document.get (), pointer ( QStringLiteral ( "/name" ) )
	);

	QCOMPARE ( inObject.mode, FormPresentation::Mode::ObjectForm );
	QVERIFY  ( inObject.container.is_root () );
	QVERIFY  ( inObject.hasFocus );
	QCOMPARE ( inObject.focus.to_string (), QStringLiteral ( "/name" ) );

	// A scalar inside an ARRAY resolves to the table instead, with the corresponding CELL as the focus.

	const FormPresentation inArray = resolve_presentation
	(
		document.get (), pointer ( QStringLiteral ( "/roles/1" ) )
	);

	QCOMPARE ( inArray.mode, FormPresentation::Mode::ArrayTable );
	QCOMPARE ( inArray.container.to_string (), QStringLiteral ( "/roles" ) );
	QCOMPARE ( inArray.focus.to_string (),     QStringLiteral ( "/roles/1" ) );

	// And a scalar inside an array ELEMENT resolves to that element's form, not to the outer table.

	const FormPresentation inElement = resolve_presentation
	(
		document.get (), pointer ( QStringLiteral ( "/projects/0/status" ) )
	);

	QCOMPARE ( inElement.mode, FormPresentation::Mode::ObjectForm );
	QCOMPARE ( inElement.container.to_string (), QStringLiteral ( "/projects/0" ) );
}

void TestFormView::a_scalar_root_presents_itself_as_a_lone_form ()
{
	load ( R"(42)" );

	const FormPresentation result = resolve_presentation ( document.get (), JsonPointer () );

	QCOMPARE ( result.mode, FormPresentation::Mode::ObjectForm );
	QVERIFY  ( result.container.is_root () );
	QVERIFY  ( result.hasFocus );
}

void TestFormView::an_unresolvable_pointer_presents_nothing ()
{
	const FormPresentation result = resolve_presentation
	(
		document.get (), pointer ( QStringLiteral ( "/missing/deeper" ) )
	);

	QCOMPARE ( result.mode, FormPresentation::Mode::Nothing );
}

//---------------------------------------------------------------------------------------------------------------------
// Presentation, end to end
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::presenting_an_object_shows_the_form ()
{
	view->present ( JsonPointer (), SelectionOrigin::Programmatic );

	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ObjectForm );
	QVERIFY  ( view->form_model ()->is_presenting () );
	QCOMPARE ( view->form_model ()->rowCount (), 4 );
}

void TestFormView::presenting_an_array_shows_the_table ()
{
	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ArrayTable );
	QVERIFY  ( view->table_model ()->is_object_table () );
	QCOMPARE ( view->table_model ()->rowCount (),    2 );
	QCOMPARE ( view->table_model ()->columnCount (), 3 );

	// A container selection has no field to indicate, so the grid starts on its first cell -- immediately navigable.

	QCOMPARE ( view->array_table_view ()->currentIndex ().row (),    0 );
	QCOMPARE ( view->array_table_view ()->currentIndex ().column (), 0 );
}

void TestFormView::a_scalar_selection_makes_its_field_current ()
{
	view->present ( pointer ( QStringLiteral ( "/roles/1" ) ), SelectionOrigin::GoTo );

	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ArrayTable );
	QCOMPARE ( view->array_table_view ()->currentIndex ().row (), 1 );

	// And in the form it LANDS on the value column, which is where the editing is. The key column is a keystroke away
	// (EDITOR-02) -- landing there is what would be wrong, not being able to reach it.

	view->present ( pointer ( QStringLiteral ( "/name" ) ), SelectionOrigin::GoTo );

	QCOMPARE ( view->object_form_view ()->currentIndex ().row (),    1 );
	QCOMPARE ( view->object_form_view ()->currentIndex ().column (), JsonFormModel::VALUE_COLUMN );
}

//---------------------------------------------------------------------------------------------------------------------
// Column stability (EDITOR-03)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::committing_a_longer_value_does_not_resize_columns ()
{
	// EDITOR-03: "a cell commit refreshes values in the existing table rather than rebuilding it". A column that
	// re-measured itself on every commit would make the whole table shift under the user as they type -- the flap
	// that version 1.0's experience records. Columns are sized once per presented node and are the user's thereafter.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

	QTableView* const tableView = view->array_table_view ();

	const int widthBefore = tableView->columnWidth ( 0 );

	QVERIFY ( widthBefore > 0 );

	QVERIFY
	(
		view->table_model ()->setData
		(
			view->table_model ()->index ( 0, 0 ),
			QStringLiteral ( "a very much longer project name than the column was ever sized for" ),
			Qt::EditRole
		)
	);

	QCOMPARE ( tableView->columnWidth ( 0 ), widthBefore );
}

// The clamp size_columns() applies, asserted where it can actually be broken. It was broken: the header stretched its
// LAST section, which handed that column the whole remaining pane width and exempted it from the maximum -- so the rule
// held for every column except the one most likely to hold a long value.

void TestFormView::no_column_is_wider_than_the_maximum ()
{
	view->array_table_view ()->resize ( 900, 300 );

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

	QTableView* const tableView = view->array_table_view ();

	QVERIFY ( tableView->model ()->columnCount () > 0 );

	for ( int column = 0; column < tableView->model ()->columnCount (); ++column )
	{
		QVERIFY2
		(
			tableView->columnWidth ( column ) <= config::form::MAXIMUM_COLUMN_WIDTH,
			qPrintable ( QStringLiteral ( "column %1 is %2px, over the %3px maximum" )
				.arg ( column )
				.arg ( tableView->columnWidth ( column ) )
				.arg ( config::form::MAXIMUM_COLUMN_WIDTH ) )
		);
	}
}

// The worst case of the same defect, and the one a user meets first: in a SCALAR array the only column is also the last
// one, so a stretched last section made it span the entire pane whatever the content measured.

void TestFormView::a_single_column_array_does_not_span_the_pane ()
{
	QTableView* const tableView = view->array_table_view ();

	tableView->resize ( 900, 300 );

	view->present ( pointer ( QStringLiteral ( "/roles" ) ), SelectionOrigin::Tree );

	QCOMPARE ( tableView->model ()->columnCount (), 1 );

	QVERIFY2 ( tableView->columnWidth ( 0 ) < tableView->viewport ()->width (),
	           "a single-column array must be sized to its content, not to the pane" );

	QVERIFY ( tableView->columnWidth ( 0 ) <= config::form::MAXIMUM_COLUMN_WIDTH );
}

//---------------------------------------------------------------------------------------------------------------------
// The selection asymmetry (EDITOR-04)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::a_form_field_writes_its_focus_back_to_the_selection ()
{
	view->present ( JsonPointer (), SelectionOrigin::Programmatic );

	// The user clicking or keying onto a field, which is what a current-index change models.

	view->object_form_view ()->setCurrentIndex
	(
		view->form_model ()->index ( 1, JsonFormModel::VALUE_COLUMN )
	);

	QVERIFY  ( selection->has_selection () );
	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/name" ) );

	// FormField, specifically: the origin that tells the tree to select WITHOUT expanding, so a collapsed branch stays
	// shut while the user works down a form (EDITOR-04).

	QCOMPARE ( selection->origin (), SelectionOrigin::FormField );
	QVERIFY  ( !reveals_selection ( selection->origin () ) );
}

void TestFormView::a_table_cell_does_not_write_the_selection_back ()
{
	// The half that looks like an omission and is not. In-place cell editing must not drag the tree around, so moving
	// the current CELL is deliberately silent (EDITOR-04).

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

	selection->set_selection ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

	QSignalSpy selectionSpy ( selection.get (), &SelectionService::selection_changed );

	view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 1, 1 ) );

	QCOMPARE ( selectionSpy.count (), 0 );
	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/projects" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Activation (EDITOR-03, EDITOR-05)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::enter_opens_the_editor_on_a_scalar_cell ()
{
	// "Enter is not a navigation key" (EDITOR-03) -- the single largest departure from QTableView's defaults, where
	// Enter would move the current cell down.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QVERIFY ( open_editor_in ( tableView ) == nullptr );

	QTest::keyClick ( tableView, Qt::Key_Return );

	QLineEdit* const editor = open_editor_in ( tableView );

	QVERIFY  ( editor != nullptr );
	QCOMPARE ( editor->text (), QStringLiteral ( "JSON Editor" ) );

	// The highlight did NOT move -- Enter activated in place.

	QCOMPARE ( tableView->currentIndex ().row (), 0 );
}

void TestFormView::enter_drills_into_a_container_cell ()
{
	// EDITOR-05, and the reentrancy discipline with it: the drill-in is deferred onto the event loop so the consumer
	// may re-present the very table whose event handler is still on the stack.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 2 ) );   // tags: [ "ui" ]

	QSignalSpy selectionSpy ( selection.get (), &SelectionService::selection_changed );

	QTest::keyClick ( tableView, Qt::Key_Return );

	// Nothing yet -- that is the deferral, and it is the point.

	QCOMPARE ( selectionSpy.count (), 0 );
	QVERIFY  ( open_editor_in ( tableView ) == nullptr );

	QCoreApplication::processEvents ();

	QCOMPARE ( selectionSpy.count (), 1 );
	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/projects/0/tags" ) );

	// DrillIn reveals, so the tree expands to show where the user landed (EDITOR-04).

	QCOMPARE ( selection->origin (), SelectionOrigin::DrillIn );
	QVERIFY  ( reveals_selection ( selection->origin () ) );
}

//---------------------------------------------------------------------------------------------------------------------
// The Edit-on hand-over (SET-05)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::presenting_a_selection_never_opens_an_editor ()
{
	// The rule the other tests in this group lean on, stated on its own: PRESENTING IS PASSIVE.
	//
	// This is the regression. The hand-over used to key off SelectionOrigin::Tree, which is the origin of a tree CLICK
	// and equally the origin of a tree ARROW KEY -- so holding Down in the tree opened an editor on the first scalar it
	// passed and took the keyboard out of the tree entirely. The user was then driving the form while believing they
	// were still driving the tree. A selection is not a gesture.

	view->present ( pointer ( QStringLiteral ( "/name" ) ), SelectionOrigin::Tree );

	// Presented and made current -- the field is indicated, which is the whole of what a selection asks for.

	QCOMPARE ( view->object_form_view ()->currentIndex ().row (), 1 );
	QVERIFY  ( open_editor_in ( view->object_form_view () ) == nullptr );
}

void TestFormView::a_tree_click_hands_over_the_caret_on_single_click ()
{
	// The default (SET-05). A CLICK on a scalar in the tree presents its field and gives the user the caret.

	view->present ( pointer ( QStringLiteral ( "/name" ) ), SelectionOrigin::Tree );
	view->tree_node_clicked ();

	QLineEdit* const editor = open_editor_in ( view->object_form_view () );

	QVERIFY  ( editor != nullptr );
	QCOMPARE ( editor->text (), QStringLiteral ( "Alex Rivera" ) );
}

void TestFormView::a_tree_click_withholds_the_caret_on_double_click ()
{
	build_view ( settings_values::EDIT_ON_DOUBLE_CLICK );

	view->present ( pointer ( QStringLiteral ( "/name" ) ), SelectionOrigin::Tree );
	view->tree_node_clicked ();

	// Under "Double click" the click only presents; the caret waits for the separate activation gesture, which arrives
	// as activate_editing().

	QCOMPARE ( view->object_form_view ()->currentIndex ().row (), 1 );
	QVERIFY  ( open_editor_in ( view->object_form_view () ) == nullptr );

	view->activate_editing ();

	QVERIFY ( open_editor_in ( view->object_form_view () ) != nullptr );
}

void TestFormView::a_gesture_on_a_container_hands_over_nothing ()
{
	// Only a scalar indicates a field, so only a scalar has somewhere to put the caret. Enter on a container is an
	// EXPANSION gesture (NAV-02) that reaches the view as an activation; answering it by opening an editor on the first
	// member of the branch the user just opened would be the same theft of the keyboard, one gesture later.

	view->present ( pointer ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );

	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ObjectForm );

	view->tree_node_clicked ();

	QVERIFY ( open_editor_in ( view->object_form_view () ) == nullptr );

	view->activate_editing ();

	QVERIFY ( open_editor_in ( view->object_form_view () ) == nullptr );
}

//=====================================================================================================================
// The field write-back is an echo, not a navigation (EDITOR-04)
//
// The document here is the reported case, reduced: a container sits directly below a scalar, and the first of the two
// containers is EMPTY -- which is what turned a silent drill-in into a visibly blank pane.
//=====================================================================================================================

namespace
{
	const char* const MIXED_MEMBERS_DOCUMENT = R"({
		"allTypes":
		{
			"aString":       "hello",
			"anEmptyObject": {},
			"anEmptyArray":  []
		}
	})";
}

void TestFormView::stepping_onto_a_container_field_leaves_the_form_in_place ()
{
	// A container presents ITSELF, so answering the grid's own write-back as a navigation drilled the form into
	// whichever container the current row happened to reach -- and an empty one has no rows to land on next, so the
	// fields disappeared and the arrow keys went with them.

	load ( MIXED_MEMBERS_DOCUMENT );

	connect_selection_loop ();

	view->present ( pointer ( QStringLiteral ( "/allTypes" ) ), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Down );

	QCOMPARE ( view->presented_pointer (), pointer ( QStringLiteral ( "/allTypes" ) ) );
	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ObjectForm );

	// The symptom, stated as the assertion it deserves: the fields are still there.

	QCOMPARE ( view->form_model ()->rowCount (), 3 );

	QCOMPARE ( formView->currentIndex ().row (), 1 );
}

void TestFormView::the_arrow_keys_step_past_a_container_field ()
{
	// The other half of the report -- "the up and down arrow keys no longer have any effect". A container cell is
	// landable and nothing more (EDITOR-03), so crossing one must leave the walk exactly where it was.

	load ( MIXED_MEMBERS_DOCUMENT );

	connect_selection_loop ();

	view->present ( pointer ( QStringLiteral ( "/allTypes" ) ), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Down );
	QTest::keyClick ( formView, Qt::Key_Down );

	QCOMPARE ( formView->currentIndex ().row (), 2 );

	QTest::keyClick ( formView, Qt::Key_Up );

	QCOMPARE ( formView->currentIndex ().row (), 1 );

	QCOMPARE ( view->presented_pointer (), pointer ( QStringLiteral ( "/allTypes" ) ) );
}

void TestFormView::a_drill_in_gesture_still_moves_the_form ()
{
	// The guard against over-correcting. Refusing the ECHO must not refuse a real navigation: drill-in is a gesture,
	// carries its own origin, and still has to land -- on the empty container too, where the blank form is now what
	// the user actually asked for.

	load ( MIXED_MEMBERS_DOCUMENT );

	connect_selection_loop ();

	view->present ( pointer ( QStringLiteral ( "/allTypes" ) ), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 1, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Return );

	// Drill-in is deferred onto the event loop.

	QCoreApplication::processEvents ();

	QCOMPARE ( view->presented_pointer (), pointer ( QStringLiteral ( "/allTypes/anEmptyObject" ) ) );
}

//=====================================================================================================================
// Two columns, both reachable (EDITOR-02)
//
// The object form is a key column and a value column, and the highlight may sit in either. It did not always: the
// controller used to bounce the current cell back to the value column on every move, which made the key a label rather
// than a place. Left / Right now cross between them and Up / Down keep the column they are given.
//=====================================================================================================================

void TestFormView::presenting_lands_the_highlight_on_the_value_column ()
{
	// The value is what the user came to edit, so that is where a freshly presented node puts them -- the key is one
	// keystroke away rather than in the way.

	view->present ( pointer ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );

	QCOMPARE ( view->object_form_view ()->currentIndex ().column (), int ( JsonFormModel::VALUE_COLUMN ) );
}

void TestFormView::left_and_right_move_between_the_key_and_its_value ()
{
	view->present ( pointer ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 1, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Left );

	QCOMPARE ( formView->currentIndex ().column (), int ( JsonFormModel::KEY_COLUMN ) );
	QCOMPARE ( formView->currentIndex ().row    (), 1 );

	QTest::keyClick ( formView, Qt::Key_Right );

	QCOMPARE ( formView->currentIndex ().column (), int ( JsonFormModel::VALUE_COLUMN ) );

	// The form is two columns wide and the highlight stops at both ends rather than wrapping to the next row.

	QTest::keyClick ( formView, Qt::Key_Right );

	QCOMPARE ( formView->currentIndex ().column (), int ( JsonFormModel::VALUE_COLUMN ) );
	QCOMPARE ( formView->currentIndex ().row    (), 1 );
}

void TestFormView::up_and_down_stay_in_the_column_the_user_chose ()
{
	// The half of the rule that makes the other half useful: having crossed to the keys, the user is reading down the
	// keys, and Up / Down must not quietly return them to the values.

	view->present ( pointer ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::KEY_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Down );

	QCOMPARE ( formView->currentIndex ().row    (), 1 );
	QCOMPARE ( formView->currentIndex ().column (), int ( JsonFormModel::KEY_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Up );

	QCOMPARE ( formView->currentIndex ().row    (), 0 );
	QCOMPARE ( formView->currentIndex ().column (), int ( JsonFormModel::KEY_COLUMN ) );
}

void TestFormView::tab_is_not_a_grid_key ()
{
	// Tab belongs to the workspace now (NAV-04). Both grids have to have let go of it, or the key never reaches the
	// focus cycle -- QAbstractItemView consumes it when tabKeyNavigation is on, which is Qt's default.

	QVERIFY ( !view->object_form_view ()->tabKeyNavigation () );
	QVERIFY ( !view->array_table_view ()->tabKeyNavigation () );
}

//=====================================================================================================================
// Editing a key in place (EDIT-02)
//
// A gesture now edits the cell it was made on. The rule that makes this more than a one-line change is that a key and
// its value answer DIFFERENTLY: an array's key renames while its value drills in, so the order in which activation asks
// those two questions decides whether renaming is reachable at all.
//=====================================================================================================================

void TestFormView::enter_on_a_key_opens_an_editor_on_the_key ()
{
	view->present ( pointer ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::KEY_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Return );

	QLineEdit* const editor = open_editor_in ( formView );

	QVERIFY ( editor != nullptr );

	// The KEY, not the value it labels -- which is what the old redirect would have given.

	QCOMPARE ( editor->text (), QStringLiteral ( "name" ) );
}

void TestFormView::enter_on_a_value_opens_an_editor_on_the_value ()
{
	view->present ( pointer ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Return );

	QLineEdit* const editor = open_editor_in ( formView );

	QVERIFY ( editor != nullptr );

	QCOMPARE ( editor->text (), QStringLiteral ( "JSON Editor" ) );
}

void TestFormView::enter_on_a_containers_key_renames_rather_than_drilling_in ()
{
	// The case the activation order exists for. "roles" is an array: its value drills in, and its key must not.

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	const int row = view->form_model ()->row_for_pointer ( pointer ( QStringLiteral ( "/roles" ) ) );

	QVERIFY ( row >= 0 );

	formView->setCurrentIndex ( view->form_model ()->index ( row, JsonFormModel::KEY_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Return );

	QLineEdit* const editor = open_editor_in ( formView );

	QVERIFY ( editor != nullptr );

	QCOMPARE ( editor->text (), QStringLiteral ( "roles" ) );

	// Drill-in is deferred onto the event loop, so a request would land here if one had been made.

	QCoreApplication::processEvents ();

	QVERIFY2 ( view->presented_pointer ().is_root (), "renaming a key must not navigate into the value it names" );
}

void TestFormView::the_edit_on_default_withholds_the_caret ()
{
	// SET-05's default is now Double click: out of the box a single tree click presents the field and stops there.

	settings->remove ( settings_keys::FORM_EDIT_ON );

	view->present ( pointer ( QStringLiteral ( "/name" ) ), SelectionOrigin::Tree );
	view->tree_node_clicked ();

	QVERIFY2 ( open_editor_in ( view->object_form_view () ) == nullptr,
	           "a single tree click must not open an editor out of the box" );
}

QTEST_MAIN ( TestFormView )

#include "tst_form_view.moc"
