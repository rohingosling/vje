//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include "services/ClipboardService.hpp"
#include "services/SelectionService.hpp"
#include "services/SettingsStore.hpp"
#include "views/FormView.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QtTest/QtTest>

#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QPlainTextEdit>
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
	void a_document_load_repopulates_the_same_pointer ();

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

	// The array-table cell clipboard and provisional rows (Phase 9 -- EDITOR-11 / 12).

	void a_cell_copy_and_paste_round_trips ();
	void a_missing_cell_copy_and_cut_are_handled_no_ops ();
	void an_empty_array_presents_a_surviving_provisional_row ();
	void down_at_the_bottom_edge_grows_a_provisional_row ();
	void pasting_into_a_provisional_row_materializes_it ();
	void a_typed_entry_into_a_provisional_row_materializes_after_the_event_loop_turns ();
	void the_object_form_declines_the_cell_clipboard ();

	// Wrap strings (SET-05).

	void each_wrapped_row_is_sized_to_its_own_content ();
	void an_unwrapped_field_keeps_its_height_beside_a_wrapped_one ();
	void a_committed_value_re_measures_its_row ();
	void the_array_table_never_wraps ();
	void wrapping_opens_a_string_in_a_multi_line_editor ();

	// NFR-05.

	void both_grids_carry_accessible_names ();
	void an_open_editor_announces_the_cell_it_edits ();
	void a_key_editor_announces_the_column_not_the_key ();
	void a_table_cell_editor_announces_its_column ();
	void a_number_keeps_its_single_line_editor_and_its_validator ();
	void the_arrows_move_the_caret_inside_a_wrapped_editor ();
	void shift_and_an_arrow_extends_the_selection_in_a_wrapped_editor ();
	void control_and_an_arrow_leaves_a_wrapped_editor ();
	void control_and_an_arrow_leaves_a_single_line_editor ();
	void control_and_an_arrow_leaves_a_boolean_editor ();
	void an_unwrapped_string_editor_keeps_the_ordinary_arrow_keys ();
	void control_enter_inserts_a_line_break ();

	// Left / Right inside an OPEN editor belong to the text, in both grids (EDITOR-02 / EDITOR-03).

	void left_and_right_move_the_caret_inside_a_table_cell_editor ();
	void left_and_right_move_the_caret_inside_a_form_value_editor ();
	void an_arrow_at_the_end_of_a_cell_editor_does_not_leave_it ();

	// Printing (FILE-12).

	void the_object_form_prints_its_rows_without_a_header ();
	void the_array_table_prints_its_column_keys_as_headers ();
	void a_ragged_element_prints_an_empty_cell_under_the_column_it_lacks ();
	void the_provisional_row_is_not_printed ();
	void a_view_presenting_nothing_prints_nothing ();

private:

	void build_view              ( const QString& editOnValue );
	void load                    ( const char* text );
	void connect_selection_loop  ();

	QLineEdit*      open_editor_in         ( QTableView* gridView ) const;
	QPlainTextEdit* open_wrapped_editor_in ( QTableView* gridView, int row, int column ) const;

	QTemporaryDir                     settingsDirectory;
	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<SettingsStore>    settings;
	std::unique_ptr<ClipboardService> clipboard;
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
	clipboard.reset ();
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

	// The temp directory is a member and outlives each rebuilt store, so a case that switched Wrap strings on would
	// leak it into the next one. Stated rather than assumed, so every case starts at the documented default.

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, false );

	// A real clipboard over the offscreen QClipboard, so the array-table cell clipboard (EDITOR-11) can be driven end
	// to end.

	clipboard = std::make_unique<ClipboardService> ( QApplication::clipboard () );

	view = std::make_unique<FormView> ( document.get (), undo.get (), selection.get (), settings.get (), nullptr, clipboard.get () );

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

void TestFormView::a_document_load_repopulates_the_same_pointer ()
{
	// The defect reported from Phase 10's smoke: opening a SECOND file left the Form View blank until the user clicked
	// some other node and came back. Both documents' roots carry the same pointer, so the idempotence check -- which
	// exists so that a re-present of what is already presented does not rebuild a model and lose column widths, scroll
	// position, and any open editor (EDITOR-03) -- judged the new document's root to be what was already on screen.
	// Meanwhile the two models had cleared themselves on the same reset signal, so "already on screen" was nothing at
	// all. The view's memory of what it is showing is therefore a claim about a SPECIFIC document, and a load has to
	// drop it.
	//
	// Both grids are exercised at the same pointer and in the same mode, since a mode CHANGE would defeat the
	// idempotence check by itself and pass either way.

	view->present ( JsonPointer (), SelectionOrigin::Programmatic );

	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ObjectForm );
	QCOMPARE ( view->form_model ()->rowCount (), 4 );

	load ( R"({ "one": 1, "two": 2 })" );

	view->present ( JsonPointer (), SelectionOrigin::Programmatic );

	QVERIFY  ( view->form_model ()->is_presenting () );
	QCOMPARE ( view->form_model ()->rowCount (), 2 );

	// The array table, same shape: an array root replaced by another array root.

	load ( R"([ { "a": 1 }, { "a": 2 }, { "a": 3 } ])" );

	view->present ( JsonPointer (), SelectionOrigin::Programmatic );

	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ArrayTable );
	QCOMPARE ( view->table_model ()->rowCount (), 3 );

	load ( R"([ { "a": 1 } ])" );

	view->present ( JsonPointer (), SelectionOrigin::Programmatic );

	QVERIFY  ( view->table_model ()->is_presenting () );
	QCOMPARE ( view->table_model ()->rowCount (), 1 );
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

//---------------------------------------------------------------------------------------------------------------------
// The array-table cell clipboard and provisional rows (Phase 9)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::a_cell_copy_and_paste_round_trips ()
{
	// EDITOR-11: copy the current cell, move to another, paste. The value crosses the real (offscreen) clipboard.

	view->present ( pointer ( QStringLiteral ( "/roles" ) ), SelectionOrigin::Tree );

	QTableView* const table = view->array_table_view ();

	table->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );   // "admin"

	QVERIFY ( view->cell_copy () );

	table->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );   // "editor"

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/roles/1" ) ) )->string_value (), QStringLiteral ( "admin" ) );
}

void TestFormView::a_missing_cell_copy_and_cut_are_handled_no_ops ()
{
	// EDITOR-11: copy / cut on a MISSING (ragged) cell are no-ops -- there is no value to place on the clipboard --
	// and they are HANDLED no-ops: the gesture belongs to the cell while the table is the face, so it must return
	// true and NOT fall through to the node clipboard, which would silently copy the whole selected array over
	// whatever the clipboard held.

	load ( R"({ "rows": [ { "a": 1, "b": 2 }, { "a": 3 } ] })" );

	view->present ( pointer ( QStringLiteral ( "/rows" ) ), SelectionOrigin::Tree );

	QTableView* const table = view->array_table_view ();

	table->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QVERIFY ( view->cell_copy () );                                    // Known content on the clipboard ("1").

	table->setCurrentIndex ( view->table_model ()->index ( 1, 1 ) );   // Element 1 lacks "b" -- a missing cell.

	QVERIFY ( view->cell_copy () );                                    // Owned, and nothing happened...
	QVERIFY ( view->cell_cut () );

	const std::unique_ptr<JsonNode> value = clipboard->value ();       // ...so the clipboard is untouched...

	QVERIFY  ( value != nullptr );
	QCOMPARE ( value->number_token (), QStringLiteral ( "1" ) );

	QVERIFY ( !document->resolve ( pointer ( QStringLiteral ( "/rows/1" ) ) )->has_member ( QStringLiteral ( "b" ) ) );

	QVERIFY ( !undo->can_undo () );                                    // ...and the cut wrote nothing to undo.
}

void TestFormView::an_empty_array_presents_a_surviving_provisional_row ()
{
	// EDITOR-12: an empty array presents WITH a provisional row already in place -- and it must survive the model reset
	// that presenting causes, whose currentChanged(invalid) would otherwise abandon it the instant it appeared.

	load ( R"({ "empty": [] })" );

	view->present ( pointer ( QStringLiteral ( "/empty" ) ), SelectionOrigin::Tree );

	QVERIFY  ( view->table_model ()->has_provisional_row () );
	QCOMPARE ( view->table_model ()->rowCount (), 1 );
	QCOMPARE ( view->table_model ()->element_count (), 0 );
}

void TestFormView::down_at_the_bottom_edge_grows_a_provisional_row ()
{
	// EDITOR-12: a Down from the last real row grows a provisional row and lands on it.

	view->present ( pointer ( QStringLiteral ( "/roles" ) ), SelectionOrigin::Tree );

	QTableView* const table = view->array_table_view ();

	table->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );   // The last real row.

	QTest::keyClick ( table, Qt::Key_Down );

	QVERIFY  ( view->table_model ()->has_provisional_row () );
	QCOMPARE ( view->table_model ()->rowCount (), 3 );
	QCOMPARE ( table->currentIndex ().row (), 2 );

	// A further Down on the still-empty provisional row does not stack another.

	QTest::keyClick ( table, Qt::Key_Down );

	QCOMPARE ( view->table_model ()->rowCount (), 3 );
}

void TestFormView::pasting_into_a_provisional_row_materializes_it ()
{
	// Paste into a provisional cell materializes the element in place, one undo step (EDITOR-11 / 12).

	view->present ( pointer ( QStringLiteral ( "/roles" ) ), SelectionOrigin::Tree );

	QTableView* const table = view->table_model () ? view->array_table_view () : nullptr;

	table->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QVERIFY ( view->cell_copy () );                                    // "admin" on the clipboard.

	table->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QTest::keyClick ( table, Qt::Key_Down );                           // Grow the provisional row.

	QVERIFY ( view->table_model ()->is_provisional_row ( table->currentIndex ().row () ) );

	QVERIFY ( view->cell_paste () );                                   // Paste materializes it (synchronous -- no editor).

	QVERIFY  ( !view->table_model ()->has_provisional_row () );
	QCOMPARE ( view->table_model ()->element_count (), 3 );
	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/roles/2" ) ) )->string_value (), QStringLiteral ( "admin" ) );
}

void TestFormView::a_typed_entry_into_a_provisional_row_materializes_after_the_event_loop_turns ()
{
	// EDITOR-12: a TYPED-ENTRY commit into a provisional cell is deferred (an editor is still open on the row about to
	// be removed), so the materialize runs on the next event-loop turn rather than inside the commit. Driving the model
	// directly (as the delegate would on commit) exercises exactly that deferral.

	view->present ( pointer ( QStringLiteral ( "/roles" ) ), SelectionOrigin::Tree );

	QTableView* const table = view->array_table_view ();

	table->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );
	QTest::keyClick ( table, Qt::Key_Down );                           // Grow the provisional row.

	const int provisionalRow = table->currentIndex ().row ();
	QVERIFY ( view->table_model ()->is_provisional_row ( provisionalRow ) );

	QVERIFY ( view->table_model ()->setData ( view->table_model ()->index ( provisionalRow, 0 ), QStringLiteral ( "viewer" ), Qt::EditRole ) );

	// Deferred: nothing has materialized yet, and the document is untouched.

	QCOMPARE ( view->table_model ()->element_count (), 2 );

	QCoreApplication::processEvents ();

	// Now it has: the element is real, the provisional row is gone, one undo step.

	QCOMPARE ( view->table_model ()->element_count (), 3 );
	QVERIFY  ( !view->table_model ()->has_provisional_row () );
	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/roles/2" ) ) )->string_value (), QStringLiteral ( "viewer" ) );
}

void TestFormView::the_object_form_declines_the_cell_clipboard ()
{
	// The cell clipboard is the array table's alone (OQ-2). On the object form the calls decline, so MainWindow falls
	// back to the node clipboard.

	view->present ( JsonPointer (), SelectionOrigin::Programmatic );   // The root object -> the form.

	QVERIFY ( !view->cell_clipboard_active () );
	QVERIFY ( !view->cell_copy () );
	QVERIFY ( !view->cell_paste () );
}

//---------------------------------------------------------------------------------------------------------------------
// Wrap strings (SET-05)
//
// THE OBJECT FORM'S ALONE (revised 2026-07-27). It applied to both faces first, on the reasoning that one setting
// should mean one thing -- but the two faces are not one thing: a form field is a labelled paragraph and wraps the way
// a form should, while a table cell is a spreadsheet cell, and rows of varying height break the one property a
// spreadsheet is read for. The asymmetry is the requirement, so it is asserted rather than assumed.
//---------------------------------------------------------------------------------------------------------------------

QPlainTextEdit* TestFormView::open_wrapped_editor_in ( QTableView* gridView, int row, int column ) const
{
	gridView->setCurrentIndex ( gridView->model ()->index ( row, column ) );
	gridView->edit ( gridView->currentIndex () );

	return gridView->viewport ()->findChild<QPlainTextEdit*> ();
}

void TestFormView::each_wrapped_row_is_sized_to_its_own_content ()
{
	// The correction that mattered most in review. Rows were UNIFORM at first -- one taller height for all of them --
	// and the effect is plainly wrong to look at: a form of short fields beside one paragraph became a page of white
	// space. A row is now as tall as ITS OWN value and no taller.

	load ( R"({ "short": "short",
	            "long":  "a very much longer value that will certainly want several lines of its own once it is wrapped to the width of a column, and then several more after that" })" );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	const int unwrappedHeight = formView->rowHeight ( 0 );

	QCOMPARE ( formView->rowHeight ( 0 ), formView->rowHeight ( 1 ) );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );

	view->resize ( 420, 400 );

	QCoreApplication::processEvents ();

	QVERIFY2 ( formView->rowHeight ( 1 ) > formView->rowHeight ( 0 ),
	           "The long value's row is no taller than the short one's -- rows are still uniform" );

	// And the SHORT row did not grow. That is the half the user reported: an unrelated field must not be dragged to
	// its neighbour's height.

	QCOMPARE ( formView->rowHeight ( 0 ), unwrappedHeight );

	// Elision is off, because a wrapped cell ending in an ellipsis on its first line is the one shape that reads as
	// broken rather than as either.

	QCOMPARE ( static_cast<int> ( formView->textElideMode () ), static_cast<int> ( Qt::ElideNone ) );
	QVERIFY ( formView->wordWrap () );

	// It takes effect at once and reverses at once -- no restart, like every other setting in the dialog.

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, false );

	QCoreApplication::processEvents ();

	QCOMPARE ( formView->rowHeight ( 0 ), unwrappedHeight );
	QCOMPARE ( formView->rowHeight ( 1 ), unwrappedHeight );
	QCOMPARE ( static_cast<int> ( formView->textElideMode () ), static_cast<int> ( Qt::ElideRight ) );
}

void TestFormView::a_committed_value_re_measures_its_row ()
{
	// QAbstractItemView::dataChanged repaints the cell and does NOT ask a ResizeToContents vertical header to recompute
	// its sections, so a row that had held one line kept its one-line height after a commit turned the value into a
	// paragraph -- the tail of what the user had just typed clipped, with no cell scroll bar to reach it, until some
	// unrelated event happened to re-measure the rows (2026-07-28 review).
	//
	// Stated as an OUTCOME rather than as a claim about who produces it: this case does NOT fail with the explicit
	// re-measure disconnected, because a ResizeToContents QHeaderView also recomputes lazily on the next size query, so
	// offscreen the two paths are indistinguishable. It is a behaviour pin, not a verified-failing regression -- the
	// clipping was seen on screen, where the repaint happens before any such query.

	load ( R"({ "note": "short" })" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );

	view->present ( JsonPointer (), SelectionOrigin::Tree );
	view->resize ( 420, 400 );

	QCoreApplication::processEvents ();

	QTableView* const formView = view->object_form_view ();

	const int beforeCommit = formView->rowHeight ( 0 );

	// Committed through the MODEL, which is the route every writer shares -- an editor commit, an undo, a paste, and an
	// edit made in another view all arrive as dataChanged.

	QVERIFY ( formView->model ()->setData
	(
		formView->model ()->index ( 0, JsonFormModel::VALUE_COLUMN ),
		QStringLiteral ( "a very much longer value that will certainly want several lines of its own once it is wrapped "
		                 "to the width of this column, and then several more lines after that" ),
		Qt::EditRole
	) );

	QVERIFY2 ( formView->rowHeight ( 0 ) > beforeCommit,
	           qPrintable ( QStringLiteral ( "row still %1 px after the commit (was %2)" )
	                        .arg ( formView->rowHeight ( 0 ) ).arg ( beforeCommit ) ) );

	// And it shrinks back, so the rule is "re-measure", not "grow".

	undo->undo ();

	QCOMPARE ( formView->rowHeight ( 0 ), beforeCommit );
}

void TestFormView::an_unwrapped_field_keeps_its_height_beside_a_wrapped_one ()
{
	// The reported issue in the shape it was reported in: one long field among short ones. Every short field must keep
	// a one-line height whatever its neighbour does.

	load ( R"({ "label1": "Some text.",
	            "label2": "Some text that has so many lines it needs to be wrapped over many lines indeed, enough that it certainly occupies more than one.",
	            "label3": "Hello World!" })" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	view->resize ( 420, 400 );

	QCoreApplication::processEvents ();

	QTableView* const formView = view->object_form_view ();

	QVERIFY2 ( formView->rowHeight ( 1 ) > formView->rowHeight ( 0 ), "The long field did not grow" );

	// And the two SHORT fields either side of it are untouched and equal -- the reported issue in one line.

	QCOMPARE ( formView->rowHeight ( 0 ), formView->rowHeight ( 2 ) );

	// THE GAP IS UNIFORM. A wrapped row exceeds a one-line row by whole LINES and nothing else, so the space between
	// any two fields is the same whether either of them wrapped. Getting this wrong is a pixel or two -- invisible on
	// its own, and obvious in a column where every other gap is the other value.

	const int lineHeight = formView->fontMetrics ().height ();

	QCOMPARE ( ( formView->rowHeight ( 1 ) - formView->rowHeight ( 0 ) ) % lineHeight, 0 );
}

void TestFormView::the_array_table_never_wraps ()
{
	// The asymmetry, asserted rather than described. A table cell is a spreadsheet cell: rows of varying height break
	// the one property a spreadsheet is read for, which is that a row is a row. So the array table keeps one-line
	// elided cells and their tooltips however the setting is left.

	load ( "[{\"note\":\"short\"},{\"note\":\"a very much longer value that would certainly want several lines of its own if this grid wrapped, which it does not\"}]" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	view->resize ( 420, 400 );

	QCoreApplication::processEvents ();

	QTableView* const tableView = view->array_table_view ();

	QVERIFY2 ( !tableView->wordWrap (), "The array table wrapped" );
	QCOMPARE ( static_cast<int> ( tableView->textElideMode () ), static_cast<int> ( Qt::ElideRight ) );
	QCOMPARE ( tableView->rowHeight ( 0 ), tableView->rowHeight ( 1 ) );

	// And its editor stays single-line, so the cell keeps its validator-backed commit path unchanged.

	tableView->setCurrentIndex ( tableView->model ()->index ( 1, 0 ) );
	tableView->edit ( tableView->currentIndex () );

	QVERIFY ( tableView->viewport ()->findChild<QPlainTextEdit*> () == nullptr );
	QVERIFY ( open_editor_in ( tableView ) != nullptr );
}

void TestFormView::an_open_editor_announces_the_cell_it_edits ()
{
	// NFR-05. An editor is a bare widget parented into a viewport, so it inherits no name from the cell it covers --
	// and it is the control a user spends most of their time inside. All three cases are checked, because the three
	// answer from different places: a form value from the row's key, a form key from a fixed label, a table cell from
	// the column's header.
	//
	// EACH ONE LOADS ITS OWN DOCUMENT rather than opening a second editor in the same grid. open_editor_in returns the
	// FIRST editor under the viewport, and an editor closed with Escape survives until the loop turns (Qt closes it
	// with deleteLater) -- so a shared grid would hand the previous editor back and the case would quietly assert
	// against the wrong widget.

	load ( R"({ "alpha": "one", "beta": "two" })" );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formGrid = view->object_form_view ();

	formGrid->setCurrentIndex ( view->form_model ()->index ( 1, JsonFormModel::VALUE_COLUMN ) );
	formGrid->edit ( formGrid->currentIndex () );

	QWidget* const valueEditor = open_editor_in ( formGrid );

	QVERIFY ( valueEditor != nullptr );
	QCOMPARE ( valueEditor->accessibleName (), QStringLiteral ( "beta" ) );
}

void TestFormView::a_key_editor_announces_the_column_not_the_key ()
{
	// The key cell is NOT named after its own text: that text is the thing being replaced, so announcing it would say
	// the old key while the user types the new one (NFR-05).

	load ( R"({ "alpha": "one", "beta": "two" })" );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formGrid = view->object_form_view ();

	formGrid->setCurrentIndex ( view->form_model ()->index ( 1, JsonFormModel::KEY_COLUMN ) );
	formGrid->edit ( formGrid->currentIndex () );

	QWidget* const keyEditor = open_editor_in ( formGrid );

	QVERIFY ( keyEditor != nullptr );
	QVERIFY2 ( !keyEditor->accessibleName ().isEmpty (), "The key editor announces nothing" );
	QVERIFY2 ( keyEditor->accessibleName () != QStringLiteral ( "beta" ), "The key editor announced the key it is replacing" );
}

void TestFormView::a_table_cell_editor_announces_its_column ()
{
	// The array table names a cell after its COLUMN, which is the label shown above it.

	load ( R"([ { "gamma": 1 }, { "gamma": 2 } ])" );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const tableGrid = view->array_table_view ();

	tableGrid->setCurrentIndex ( tableGrid->model ()->index ( 0, 0 ) );
	tableGrid->edit ( tableGrid->currentIndex () );

	QWidget* const cellEditor = open_editor_in ( tableGrid );

	QVERIFY ( cellEditor != nullptr );
	QCOMPARE ( cellEditor->accessibleName (), QStringLiteral ( "gamma" ) );
}

void TestFormView::both_grids_carry_accessible_names ()
{
	// NFR-05, and two names rather than one: the Form View's two faces are two different things to be told you are in
	// -- a labelled form whose key column names each row, and a spreadsheet whose header row names each column.

	QVERIFY2 ( !view->object_form_view ()->accessibleName ().isEmpty (), "The object form has no accessible name" );
	QVERIFY2 ( !view->array_table_view ()->accessibleName ().isEmpty (), "The array table has no accessible name" );

	QVERIFY ( view->object_form_view ()->accessibleName () != view->array_table_view ()->accessibleName () );
}

void TestFormView::wrapping_opens_a_string_in_a_multi_line_editor ()
{
	// A value shown over four wrapped lines and then edited in a one-line field is the same value twice in two shapes,
	// so the editor follows the display.

	load ( R"({ "note": "a value" })" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QPlainTextEdit* const wrappedEditor = open_wrapped_editor_in ( view->object_form_view (), 0, JsonFormModel::VALUE_COLUMN );

	QVERIFY2 ( wrappedEditor != nullptr, "A string did not open in a multi-line editor while wrapping was on" );
	QCOMPARE ( wrappedEditor->toPlainText (), QStringLiteral ( "a value" ) );

	// Neither scroll bar: the row holds the whole value, so one could only ever say the row got its height wrong.

	QCOMPARE ( static_cast<int> ( wrappedEditor->verticalScrollBarPolicy () ),   static_cast<int> ( Qt::ScrollBarAlwaysOff ) );
	QCOMPARE ( static_cast<int> ( wrappedEditor->horizontalScrollBarPolicy () ), static_cast<int> ( Qt::ScrollBarAlwaysOff ) );
}

void TestFormView::a_number_keeps_its_single_line_editor_and_its_validator ()
{
	// The containment that makes the multi-line editor safe: it is scoped to STRINGS. A number keeps its QLineEdit and
	// therefore keeps JsonNumberValidator, which is what buys VAL-03's keep-the-caret-in-the-cell behaviour from Qt's
	// own plumbing -- QPlainTextEdit has no validator to give it.

	load ( R"({ "tag": 7 })" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( formView->model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );
	formView->edit ( formView->currentIndex () );

	QVERIFY ( formView->viewport ()->findChild<QPlainTextEdit*> () == nullptr );

	QLineEdit* const numberEditor = open_editor_in ( formView );

	QVERIFY ( numberEditor != nullptr );
	QVERIFY2 ( numberEditor->validator () != nullptr, "The number editor lost its validator" );
}

//---------------------------------------------------------------------------------------------------------------------
// The arrow keys inside a wrapped string editor (SET-05)
//
// The one place the grid gives its arrow keys away. A value shown over six lines has to be navigable line by line, and
// a grid that moved to the next field on the first Down would make the second line of a paragraph unreachable by
// keyboard -- so inside a MULTI-LINE editor Up / Down belong to the caret and CTRL is the way out. Shift belongs to
// the editor, which extends its selection a line at a time (corrected 2026-07-28; it was Shift, wrongly).
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::the_arrows_move_the_caret_inside_a_wrapped_editor ()
{
	load ( R"({ "note": "line one\nline two\nline three", "second": "second field" })" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );

	// Decoded, so the editor holds real line breaks for the caret to move between. Under the default ESCAPED notation
	// the same value is ONE line carrying a literal backslash-n, Down would have nowhere to go, and this case would
	// assert nothing while appearing to fail (SET-03).

	settings->set_string ( settings_keys::STRING_DISPLAY, settings_values::STRING_DISPLAY_DECODED );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	QPlainTextEdit* const editor = open_wrapped_editor_in ( formView, 0, JsonFormModel::VALUE_COLUMN );

	QVERIFY ( editor != nullptr );
	QCOMPARE ( editor->blockCount (), 3 );

	editor->moveCursor ( QTextCursor::Start );

	const int startingRow = formView->currentIndex ().row ();

	QTest::keyClick ( editor, Qt::Key_Down );

	// The caret moved a line; the grid did not move a field, and the editor is still open.

	QCOMPARE ( editor->textCursor ().blockNumber (), 1 );
	QCOMPARE ( formView->currentIndex ().row (), startingRow );
	QVERIFY ( formView->viewport ()->findChild<QPlainTextEdit*> () != nullptr );

	QTest::keyClick ( editor, Qt::Key_Up );

	QCOMPARE ( editor->textCursor ().blockNumber (), 0 );
	QCOMPARE ( formView->currentIndex ().row (), startingRow );
}

void TestFormView::shift_and_an_arrow_extends_the_selection_in_a_wrapped_editor ()
{
	// Shift+Up / Shift+Down belong to QPlainTextEdit and always did -- selecting a line at a time is what they mean in
	// every other text box, and taking them for grid movement (as this editor briefly did) left a multi-line value with
	// no way to select vertically from the keyboard at all.

	load ( R"({ "note": "line one\nline two", "second": "second field" })" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );
	settings->set_string ( settings_keys::STRING_DISPLAY, settings_values::STRING_DISPLAY_DECODED );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	QPlainTextEdit* const editor = open_wrapped_editor_in ( formView, 0, JsonFormModel::VALUE_COLUMN );

	QVERIFY ( editor != nullptr );

	editor->moveCursor ( QTextCursor::Start );

	const int startingRow = formView->currentIndex ().row ();

	QTest::keyClick ( editor, Qt::Key_Down, Qt::ShiftModifier );

	// A line is now selected, the field did NOT change, and the editor is still open.

	QVERIFY  ( !editor->textCursor ().selectedText ().isEmpty () );
	QCOMPARE ( editor->textCursor ().blockNumber (), 1 );
	QCOMPARE ( formView->currentIndex ().row (), startingRow );
	QVERIFY  ( formView->viewport ()->findChild<QPlainTextEdit*> () != nullptr );
}

void TestFormView::control_and_an_arrow_leaves_a_wrapped_editor ()
{
	// Ctrl is the escape now. It is free to be: a stock QPlainTextEdit does nothing whatever with Ctrl+Up / Ctrl+Down
	// -- no caret move, no scroll -- so this takes a keystroke from the editor that the editor was not using.

	load ( R"({ "note": "line one\nline two", "second": "second field" })" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );
	settings->set_string ( settings_keys::STRING_DISPLAY, settings_values::STRING_DISPLAY_DECODED );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	QPlainTextEdit* const editor = open_wrapped_editor_in ( formView, 0, JsonFormModel::VALUE_COLUMN );

	QVERIFY ( editor != nullptr );

	editor->moveCursor ( QTextCursor::Start );

	QTest::keyClick ( editor, Qt::Key_Down, Qt::ControlModifier );

	// Committed and moved a FIELD, not a line -- and the editor closed behind it.

	QCOMPARE ( formView->currentIndex ().row (), 1 );

	// QAbstractItemView releases the editor with deleteLater, and processEvents does NOT run DeferredDelete -- the
	// posted events have to be sent explicitly.

	QCoreApplication::sendPostedEvents ( nullptr, QEvent::DeferredDelete );

	QVERIFY ( formView->viewport ()->findChild<QPlainTextEdit*> () == nullptr );

	// And back up the other way.

	QPlainTextEdit* const second = open_wrapped_editor_in ( formView, 1, JsonFormModel::VALUE_COLUMN );

	QVERIFY ( second != nullptr );

	QTest::keyClick ( second, Qt::Key_Up, Qt::ControlModifier );

	QCOMPARE ( formView->currentIndex ().row (), 0 );
}

void TestFormView::control_and_an_arrow_leaves_a_single_line_editor ()
{
	// The rule is stated for every editor kind, not just the one that needed it: a user who learns "Ctrl+Down leaves
	// the field" in a wrapped value must not find it dead in an unwrapped one.

	load ( R"({ "first": "first", "second": "second" })" );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Return );

	QLineEdit* const editor = open_editor_in ( formView );

	QVERIFY ( editor != nullptr );

	QTest::keyClick ( editor, Qt::Key_Down, Qt::ControlModifier );

	QCOMPARE ( formView->currentIndex ().row (), 1 );
}

void TestFormView::control_and_an_arrow_leaves_a_boolean_editor ()
{
	// A boolean edits in a combo, which owns plain Up / Down to change its value -- so before this it had no arrow
	// escape at all. Ctrl now leaves it like any other editor, and the combo keeps the unmodified pair.

	load ( R"({ "flag": true, "second": "second" })" );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Return );

	QComboBox* const editor = formView->viewport ()->findChild<QComboBox*> ();

	QVERIFY ( editor != nullptr );

	QTest::keyClick ( editor, Qt::Key_Down, Qt::ControlModifier );

	QCOMPARE ( formView->currentIndex ().row (), 1 );
}

void TestFormView::an_unwrapped_string_editor_keeps_the_ordinary_arrow_keys ()
{
	// The exception is the WRAPPED editor's alone. With the setting off a string edits in a QLineEdit and Down commits
	// and moves, exactly as it always has (EDITOR-02) -- there is no second line for it to belong to.

	load ( R"({ "first": "first", "second": "second" })" );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( formView->model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );
	formView->edit ( formView->currentIndex () );

	QLineEdit* const editor = open_editor_in ( formView );

	QVERIFY ( editor != nullptr );

	QTest::keyClick ( editor, Qt::Key_Down );

	QCOMPARE ( formView->currentIndex ().row (), 1 );
}

void TestFormView::control_enter_inserts_a_line_break ()
{
	// Enter still commits, so the line break needs a key of its own (spec section 4). QPlainTextEdit answers only the
	// UNMODIFIED Enter, so without the delegate handling this the key would do nothing at all.

	load ( R"({ "note": "one" })" );

	settings->set_bool ( settings_keys::FORM_WRAP_STRINGS, true );

	view->present ( JsonPointer (), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	QPlainTextEdit* const editor = open_wrapped_editor_in ( formView, 0, JsonFormModel::VALUE_COLUMN );

	QVERIFY ( editor != nullptr );

	editor->moveCursor ( QTextCursor::End );

	QTest::keyClick ( editor, Qt::Key_Return, Qt::ControlModifier );

	QCOMPARE ( editor->blockCount (), 2 );

	// The editor is still open: a line break is an edit, not a commit.

	QVERIFY ( formView->viewport ()->findChild<QPlainTextEdit*> () != nullptr );
}

//=====================================================================================================================
// Left / Right inside an open editor (EDITOR-02 / EDITOR-03)
//
// While an editor is OPEN the horizontal arrows belong to the text, in both grids. The array table used to take them as
// spreadsheet navigation -- commit and move one cell -- which made a mistyped character in the middle of a value
// unreachable without the mouse: the only way back was to retype the value or Esc and start again.
//
// The editor opens with its text SELECTED, so the first press has two jobs, and both come free from the text widget
// once the delegate stops swallowing the key: collapse the selection to the edge the arrow points at, and put the caret
// there.
//=====================================================================================================================

void TestFormView::left_and_right_move_the_caret_inside_a_table_cell_editor ()
{
	// Column 1 ("status") is deliberately a MIDDLE column. Written on column 0 this test would pass against a
	// navigating build simply by clamping at the left edge, which is a test that agrees with both implementations.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 1 ) );

	QTest::keyClick ( tableView, Qt::Key_Return );

	QLineEdit* const editor = open_editor_in ( tableView );

	QVERIFY  ( editor != nullptr );
	QCOMPARE ( editor->text (), QStringLiteral ( "in-progress" ) );
	QVERIFY  ( editor->hasSelectedText () );

	QTest::keyClick ( editor, Qt::Key_Left );

	QVERIFY ( !editor->hasSelectedText () );

	// Where the caret lands is QLineEdit's own convention, measured rather than assumed: it clears the selection and
	// then moves ORDINARILY from the caret, which selectAll leaves at the END of the text. So Left from a fully
	// selected value gives length - 1, not 0 -- Qt does not collapse to the selection's near edge.

	QCOMPARE ( editor->cursorPosition (), editor->text ().length () - 1 );

	// The cell did not move and the editor is still open -- the key went to the text, not to the grid.

	QCOMPARE ( tableView->currentIndex ().column (), 1 );
	QVERIFY  ( open_editor_in ( tableView ) != nullptr );

	QTest::keyClick ( editor, Qt::Key_Right );

	QCOMPARE ( editor->cursorPosition (), editor->text ().length () );
	QCOMPARE ( tableView->currentIndex ().column (), 1 );
}

void TestFormView::left_and_right_move_the_caret_inside_a_form_value_editor ()
{
	// The form has always behaved this way; pinning it is what makes the two grids' parity a stated property rather
	// than a coincidence that the next keyboard change could quietly break on one side.

	view->present ( pointer ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );

	QTableView* const formView = view->object_form_view ();

	formView->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( formView, Qt::Key_Return );

	QLineEdit* const editor = open_editor_in ( formView );

	QVERIFY ( editor != nullptr );
	QVERIFY ( editor->hasSelectedText () );

	QTest::keyClick ( editor, Qt::Key_Left );

	QVERIFY  ( !editor->hasSelectedText () );
	QCOMPARE ( editor->cursorPosition (), editor->text ().length () - 1 );
	QCOMPARE ( formView->currentIndex ().column (), int ( JsonFormModel::VALUE_COLUMN ) );
}

void TestFormView::an_arrow_at_the_end_of_a_cell_editor_does_not_leave_it ()
{
	// The boundary the spreadsheet reading would have kept: a Right with the caret already at the end of the text does
	// NOT fall through to the next cell. An editor is a text box for as long as it is open, edges included.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 1 ) );

	QTest::keyClick ( tableView, Qt::Key_Return );

	QLineEdit* const editor = open_editor_in ( tableView );

	QVERIFY ( editor != nullptr );

	editor->setCursorPosition ( editor->text ().length () );

	QTest::keyClick ( editor, Qt::Key_Right );

	QCOMPARE ( editor->cursorPosition (), editor->text ().length () );
	QCOMPARE ( tableView->currentIndex ().column (), 1 );
	QVERIFY  ( open_editor_in ( tableView ) != nullptr );

	editor->setCursorPosition ( 0 );

	QTest::keyClick ( editor, Qt::Key_Left );

	QCOMPARE ( editor->cursorPosition (), 0 );
	QCOMPARE ( tableView->currentIndex ().column (), 1 );
	QVERIFY  ( open_editor_in ( tableView ) != nullptr );
}

//---------------------------------------------------------------------------------------------------------------------
// Printing (FILE-12)
//
// What is printed is what is SHOWN: the content is read back through Qt::DisplayRole, the very role the grid paints
// from, so the container placeholders, SET-03's string notation and EDITOR-03's ragged key union all reach the paper
// without any of them being restated for the printer. These cases pin that the reading is faithful, and that the one
// thing on screen which is not part of the document -- the provisional row -- stays off the page.
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::the_object_form_prints_its_rows_without_a_header ()
{
	view->present ( pointer ( QStringLiteral ( "/projects/0" ) ), SelectionOrigin::Tree );

	const PrintContent content = view->print_content ( 0 );

	QCOMPARE ( content.kind,    PrintContent::Kind::Table );
	QCOMPARE ( content.subject, QStringLiteral ( "/projects/0" ) );

	// The key / value columns carry no labels on screen (EDITOR-02), so an invented "Key" / "Value" row on paper would
	// be the printer saying something the view never did.

	QVERIFY2 ( content.headers.isEmpty (), qPrintable ( content.headers.join ( QLatin1Char ( ',' ) ) ) );

	QCOMPARE ( content.rows.size (), 3 );

	QCOMPARE ( content.rows.at ( 0 ), QStringList ( { QStringLiteral ( "name" ), QStringLiteral ( "JSON Editor" ) } ) );
	QCOMPARE ( content.rows.at ( 1 ).first (), QStringLiteral ( "status" ) );

	// The nested array prints the same one-slot placeholder the grid shows, because that text is now a shared
	// definition rather than a coincidence (vje_core/services/value_placeholders.hpp).

	QCOMPARE ( content.rows.at ( 2 ), QStringList ( { QStringLiteral ( "tags" ), QStringLiteral ( "[...]" ) } ) );
}

void TestFormView::the_array_table_prints_its_column_keys_as_headers ()
{
	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Tree );

	const PrintContent content = view->print_content ( 0 );

	QCOMPARE ( content.kind, PrintContent::Kind::Table );

	QCOMPARE
	(
		content.headers,
		QStringList ( { QStringLiteral ( "name" ), QStringLiteral ( "status" ), QStringLiteral ( "tags" ) } )
	);

	QCOMPARE ( content.rows.size (), 2 );
	QCOMPARE ( content.rows.at ( 0 ).size (), 3 );
	QCOMPARE ( content.rows.at ( 0 ).at ( 0 ), QStringLiteral ( "JSON Editor" ) );
}

void TestFormView::a_ragged_element_prints_an_empty_cell_under_the_column_it_lacks ()
{
	// EDITOR-03's key union. A ragged element must print its absent member as an EMPTY cell in the right column, not
	// as a short row -- a short row slides its remaining values left under the wrong headings, which is wrong rather
	// than merely absent.

	load ( R"({ "rows": [ { "a": 1, "b": 2 }, { "a": 3 } ] })" );

	view->present ( pointer ( QStringLiteral ( "/rows" ) ), SelectionOrigin::Tree );

	const PrintContent content = view->print_content ( 0 );

	QCOMPARE ( content.headers, QStringList ( { QStringLiteral ( "a" ), QStringLiteral ( "b" ) } ) );

	QCOMPARE ( content.rows.size (), 2 );
	QCOMPARE ( content.rows.at ( 1 ).size (), 2 );
	QCOMPARE ( content.rows.at ( 1 ).at ( 0 ), QStringLiteral ( "3" ) );
	QVERIFY2 ( content.rows.at ( 1 ).at ( 1 ).isEmpty (), qPrintable ( content.rows.at ( 1 ).at ( 1 ) ) );
}

void TestFormView::the_provisional_row_is_not_printed ()
{
	// EDITOR-12's trailing row is a view-only affordance for GROWING an array. It is not in the document and there is
	// nothing in it, so printing it would put a blank row on the page of every array the user has arrowed to the
	// bottom of.

	load ( R"({ "empty": [] })" );

	view->present ( pointer ( QStringLiteral ( "/empty" ) ), SelectionOrigin::Tree );

	QVERIFY  ( view->table_model ()->has_provisional_row () );
	QCOMPARE ( view->table_model ()->rowCount (), 1 );

	QVERIFY2 ( view->print_content ( 0 ).rows.isEmpty (),
	           qPrintable ( QStringLiteral ( "%1 row(s) printed" ).arg ( view->print_content ( 0 ).rows.size () ) ) );
}

void TestFormView::a_view_presenting_nothing_prints_nothing ()
{
	view->present ( pointer ( QStringLiteral ( "/nope/at/all" ) ), SelectionOrigin::Tree );

	QVERIFY ( view->print_content ( 0 ).is_empty () );
}

QTEST_MAIN ( TestFormView )

#include "tst_form_view.moc"
