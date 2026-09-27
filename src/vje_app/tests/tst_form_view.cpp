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
#include "models/cell_presentation.hpp"
#include "models/JsonTableModel.hpp"
#include "services/ClipboardService.hpp"
#include "services/SelectionService.hpp"
#include "views/FormGridController.hpp"
#include "views/FormView.hpp"
#include "views/GridHeaderView.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/services/JsonSerializer.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
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
	void the_object_form_owns_the_field_clipboard ();
	void the_empty_state_still_declines_the_clipboard ();
	void copying_a_key_cell_writes_plain_text_only ();

	// The object form's provisional row (EDITOR-15).

	void the_down_arrow_grows_a_provisional_row ();
	void an_empty_object_lands_on_the_provisional_key ();
	void the_provisional_value_cell_explains_itself ();

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

	// The interactive header (EDITOR-16 / EDITOR-17 / EDIT-15).

	void a_column_header_click_selects_the_column ();
	void a_row_index_click_selects_the_row ();
	void only_one_header_selection_is_live_at_a_time ();
	void a_header_selection_unselects_the_cell_that_was_selected ();
	void a_cell_move_ends_a_header_selection ();
	void a_click_on_the_current_cell_ends_a_header_selection ();
	void the_keyboard_selects_the_current_row_and_column ();
	void shift_space_does_not_start_editing_the_cell ();
	void the_provisional_row_is_selectable_but_has_nothing_to_copy ();
	void the_cells_of_a_selected_column_are_painted_as_selected ();

	void the_sort_zone_is_bounded_and_spans_the_headers_height ();
	void a_click_on_the_sort_marker_sorts_without_selecting ();
	void a_click_one_pixel_beside_the_marker_selects_without_sorting ();
	void sorting_toggles_ascending_then_descending ();
	void sorting_a_different_column_starts_ascending_again ();
	void the_marker_is_cleared_by_a_change_to_the_array ();
	void a_re_present_clears_both_the_marker_and_the_selection ();
	void a_narrow_column_carries_no_marker_and_stays_selectable ();

	void a_short_arrays_index_column_is_as_wide_as_a_two_digit_one ();
	void a_three_digit_array_widens_past_the_floor ();
	void a_double_click_on_a_row_index_drills_into_the_element ();

	// Row height, and the two dials that now set it (config::form::OBJECT_ / ARRAY_ROW_VERTICAL_PADDING).

	void the_array_tables_rows_are_sized_by_the_array_dial ();
	void the_object_forms_rows_are_sized_by_the_object_dial ();
	void the_array_tables_rows_are_fixed_and_floored_at_the_dials_answer ();

	// The row and column clipboard (EDITOR-18). Since 2026-09-23 Paste INSERTS, and the cases here that assert an
	// OVERWRITE reach it through Paste Over -- which is exactly the overwrite Paste was, moved to its own command.

	void a_copied_row_pastes_onto_another_row ();
	void a_copied_column_pastes_onto_another_column ();
	void a_column_cannot_be_pasted_onto_a_row ();
	void a_short_source_fills_what_it_covers_and_leaves_the_rest ();
	void a_longer_source_grows_the_array ();
	void a_column_pasted_into_an_empty_array_creates_bare_elements ();
	void a_scalar_arrays_column_copies_under_the_arrays_own_name ();
	void an_appended_element_carries_null_for_every_other_column ();
	void growth_writes_the_target_columns_key_not_the_sources ();
	void a_scalar_column_grows_a_scalar_array_with_bare_values ();
	void a_row_pasted_onto_the_provisional_row_appends_an_element ();
	void a_wider_row_adds_its_columns_across_the_whole_array ();
	void an_absent_source_cell_leaves_its_target_untouched ();
	void a_column_paste_is_one_undo_step ();
	void a_type_mismatch_asks_once_and_pastes_on_confirm ();
	void declining_the_type_override_abandons_the_whole_paste ();
	void a_cell_paste_refuses_a_table_selection_rather_than_stringifying_it ();
	void deleting_a_row_removes_the_element ();
	void deleting_a_column_removes_the_member_from_every_element_in_one_step ();
	void a_ragged_column_delete_removes_only_the_members_that_exist ();
	void a_column_delete_is_refused_on_a_scalar_array ();

	// Clear Contents and the column header's menu (EDITOR-19).

	void clearing_a_column_nulls_its_cells_and_keeps_the_column ();
	void clearing_leaves_an_absent_member_absent ();
	void clearing_an_already_empty_column_is_unchanged ();
	void a_scalar_column_clears_where_it_cannot_be_deleted ();
	void the_column_menu_offers_the_clipboard_then_the_column_commands ();
	void the_column_menus_clipboard_items_act_on_the_column ();
	void renaming_a_column_renames_the_member_on_every_element ();
	void a_rename_that_would_collide_is_refused_whole ();
	void a_value_cell_right_click_offers_the_field_clipboard ();
	void a_right_click_routes_by_column_and_by_face ();
	void a_column_name_is_left_aligned ();
	void an_auto_fit_leaves_room_for_the_sort_zone ();
	void a_column_that_appears_is_sized_to_its_name ();
	void the_name_zone_and_the_row_index_raise_the_menu_and_the_sort_zone_does_not ();
	void opening_the_column_menu_selects_the_column ();
	void cutting_a_row_places_what_copy_would_and_removes_the_element ();
	void a_header_selection_takes_precedence_over_the_current_cell ();
	void the_provisional_row_is_not_among_a_columns_cells ();

	// The Delete key on a selected row or column (EDITOR-18), and what the window's enablement hears.

	void the_delete_key_removes_a_selected_row ();
	void the_delete_key_removes_a_selected_column ();
	void the_delete_key_leaves_a_current_cell_alone ();
	void a_header_selection_change_is_announced ();

	// Clear Contents on a row, and the row index's menu (EDITOR-22).

	void clearing_a_row_nulls_its_cells_and_keeps_the_row ();
	void clearing_a_row_leaves_an_absent_member_absent ();
	void clearing_an_already_empty_row_is_unchanged ();
	void a_scalar_row_clears_to_a_null_element ();
	void the_provisional_row_cannot_be_cleared ();
	void the_row_menu_offers_the_clipboard_then_the_row_commands ();
	void the_row_menus_clipboard_items_act_on_the_row ();
	void opening_the_row_menu_selects_the_row ();

	// Paste INSERTS; Paste Over overwrites (EDITOR-18, revised 2026-09-23).

	void a_pasted_row_is_inserted_in_front_of_the_selected_row ();
	void a_pasted_row_is_mapped_by_key ();
	void a_value_row_goes_in_bare_and_an_object_row_as_an_object ();
	void an_inserted_row_is_shape_checked_against_the_array ();
	void a_pasted_column_is_inserted_in_front_of_the_selected_column ();
	void a_pasted_column_whose_name_is_taken_arrives_as_a_copy ();
	void ctrl_shift_v_pastes_over_a_selection_and_nothing_else ();

	// The selection follows a removal (EDITOR-18, revised 2026-09-23).

	void deleting_rows_selects_each_row_that_takes_the_place ();
	void deleting_columns_selects_each_column_that_takes_the_place ();
	void the_placeholder_row_is_no_replacement ();
	void cutting_selects_what_takes_the_place_too ();

	// An undo that brings a row back shows it (UNDO-05).

	void undoing_a_delete_below_the_view_scrolls_the_restored_row_into_view ();
	void a_restored_row_already_on_screen_does_not_move_the_view ();
	void an_ordinary_insert_does_not_scroll_but_its_undo_and_redo_do ();

	// A row pasted onto a row the user has scrolled away from is shown (EDITOR-18, revised 2026-09-24).

	void pasting_onto_a_row_scrolled_out_of_view_scrolls_to_it ();
	void pasting_over_a_row_scrolled_out_of_view_scrolls_to_it ();
	void a_paste_onto_a_row_on_screen_does_not_move_the_view ();
	void pasting_over_the_placeholder_row_out_of_view_scrolls_to_the_new_row ();

	// An undo shows the row it acted on, whatever it did to it (UNDO-05, revised 2026-09-24).

	void undoing_a_paste_below_the_view_scrolls_to_where_the_row_was ();
	void undoing_a_paste_over_below_the_view_scrolls_to_the_row ();
	void undoing_a_cell_edit_below_the_view_scrolls_to_its_row ();
	void undoing_the_last_row_scrolls_to_the_new_last_row ();
	void an_undo_across_many_rows_does_not_scroll ();
	void a_column_pasted_over_after_a_sort_undoes_back_through_the_sort ();
	void lines_from_another_application_fill_down_from_the_current_cell ();
	void a_spreadsheet_block_fills_rows_and_columns_and_grows_the_array ();
	void a_block_wider_than_the_columns_left_is_refused_whole ();
	void an_empty_field_leaves_its_cell_and_holds_its_place ();
	void a_json_value_over_several_lines_still_pastes_as_one_value ();
	void vje_s_own_copy_of_a_multi_line_string_is_not_read_as_a_block ();
	void a_block_pasted_on_the_placeholder_row_appends_its_rows ();
	void a_blank_line_past_the_end_still_holds_its_row ();
	void a_block_with_the_wrong_types_asks_once ();
	void row_numbers_are_right_aligned_a_space_clear_of_the_edge ();

private:

	void build_view              ( const QString& editOnValue );
	void load                    ( const char* text );
	void connect_selection_loop  ();

	QLineEdit*      open_editor_in         ( QTableView* gridView ) const;
	QPlainTextEdit* open_wrapped_editor_in ( QTableView* gridView, int row, int column ) const;

	// The interactive header's geometry helpers. sort_zone asks the header itself rather than recomputing the
	// rectangle from the dials, because a rect derived twice is a rect that can differ twice -- and the whole of
	// EDITOR-03's select-versus-sort resolution rests on the paint and the hit test sharing exactly one.

	QRect  sort_zone          ( int section ) const;
	void   click_header        ( QHeaderView* header, const QPoint& position ) const;
	void   send_context_menu   ( QHeaderView* header, const QPoint& position ) const;
	QPoint section_body_point  ( QHeaderView* header, int section ) const;
	QPoint row_index_point     ( int row ) const;

	// EDITOR-18's two selections, made through the real header click so the routing is exercised rather than the
	// controller's setter being called directly.

	// VAL-05: what the controller ANNOUNCED, which for Clear Contents is the only place Unchanged is observable --
	// the command returns a bool, and a no-op leaves neither the document nor the undo stack changed, so without
	// this the "already empty" case could not fail against a build that reported Applied (lesson D20).
	//
	// A lambda rather than a QSignalSpy: EditCommand, EditOutcome and JsonPointer are not registered metatypes, and
	// QSignalSpy needs a QMetaType per argument before it will store one.

	struct EditReport
	{
		int         count   = 0;
		EditCommand command = EditCommand::Delete;
		EditOutcome outcome = EditOutcome::Rejected;
	};

	EditReport editReport;

	void    record_edit_reports ();

	void    select_column  ( int column );
	void    select_row     ( int row );
	QString document_text  () const;

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

	view = std::make_unique<FormView> ( document.get (), undo.get (), selection.get (), settings.get (), nullptr, clipboard.get (), nullptr, nullptr );

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

// EDITOR-14 (Phase 15, closing OQ-2) REVERSED THIS, and the case is renamed with it -- it was called
// the_object_form_declines_the_cell_clipboard, which is the behaviour that changed. The object form declined so that
// MainWindow would fall back to the NODE clipboard, which is exactly the asymmetry OQ-2 recorded: one keystroke meant
// a cell in one face and a whole node in the other, with nothing on screen to say which.

void TestFormView::the_object_form_owns_the_field_clipboard ()
{
	view->present ( JsonPointer (), SelectionOrigin::Programmatic );   // The root object -> the form.

	QVERIFY ( view->cell_clipboard_active () );
	QVERIFY ( view->cell_copy () );
}

// The empty state still declines, and must: with no grid presented there is no cell for the gesture to act on, so the
// node clipboard is its rightful owner rather than a fallback. Without this the case above would pass against a
// FormView that simply answered yes to everything.

void TestFormView::the_empty_state_still_declines_the_clipboard ()
{
	view->present ( JsonPointer::parse ( QStringLiteral ( "/name" ) ), SelectionOrigin::Programmatic );

	// A scalar presents its PARENT's form (EDITOR-02), so drive the view to a state with no grid at all instead.

	view->present ( JsonPointer::parse ( QStringLiteral ( "/no/such/node" ) ), SelectionOrigin::Programmatic );

	QVERIFY ( !view->cell_clipboard_active () );
	QVERIFY ( !view->cell_copy () );
	QVERIFY ( !view->cell_paste () );
}

// EDITOR-14's key column writes PLAIN TEXT ONLY -- never the private node format, so a following Ctrl+V cannot insert
// a node where the user asked for a name. This is FIND-05's rule (Copy JSON Pointer) reaching a second caller, and it
// is asserted by reading back what the clipboard actually holds rather than by trusting the call.

void TestFormView::copying_a_key_cell_writes_plain_text_only ()
{
	view->present ( JsonPointer (), SelectionOrigin::Programmatic );

	QTableView* const grid = view->findChild<QTableView*> ( QStringLiteral ( "objectFormView" ) );

	QVERIFY ( grid != nullptr );

	const QModelIndex keyCell = grid->model ()->index ( 0, JsonFormModel::KEY_COLUMN );

	grid->setCurrentIndex ( keyCell );

	QVERIFY ( view->cell_copy () );

	// The key the form is showing in that cell -- read from the model rather than hard-coded, so the case survives a
	// change to the fixture document.

	QCOMPARE ( clipboard->plain_text (), grid->model ()->data ( keyCell, Qt::DisplayRole ).toString () );

	// The private format must be ABSENT -- its presence is what would make the next paste insert a node.

	QVERIFY ( clipboard->source_key ().isEmpty () );
}

// EDITOR-15: BOTH routes to the bottom edge grow the row. The plain Down arrow was missing until 2026-08-03 -- the
// growth was wired only into the delegate's post-commit movement, so Enter on the last field worked and Down did not.
// Found by manual test; this is the case that would have found it.

void TestFormView::the_down_arrow_grows_a_provisional_row ()
{
	view->present ( JsonPointer (), SelectionOrigin::Programmatic );

	QTableView* const grid = view->findChild<QTableView*> ( QStringLiteral ( "objectFormView" ) );

	QVERIFY ( grid != nullptr );

	const int members = grid->model ()->rowCount ();

	// The last real field, with no editor open -- which is the state the arrow keys navigate in.

	grid->setCurrentIndex ( grid->model ()->index ( members - 1, JsonFormModel::VALUE_COLUMN ) );

	QTest::keyClick ( grid, Qt::Key_Down );

	QCOMPARE ( grid->model ()->rowCount (), members + 1 );

	// And the highlight lands on the KEY, whichever column the move came from: the key is what creates the member.

	QCOMPARE ( grid->currentIndex ().row (),    members );
	QCOMPARE ( grid->currentIndex ().column (), JsonFormModel::KEY_COLUMN );

	// A further Down does not stack a second one.

	QTest::keyClick ( grid, Qt::Key_Down );

	QCOMPARE ( grid->model ()->rowCount (), members + 1 );
}

// An EMPTY object presents with the provisional row as its only row -- and the form's landing column is the VALUE
// column, which on that row is deliberately not editable. Landing there left the first member of an empty object
// untypeable (found by manual test, 2026-08-03).

void TestFormView::an_empty_object_lands_on_the_provisional_key ()
{
	load ( R"({ "empty": {} })" );

	view->present ( JsonPointer::parse ( QStringLiteral ( "/empty" ) ), SelectionOrigin::Programmatic );

	QTableView* const grid = view->findChild<QTableView*> ( QStringLiteral ( "objectFormView" ) );

	QVERIFY ( grid != nullptr );

	QCOMPARE ( grid->model ()->rowCount (), 1 );

	QCOMPARE ( grid->currentIndex ().row (),    0 );
	QCOMPARE ( grid->currentIndex ().column (), JsonFormModel::KEY_COLUMN );

	// The cell the highlight is on must actually open an editor, which is the whole point.

	QVERIFY ( grid->model ()->flags ( grid->currentIndex () ).testFlag ( Qt::ItemIsEditable ) );
}

// A provisional row has no member behind it, so its VALUE cell has no referent -- and left blank it looked fillable
// while doing nothing at all when typed into, which is the silent refusal VAL-05 forbids (found by manual smoke,
// 2026-08-03). It now carries a dimmed instruction, and activating it says why and moves the highlight to the key.

void TestFormView::the_provisional_value_cell_explains_itself ()
{
	load ( R"({ "empty": {} })" );

	view->present ( JsonPointer::parse ( QStringLiteral ( "/empty" ) ), SelectionOrigin::Programmatic );

	QTableView* const grid = view->findChild<QTableView*> ( QStringLiteral ( "objectFormView" ) );

	QVERIFY ( grid != nullptr );

	const QModelIndex valueCell = grid->model ()->index ( 0, JsonFormModel::VALUE_COLUMN );

	// It reads as an instruction rather than as an empty, fillable cell...

	QVERIFY ( !grid->model ()->data ( valueCell, Qt::DisplayRole ).toString ().isEmpty () );

	// ...and is rendered in the dimmed read-only style the form already uses for a value that cannot be edited,
	// rather than in a second style meaning the same thing.

	QCOMPARE
	(
		grid->model ()->data ( valueCell, cell_roles::CONTENT_KIND ).toInt (),
		static_cast<int> ( CellContent::Null )
	);

	// Activating it moves the highlight to the KEY, which is where the work actually starts.

	grid->setCurrentIndex ( valueCell );

	QTest::keyClick ( grid, Qt::Key_Return );

	QCOMPARE ( grid->currentIndex ().column (), JsonFormModel::KEY_COLUMN );
	QCOMPARE ( grid->currentIndex ().row (),    0 );

	// And nothing was created by the attempt.

	QVERIFY ( !undo->can_undo () );
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

//=====================================================================================================================
// The interactive header (EDITOR-16 / EDITOR-17 / EDIT-15)
//
// The gestures are delivered as real mouse and key events to the real headers, so what is exercised is the routing --
// which press reaches QHeaderView and which is consumed -- rather than a paraphrase of it. The STATE each gesture
// leaves behind is read from the controller, because the alternative is reading pixels, which would assert the
// painting rather than the rule; the one claim that IS about painting has its own case and does read pixels (Q12).
//=====================================================================================================================

namespace
{
	// A JSON array of `count` objects, each carrying its own index, so a case can say which element ended up where.

	QString numbered_array ( int count )
	{
		QStringList elements;

		for ( int index = 0; index < count; ++index )
		{
			elements << QStringLiteral ( R"({"n":%1})" ).arg ( index );
		}

		return QStringLiteral ( R"({"items":[%1]})" ).arg ( elements.join ( QLatin1Char ( ',' ) ) );
	}
}

QRect TestFormView::sort_zone ( int section ) const
{
	GridHeaderView* const header = view->column_header ();

	const QRect sectionRect
	(
		header->sectionViewportPosition ( section ),
		0,
		header->sectionSize ( section ),
		header->height ()
	);

	return header->sort_zone_rect ( sectionRect );
}

void TestFormView::record_edit_reports ()
{
	editReport = EditReport {};

	QObject::connect
	(
		view->array_table_controller (), &FormGridController::edit_reported,
		view.get (), [ this ] ( EditCommand command, EditOutcome outcome, const JsonPointer& )
		{
			++editReport.count;

			editReport.command = command;
			editReport.outcome = outcome;
		}
	);
}

void TestFormView::click_header ( QHeaderView* header, const QPoint& position ) const
{
	QTest::mouseClick ( header->viewport (), Qt::LeftButton, Qt::NoModifier, position );
}

void TestFormView::send_context_menu ( QHeaderView* header, const QPoint& position ) const
{
	// SENT rather than synthesized from a right click: a QContextMenuEvent is raised by the PLATFORM, and
	// QTest::mouseClick with Qt::RightButton produces press and release and nothing else. Delivered to the viewport
	// for click_header's reason -- that is the widget a header's mouse handling actually sees, and the two
	// coordinate systems coincide, a QHeaderView carrying no frame.

	QContextMenuEvent event
	(
		QContextMenuEvent::Mouse,
		position,
		header->viewport ()->mapToGlobal ( position )
	);

	QApplication::sendEvent ( header->viewport (), &event );
}

QPoint TestFormView::section_body_point ( QHeaderView* header, int section ) const
{
	// A THIRD of the way into the section: far enough from the left edge to clear QHeaderView's own resize grip --
	// which swallows the press and starts a drag instead of emitting sectionClicked -- and far enough from the right
	// edge to clear the sort marker, so a case that means "click the header, not the control inside it" is about
	// neither.

	const int left = header->sectionViewportPosition ( section );

	return QPoint ( left + ( header->sectionSize ( section ) / 3 ), header->height () / 2 );
}

QPoint TestFormView::row_index_point ( int row ) const
{
	// The middle of the row, for the resize-grip reason above: the vertical header's grip sits at the row boundary.

	GridHeaderView* const header = view->row_header ();

	return QPoint
	(
		header->width () / 2,
		header->sectionViewportPosition ( row ) + ( header->sectionSize ( row ) / 2 )
	);
}

void TestFormView::a_column_header_click_selects_the_column ()
{
	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	// EDITOR-16: a header selection is not a tree selection and is never written back to one -- selecting a column of
	// a hundred elements says nothing about which node the rest of the application is looking at. Captured rather
	// than named, because present() deliberately does not publish a selection either.

	QSignalSpy selectionSpy ( selection.get (), &SelectionService::selection_changed );

	click_header ( view->column_header (), section_body_point ( view->column_header (), 1 ) );

	const HeaderSelection& selected = view->array_table_controller ()->header_selection ();

	QCOMPARE ( selected.kind,  HeaderSelectionKind::Column );
	QCOMPARE ( selected.index, 1 );

	QCOMPARE ( selectionSpy.count (), 0 );
}

void TestFormView::a_row_index_click_selects_the_row ()
{
	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	click_header ( view->row_header (), row_index_point ( 1 ) );

	const HeaderSelection& selected = view->array_table_controller ()->header_selection ();

	QCOMPARE ( selected.kind,  HeaderSelectionKind::Row );
	QCOMPARE ( selected.index, 1 );
}

void TestFormView::only_one_header_selection_is_live_at_a_time ()
{
	// EDITOR-16's load-bearing rule: exactly one of a row and a column is selected, so a clipboard gesture has one
	// subject rather than two. A header click REPLACES rather than adding.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	FormGridController* const controller = view->array_table_controller ();

	controller->select_current_row ();
	controller->select_current_column ();

	QCOMPARE ( controller->header_selection ().kind, HeaderSelectionKind::Column );

	controller->select_current_row ();

	QCOMPARE ( controller->header_selection ().kind, HeaderSelectionKind::Row );
}

void TestFormView::a_header_selection_unselects_the_cell_that_was_selected ()
{
	// EDITOR-16's "only one of the two selections is live" is a claim about what the user SEES, not only about what a
	// command acts on. Reported from the 15g smoke: a cell selected before the header click stayed filled with the
	// highlight beside the whole column that had replaced it.
	//
	// The CURRENT cell survives -- it is where the keyboard is, and an arrow key has to resume from it -- so what is
	// asserted is the two halves separately: nothing is selected, and the current index is still valid.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 1, 1 ) );

	QVERIFY ( !tableView->selectionModel ()->selectedIndexes ().isEmpty () );

	click_header ( view->column_header (), section_body_point ( view->column_header (), 0 ) );

	QVERIFY  ( tableView->selectionModel ()->selectedIndexes ().isEmpty () );
	QVERIFY  ( tableView->currentIndex ().isValid () );
	QCOMPARE ( tableView->currentIndex ().row (), 1 );

	// And the same for a row selection, which is the other half of the report.

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 2 ) );

	QVERIFY ( !tableView->selectionModel ()->selectedIndexes ().isEmpty () );

	click_header ( view->row_header (), row_index_point ( 1 ) );

	QVERIFY ( tableView->selectionModel ()->selectedIndexes ().isEmpty () );
	QVERIFY ( tableView->currentIndex ().isValid () );
}

void TestFormView::a_cell_move_ends_a_header_selection ()
{
	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	click_header ( view->column_header (), section_body_point ( view->column_header (), 1 ) );

	QVERIFY ( view->array_table_controller ()->header_selection ().is_active () );

	// An arrow key moves the current cell, which is one of the four things EDITOR-16 says ends the selection.

	QTest::keyClick ( tableView, Qt::Key_Down );

	QVERIFY ( !view->array_table_controller ()->header_selection ().is_active () );
}

void TestFormView::a_click_on_the_current_cell_ends_a_header_selection ()
{
	// THE CASE THE CURRENT-CHANGED ROUTE CANNOT SEE, found by the MainWindow harness. A click on the cell that is
	// ALREADY current moves nothing, so currentChanged never fires -- and a header selection hides the current cell's
	// indication, so the user cannot tell which cell that is. The column stayed selected under a click that visibly
	// meant "this cell", and the next Delete removed the column.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();
	const QModelIndex current   = view->table_model ()->index ( 0, 0 );

	tableView->setCurrentIndex ( current );

	click_header ( view->column_header (), section_body_point ( view->column_header (), 1 ) );

	QVERIFY ( view->array_table_controller ()->header_selection ().is_active () );

	QTest::mouseClick ( tableView->viewport (), Qt::LeftButton, Qt::NoModifier, tableView->visualRect ( current ).center () );

	QCOMPARE ( tableView->currentIndex (), current );
	QVERIFY  ( !view->array_table_controller ()->header_selection ().is_active () );
}

void TestFormView::the_keyboard_selects_the_current_row_and_column ()
{
	// NFR-05: neither selection needs a mouse. Shift+Space and Ctrl+Space are the spreadsheet spelling.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 1, 2 ) );

	QTest::keyClick ( tableView, Qt::Key_Space, Qt::ShiftModifier );

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 1 );

	QTest::keyClick ( tableView, Qt::Key_Space, Qt::ControlModifier );

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Column );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 2 );
}

void TestFormView::shift_space_does_not_start_editing_the_cell ()
{
	// Shift+Space produces an ordinary space character, and the grid's AnyKeyPressed edit trigger would take it as
	// "start editing this cell, replacing its value with a space". The keystroke is therefore swallowed, not passed
	// on -- which is a separate claim from the selection happening, and fails separately.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QTest::keyClick ( tableView, Qt::Key_Space, Qt::ShiftModifier );

	QVERIFY ( open_editor_in ( tableView ) == nullptr );
}

void TestFormView::the_provisional_row_is_selectable_but_has_nothing_to_copy ()
{
	// EDITOR-12's provisional row became selectable when EDITOR-18 was revised to let a paste grow its target: it is
	// the only way a row reaches an EMPTY array. What it still has is no VALUES, so copy says so rather than writing
	// a row of absent cells onto the clipboard, and delete says the row does not exist yet rather than borrowing
	// Delete's Rejected wording, which is about the document root and would simply be untrue here.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QTest::keyClick ( tableView, Qt::Key_Down );

	QVERIFY ( view->table_model ()->has_provisional_row () );

	const int provisionalRow = view->table_model ()->element_count ();

	view->array_table_controller ()->select_current_row ();

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, provisionalRow );

	QVERIFY ( view->cell_copy () );
	QVERIFY ( clipboard->table_selection ().is_empty () );

	QVERIFY  ( view->cell_delete () );
	QCOMPARE ( undo->stack ()->count (), 0 );
}

void TestFormView::the_cells_of_a_selected_column_are_painted_as_selected ()
{
	// The one claim here that IS about painting, so it is checked in rendered pixels rather than by trusting a flag
	// (Q12). A selected column's cells must carry the grid's ORDINARY selection highlight -- a selected row looking
	// unlike a selected cell would be two things to learn instead of one.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	const QColor highlight = tableView->palette ().color ( QPalette::Highlight );

	const auto highlighted_pixels = [ tableView, highlight ] ()
	{
		QImage rendering ( tableView->viewport ()->size (), QImage::Format_ARGB32 );
		rendering.fill ( Qt::transparent );

		tableView->viewport ()->render ( &rendering );

		int count = 0;

		for ( int y = 0; y < rendering.height (); ++y )
		{
			for ( int x = 0; x < rendering.width (); ++x )
			{
				if ( rendering.pixelColor ( x, y ).rgb () == highlight.rgb () )
				{
					++count;
				}
			}
		}

		return count;
	};

	const int beforeSelection = highlighted_pixels ();

	view->array_table_controller ()->select_current_column ();

	const int afterSelection = highlighted_pixels ();

	// Two elements in the sample, one of which is the current cell and already highlighted -- so the increase is the
	// rest of the column. Asserted as a substantial increase rather than an exact figure, which would pin the row
	// height and the column width along with the rule.

	QVERIFY2 ( afterSelection > beforeSelection,
	           qPrintable ( QStringLiteral ( "%1 highlighted pixels before, %2 after" )
	                        .arg ( beforeSelection ).arg ( afterSelection ) ) );
}

void TestFormView::the_sort_zone_is_bounded_and_spans_the_headers_height ()
{
	// EDITOR-03's resolution rests on the sort control being small enough to MISS -- a region the size of the section
	// would pass a centre-point case and fail the requirement -- and, since the 15g/15h smoke, on its being big enough
	// to HIT. Both halves are asserted: the horizontal bound is a real boundary, and the vertical extent is the whole
	// header, so a user aiming at the control does not have to aim vertically at all.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	GridHeaderView* const header = view->column_header ();

	const QRect zone = sort_zone ( 0 );

	QVERIFY ( !zone.isEmpty () );

	QVERIFY ( header->hits_sort_zone ( zone.center () ) );

	// Bounded on the left, and on the right by the section's own edge.

	QVERIFY ( !header->hits_sort_zone ( QPoint ( zone.left () - 1, zone.center ().y () ) ) );

	// Full height, asserted against the HEADER's own extent rather than the zone's. Asking whether the zone's top and
	// bottom rows hit is a question the zone answers about itself: shrink it back to the triangle it replaced and both
	// assertions stay true, because they shrink with it. The header's height is the independent measure.

	QVERIFY ( header->hits_sort_zone ( QPoint ( zone.center ().x (), 0 ) ) );
	QVERIFY ( header->hits_sort_zone ( QPoint ( zone.center ().x (), header->height () - 1 ) ) );

	// And big enough to aim at without aiming. An absolute floor for the same reason: a bound expressed as a fraction
	// of the zone would be satisfied by any zone at all.

	QVERIFY2 ( zone.width () >= 16,
	           qPrintable ( QStringLiteral ( "sort zone is %1 px wide" ).arg ( zone.width () ) ) );

	// And it is a MINORITY of the section, not most of it -- a zone that swallowed the header would make the column
	// unselectable while passing every assertion above.

	QVERIFY2 ( zone.width () * 2 < header->sectionSize ( 0 ),
	           qPrintable ( QStringLiteral ( "zone %1 of section %2" ).arg ( zone.width () ).arg ( header->sectionSize ( 0 ) ) ) );

	// The triangle is centred INSIDE the zone -- it is a mark, and nothing hit-tests against it.

	QVERIFY ( zone.contains ( header->sort_marker_rect ( QRect ( header->sectionViewportPosition ( 0 ), 0,
	                                                            header->sectionSize ( 0 ), header->height () ) ) ) );

	// A VERTICAL header has no zone at all, so every click on a row index is a selection.

	QVERIFY ( !view->row_header ()->hits_sort_zone ( QPoint ( 2, 2 ) ) );
	QVERIFY ( view->row_header ()->sort_zone_rect ( QRect ( 0, 0, 200, 20 ) ).isEmpty () );
}

void TestFormView::a_click_on_the_sort_marker_sorts_without_selecting ()
{
	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QSignalSpy sortSpy ( view->array_table_controller (), &FormGridController::edit_reported );

	click_header ( view->column_header (), sort_zone ( 0 ).center () );

	QCOMPARE ( sortSpy.count (), 1 );

	// The press is consumed, so QHeaderView never emits sectionClicked and the sort does not also select the column
	// it sorted -- which is the half of EDITOR-03's resolution that a shared hit target would break.

	QVERIFY ( !view->array_table_controller ()->header_selection ().is_active () );
}

void TestFormView::a_click_one_pixel_beside_the_marker_selects_without_sorting ()
{
	// Deliberately at the boundary rather than at a comfortable distance: one pixel outside the sort zone is the exact
	// place a hit test that is off by one gets wrong, and it is the difference between selecting a column and
	// reordering the document.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QSignalSpy sortSpy ( view->array_table_controller (), &FormGridController::edit_reported );

	const QRect zone = sort_zone ( 0 );

	click_header ( view->column_header (), QPoint ( zone.left () - 1, zone.center ().y () ) );

	QCOMPARE ( sortSpy.count (), 0 );

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Column );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 0 );
}

void TestFormView::sorting_toggles_ascending_then_descending ()
{
	// EDIT-15: ascending FIRST. The document is what is asserted, not the marker, because the marker is a claim about
	// the document and a build that moved the marker without reordering would pass a marker-only case.

	load ( R"({"items":[{"n":"c"},{"n":"a"},{"n":"b"}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	GridHeaderView* const header = view->column_header ();

	click_header ( header, sort_zone ( 0 ).center () );

	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/items/0/n" ) ) )->string_value (), QStringLiteral ( "a" ) );
	QCOMPARE ( header->sort_marker_section (), 0 );
	QCOMPARE ( header->sort_marker_order (),   Qt::AscendingOrder );

	click_header ( header, sort_zone ( 0 ).center () );

	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/items/0/n" ) ) )->string_value (), QStringLiteral ( "c" ) );
	QCOMPARE ( header->sort_marker_order (), Qt::DescendingOrder );
}

void TestFormView::sorting_a_different_column_starts_ascending_again ()
{
	// The direction describes an ordering of the column it was chosen for. Carrying it across would state a choice
	// about the new column that the user did not make.

	load ( R"({"items":[{"a":3,"b":"x"},{"a":1,"b":"z"},{"a":2,"b":"y"}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	GridHeaderView* const header = view->column_header ();

	click_header ( header, sort_zone ( 0 ).center () );
	click_header ( header, sort_zone ( 0 ).center () );        // Column 0 is now descending.

	QCOMPARE ( header->sort_marker_order (), Qt::DescendingOrder );

	click_header ( header, sort_zone ( 1 ).center () );

	QCOMPARE ( header->sort_marker_section (), 1 );
	QCOMPARE ( header->sort_marker_order (),   Qt::AscendingOrder );

	QCOMPARE ( document->resolve ( pointer ( QStringLiteral ( "/items/0/b" ) ) )->string_value (), QStringLiteral ( "x" ) );
}

void TestFormView::the_marker_is_cleared_by_a_change_to_the_array ()
{
	// EDIT-15: the marker claims the array is in a particular order, so it cannot outlive a change to the array --
	// including the Undo of the sort that set it. Clearing says nothing where restating would say something wrong.

	load ( R"({"items":[{"n":"c"},{"n":"a"},{"n":"b"}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	click_header ( view->column_header (), sort_zone ( 0 ).center () );

	QCOMPARE ( view->column_header ()->sort_marker_section (), 0 );

	undo->undo ();

	QCOMPARE ( view->column_header ()->sort_marker_section (), -1 );
}

void TestFormView::a_re_present_clears_both_the_marker_and_the_selection ()
{
	load ( R"({"items":[{"n":"c"},{"n":"a"}],"other":[{"n":"z"}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	click_header ( view->column_header (), sort_zone ( 0 ).center () );
	click_header ( view->column_header (), section_body_point ( view->column_header (), 0 ) );

	QCOMPARE ( view->column_header ()->sort_marker_section (), 0 );
	QVERIFY  ( view->array_table_controller ()->header_selection ().is_active () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );

	QCOMPARE ( view->column_header ()->sort_marker_section (), -1 );
	QVERIFY  ( !view->array_table_controller ()->header_selection ().is_active () );
}

void TestFormView::a_narrow_column_carries_no_marker_and_stays_selectable ()
{
	// A section too narrow for the marker carries none -- and because the marker's rect IS the hit region, a narrow
	// column stays selectable rather than having its whole width become the sort control.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	GridHeaderView* const header = view->column_header ();

	// The minimum section size is the STYLE's, and it differs between platforms -- on Linux it is wide enough that a
	// resizeSection ( 0, 4 ) clamps to a section the marker still fits in, and the case then asserts nothing. Lowered
	// first, and the achieved width asserted, so a clamp fails here rather than quietly making the case vacuous.

	header->setMinimumSectionSize ( 1 );
	header->resizeSection ( 0, 4 );

	QCOMPARE ( header->sectionSize ( 0 ), 4 );

	QVERIFY ( sort_zone ( 0 ).isEmpty () );

	QSignalSpy sortSpy ( view->array_table_controller (), &FormGridController::edit_reported );

	click_header ( header, QPoint ( header->sectionViewportPosition ( 0 ) + 2, header->height () / 2 ) );

	QCOMPARE ( sortSpy.count (), 0 );
	QCOMPARE ( view->array_table_controller ()->header_selection ().kind, HeaderSelectionKind::Column );
}

void TestFormView::a_short_arrays_index_column_is_as_wide_as_a_two_digit_one ()
{
	// EDITOR-17, asserted as the PROPERTY rather than as a pixel count: an array of three elements gets the same index
	// column as an array of thirty, because the floor is two digits either way.
	//
	// Both halves matter. Qt's own hint for the two differs -- it measures the widest index it samples -- so without
	// the floor the three-element array's column is the narrower of them, which is the defect. And the width
	// exceeding the hint is the direct evidence that something raised it, a header with no minimum set being exactly
	// its own hint.

	load ( numbered_array ( 3 ).toUtf8 ().constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	const int shortArrayWidth = view->row_header ()->width ();
	const int shortArrayHint  = view->row_header ()->sizeHint ().width ();

	QVERIFY2 ( shortArrayWidth > shortArrayHint,
	           qPrintable ( QStringLiteral ( "width %1, hint %2" ).arg ( shortArrayWidth ).arg ( shortArrayHint ) ) );

	load ( numbered_array ( 30 ).toUtf8 ().constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QCOMPARE ( shortArrayWidth, view->row_header ()->width () );
}

void TestFormView::a_three_digit_array_widens_past_the_floor ()
{
	// It is a FLOOR and not a fixed width. The array has to be genuinely large for this to say anything: a header's
	// hint is the widest index it SAMPLES rather than the widest one there is, so it stays under the two-digit floor
	// well past three digits -- a 120-element array is still floor-bound, and choosing it here would have produced a
	// case that only ever compared the floor against itself.

	load ( numbered_array ( 30 ).toUtf8 ().constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	const int flooredWidth = view->row_header ()->width ();

	load ( numbered_array ( 12000 ).toUtf8 ().constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	const int wideWidth = view->row_header ()->width ();

	QVERIFY2 ( wideWidth > flooredWidth,
	           qPrintable ( QStringLiteral ( "floored %1, five digits %2" ).arg ( flooredWidth ).arg ( wideWidth ) ) );

	// And it is the CONTENT deciding now rather than the floor -- the header is exactly as wide as it asked to be.

	QCOMPARE ( wideWidth, view->row_header ()->sizeHint ().width () );
}

void TestFormView::a_double_click_on_a_row_index_drills_into_the_element ()
{
	// EDITOR-17: EDITOR-05's gesture with a second and more obvious target than the {...} cell -- a target an array of
	// SCALARS does not have at all, which is why the case is written on one.

	view->present ( pointer ( QStringLiteral ( "/roles" ) ), SelectionOrigin::Programmatic );

	QSignalSpy selectionSpy ( selection.get (), &SelectionService::selection_changed );

	QTest::mouseDClick ( view->row_header ()->viewport (), Qt::LeftButton, Qt::NoModifier, row_index_point ( 1 ) );

	// Deferred off the gesture, exactly as the cell drill-in is, so the consumer may re-present the table whose event
	// handler is still on the stack.

	QCOMPARE ( selectionSpy.count (), 0 );

	QCoreApplication::processEvents ();

	QCOMPARE ( selectionSpy.count (), 1 );
	QCOMPARE ( selection->selection ().to_string (), QStringLiteral ( "/roles/1" ) );
}

//=====================================================================================================================
// The row and column clipboard (EDITOR-18)
//
// The refusal paths report through a message box (VAL-05's "a refusal reaches both channels"), which blocks -- so the
// cases that drive one arm answer_next_modal first. That it FIRED is asserted as well as the outcome: a refusal that
// silently did nothing and a refusal that said so are different behaviours, and only one of them is the requirement.
//=====================================================================================================================

namespace
{
	// GridHeaderView::sectionSizeFromContents is protected, so the suite reaches it through a subclass -- and reaches
	// the BASE class's answer the same way, which is what makes the sort-zone allowance measurable as a difference
	// rather than as a number. A number would pin the font, the style and the display scaling along with the rule.

	class HeaderProbe : public GridHeaderView
	{
	public:

		using GridHeaderView::GridHeaderView;

		int base_hint ( int section ) const { return QHeaderView::sectionSizeFromContents ( section ).width (); }
		int own_hint  ( int section ) const { return sectionSizeFromContents ( section ).width (); }
	};

	struct ModalRecord
	{
		bool appeared = false;
	};

	// The popup counterpart, for QMenu::exec. A menu is a POPUP rather than a modal widget, so it is invisible to
	// QApplication::activeModalWidget and needs its own probe -- but the shape is answer_next_modal's exactly,
	// including the self-stopping attempt count, so a case that expected a menu and got none fails on its own
	// assertion rather than hanging the suite.

	struct PopupRecord
	{
		bool appeared = false;
	};

	void close_next_popup ( PopupRecord* record )
	{
		QTimer* const timer = new QTimer ( qApp );

		auto attempts = std::make_shared<int> ( 0 );

		timer->setInterval ( 5 );

		QObject::connect ( timer, &QTimer::timeout, timer, [ timer, record, attempts ] ()
		{
			if ( ++( *attempts ) > 200 )
			{
				timer->stop ();
				timer->deleteLater ();

				return;
			}

			QWidget* const popup = QApplication::activePopupWidget ();

			if ( popup == nullptr )
			{
				return;
			}

			record->appeared = true;

			popup->close ();

			timer->stop ();
			timer->deleteLater ();
		} );

		timer->start ();
	}

	// Answer the next modal dialog to appear. Self-stopping after a bounded number of attempts, so a case that
	// expected a modal and got none fails on its own assertion rather than hanging the suite.

	void answer_next_modal ( ModalRecord* record, QMessageBox::StandardButton button )
	{
		QTimer* const timer = new QTimer ( qApp );

		auto attempts = std::make_shared<int> ( 0 );

		timer->setInterval ( 5 );

		QObject::connect ( timer, &QTimer::timeout, timer, [ timer, record, button, attempts ] ()
		{
			if ( ++( *attempts ) > 200 )
			{
				timer->stop ();
				timer->deleteLater ();

				return;
			}

			QWidget* const modal = QApplication::activeModalWidget ();

			if ( modal == nullptr )
			{
				return;
			}

			record->appeared = true;

			if ( QMessageBox* const box = qobject_cast<QMessageBox*> ( modal ) )
			{
				if ( QAbstractButton* const answer = box->button ( button ) )
				{
					answer->click ();
				}
				else
				{
					box->close ();
				}
			}
			else
			{
				modal->close ();
			}

			timer->stop ();
			timer->deleteLater ();
		} );

		timer->start ();
	}
}

void TestFormView::select_column ( int column )
{
	view->array_table_controller ()->clear_header_selection ();

	click_header ( view->column_header (), section_body_point ( view->column_header (), column ) );

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind, HeaderSelectionKind::Column );
}

void TestFormView::select_row ( int row )
{
	view->array_table_controller ()->clear_header_selection ();

	click_header ( view->row_header (), row_index_point ( row ) );

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind, HeaderSelectionKind::Row );
}

QString TestFormView::document_text () const
{
	return JsonSerializer::serialize ( *document->root () );
}

void TestFormView::a_copied_row_pastes_onto_another_row ()
{
	load ( R"({"items":[{"a":"one","b":1},{"a":"two","b":2}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	select_row ( 1 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	// The values arrive in the matching columns; the target row's identity (its position) is untouched.

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"one","b":1},{"a":"one","b":1}]})" ) );

	// One undo step, whatever it moved -- EDITOR-18's rule for all four commands.

	QCOMPARE ( undo->stack ()->count (), 1 );

	undo->undo ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"one","b":1},{"a":"two","b":2}]})" ) );
}

void TestFormView::a_copied_column_pastes_onto_another_column ()
{
	// A column of strings onto a column of strings: EDITOR-11's matrix applied down the column, and nothing else.

	load ( R"({"items":[{"a":"x","b":"1"},{"a":"y","b":"2"},{"a":"z","b":"3"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	select_column ( 1 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"a":"x","b":"x"},{"a":"y","b":"y"},{"a":"z","b":"z"}]})" )
	);
}

void TestFormView::a_column_cannot_be_pasted_onto_a_row ()
{
	// The private format carries its SHAPE, which is what lets the refusal name what is on the clipboard rather than
	// merely failing to recognize it.

	load ( R"({"items":[{"a":"x","b":"y"},{"a":"p","b":"q"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	select_row ( 1 );

	ModalRecord modal;

	answer_next_modal ( &modal, QMessageBox::Ok );

	QVERIFY ( view->cell_paste () );

	QVERIFY  ( modal.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x","b":"y"},{"a":"p","b":"q"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );
}

void TestFormView::a_short_source_fills_what_it_covers_and_leaves_the_rest ()
{
	// EDITOR-18: a shorter source fills what it covers and leaves the rest AS IT WAS -- it does not clear the tail,
	// and it does not shrink the array.

	load ( R"({"items":[{"a":"x"},{"a":"y"}],"other":[{"a":"1"},{"a":"2"},{"a":"3"}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"a":"x"},{"a":"y"}],"other":[{"a":"x"},{"a":"y"},{"a":"3"}]})" )
	);
}

void TestFormView::a_longer_source_grows_the_array ()
{
	// EDITOR-18 revised (2026-08-18): a paste EXTENDS its target rather than dropping what will not fit. The two
	// values past the end append elements, each carrying the column's key.

	load ( R"({"items":[{"a":1},{"a":2},{"a":3}],"other":[{"a":9}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2},{"a":3}],"other":[{"a":1},{"a":2},{"a":3}]})" ) );

	// One undo step for the whole gesture -- the assignments and the growth together, which is why the macro is
	// opened by the paste rather than inside either half.

	QCOMPARE ( undo->stack ()->count (), 1 );

	undo->undo ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2},{"a":3}],"other":[{"a":9}]})" ) );
}

void TestFormView::a_scalar_arrays_column_copies_under_the_arrays_own_name ()
{
	// EDIT-16's naming rule, which is section 2.12's CSV-header rule reused rather than restated: a single-column
	// array's column has no member key but IS named -- after the array. So `roles` copies as `roles`, and a paste of
	// it into an object arrives as `roles (copy)` rather than under a placeholder.
	//
	// An earlier draft of the requirement invented a fixed fallback and would have made the same column export headed
	// `roles` and paste as something else; this is the case that would have caught it.

	view->present ( pointer ( QStringLiteral ( "/roles" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	const TableSelectionValue copied = clipboard->table_selection ();

	QCOMPARE ( copied.cells.size (), std::size_t ( 2 ) );
	QCOMPARE ( copied.cells [ 0 ].key, QStringLiteral ( "roles" ) );
}

void TestFormView::a_column_pasted_into_an_empty_array_creates_bare_elements ()
{
	// The case that prompted EDIT-16, and the one whose answer REVERSED. An empty array has no shape yet, so there is
	// nothing to attach the column's name to and the values stand as the elements.
	//
	// It produced an array of OBJECTS keyed by the source column until 2026-08-18. Stating the general rule showed
	// that reading to be the odd one out -- every other target either has a name of its own or has nothing to attach
	// one to (lesson D30).

	load ( R"({"array_1":[{"id":0,"name":"zero"},{"id":1,"name":"one"},{"id":2,"name":"two"}],"array_2":[]})" );

	view->present ( pointer ( QStringLiteral ( "/array_1" ) ), SelectionOrigin::Programmatic );
	select_column ( 1 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/array_2" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_paste () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"array_1":[{"id":0,"name":"zero"},{"id":1,"name":"one"},{"id":2,"name":"two"}],)"
		                 R"("array_2":["zero","one","two"]})" )
	);

	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::an_appended_element_carries_null_for_every_other_column ()
{
	// "New cells are null": an element the paste appends is filled out across the columns the table ALREADY has, so
	// the array stays uniform rather than the paste quietly making it ragged.

	load ( R"({"items":[{"a":"x"},{"a":"y"},{"a":"z"}],"other":[{"a":"p","b":2}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"a":"x"},{"a":"y"},{"a":"z"}],)"
		                 R"("other":[{"a":"x","b":2},{"a":"y","b":null},{"a":"z","b":null}]})" )
	);
}

void TestFormView::growth_writes_the_target_columns_key_not_the_sources ()
{
	// EDITOR-18's growth writes the TARGET column's key where the table has one, and only falls back to the source's
	// where it does not. The two must therefore DIFFER for the case to say anything -- with both columns named the
	// same, an implementation that always used the source's key passes.
	//
	// Pasting into a named column extends THAT column; adopting the source's name there would leave the array with two
	// columns holding one column's worth of values.

	load ( R"({"items":[{"name":"a"},{"name":"b"},{"name":"c"}],"other":[{"label":"z"}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"name":"a"},{"name":"b"},{"name":"c"}],)"
		                 R"("other":[{"label":"a"},{"label":"b"},{"label":"c"}]})" )
	);
}

void TestFormView::a_scalar_column_grows_a_scalar_array_with_bare_values ()
{
	// A single-value table's column has no member key, so there is nothing to write the appended values under -- and
	// inventing one would change the array's kind. The value IS the element.

	load ( R"({"items":["a","b","c"],"other":["z"]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["a","b","c"],"other":["a","b","c"]})" ) );
}

void TestFormView::a_row_pasted_onto_the_provisional_row_appends_an_element ()
{
	// The row half of the same revision. An empty array has no element to select, so the row that reaches it is
	// EDITOR-12's provisional one -- which that requirement already defines a paste into as the thing that
	// materializes it. What is new is only that several values arrive at once.

	load ( R"({"items":[{"id":1,"name":"one"}],"other":[]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );

	QVERIFY ( view->table_model ()->has_provisional_row () );

	select_row ( 0 );
	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"id":1,"name":"one"}],"other":[{"id":1,"name":"one"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::a_wider_row_adds_its_columns_across_the_whole_array ()
{
	// A row from a two-column array pasted onto a row of a one-column one. The extra value has no cell, so it creates
	// the member -- and every OTHER element gains the same key as null, or the paste would have made the array ragged
	// behind the user's back.

	load ( R"({"items":[{"a":1,"b":2}],"other":[{"a":9},{"a":8}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"a":1,"b":2}],"other":[{"a":1,"b":2},{"a":8,"b":null}]})" )
	);

	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::an_absent_source_cell_leaves_its_target_untouched ()
{
	// A ragged source: element 1 lacks "a". EDITOR-11 already makes copy and cut on a missing cell no-ops, so a paste
	// of nothing is nothing -- the target keeps the value it had rather than being nulled.

	load ( R"({"items":[{"a":"x"},{"b":"only"}],"other":[{"a":"p"},{"a":"q"}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"a":"x"},{"b":"only"}],"other":[{"a":"x"},{"a":"q"}]})" )
	);
}

void TestFormView::a_column_paste_is_one_undo_step ()
{
	// Three cells changed by one gesture. Without the macro this is three steps, and a user who pasted once would
	// press Ctrl+Z three times to undo it.

	load ( R"({"items":[{"a":"x","b":"1"},{"a":"y","b":"2"},{"a":"z","b":"3"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	select_column ( 1 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE ( undo->stack ()->count (), 1 );

	undo->undo ();

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"a":"x","b":"1"},{"a":"y","b":"2"},{"a":"z","b":"3"}]})" )
	);
}

void TestFormView::a_type_mismatch_asks_once_and_pastes_on_confirm ()
{
	// EDITOR-11 revised (2026-08-20) and EDITOR-18 with it. This case used to be
	// one_incompatible_cell_refuses_the_whole_paste, asserting that a single unconvertible cell killed the gesture --
	// which is exactly the behaviour the user reported as unusable: overwriting a column of numbers with a column of
	// strings is a deliberate act, and there was no way to say so.
	//
	// The source column's first value converts to the target's kind ("1" is a valid JSON number) and the second does
	// not. One question is asked FOR THE PASTE, and a Yes lands both -- the converted one as a number and the
	// unconvertible one as the source string, its cell having changed type.

	load ( R"({"items":[{"a":"1"},{"a":"x"}],"other":[{"a":7},{"a":8}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );

	ModalRecord modal;

	answer_next_modal ( &modal, QMessageBox::Yes );

	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QVERIFY ( modal.appeared );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"a":"1"},{"a":"x"}],"other":[{"a":1},{"a":"x"}]})" )
	);

	// ONE undo step for the paste, unchanged -- the override changes what a paste may do, never how much of it one
	// Ctrl+Z takes back.

	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::declining_the_type_override_abandons_the_whole_paste ()
{
	// THE OPPOSING HALF, and it is the old case's assertion kept rather than dropped: a No leaves the document
	// untouched -- not even the cell that would have converted cleanly.
	//
	// EDITOR-18's plan-before-apply exists precisely so a half-done column is unreachable, and skipping the offending
	// cells on a decline would land some values and not others under one undo step, which is that rule's own worst
	// case wearing a helpful face.

	load ( R"({"items":[{"a":"1"},{"a":"x"}],"other":[{"a":7},{"a":8}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );

	ModalRecord modal;

	answer_next_modal ( &modal, QMessageBox::No );

	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QVERIFY  ( modal.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"1"},{"a":"x"}],"other":[{"a":7},{"a":8}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );
}

void TestFormView::a_cell_paste_refuses_a_table_selection_rather_than_stringifying_it ()
{
	// The defect this pins produced a WRONG SUCCESS rather than a failure. ClipboardService::value falls back to the
	// clipboard's plain text when it finds no single-value format, and a column's plain text is its values one per
	// line -- so pasting a copied column onto a cell wrote one string with newlines in it, our own format having been
	// read as though it came from another application.

	load ( R"({"items":[{"a":"x"},{"a":"y"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->array_table_controller ()->clear_header_selection ();

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	ModalRecord modal;

	answer_next_modal ( &modal, QMessageBox::Ok );

	QVERIFY ( view->cell_paste () );

	QVERIFY  ( modal.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x"},{"a":"y"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );
}

void TestFormView::deleting_a_row_removes_the_element ()
{
	load ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 1 );

	QVERIFY ( view->cell_delete () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":3}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );

	// The selection moves to the row that took the deleted one's place (2026-09-23) -- the element that was row 2 is
	// row 1 now, and it is the selected row. Until then the selection simply ended here.

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 1 );
}

void TestFormView::deleting_a_column_removes_the_member_from_every_element_in_one_step ()
{
	// The one operation here with no single-node equivalent, and the one whose single undo step matters most.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"},{"a":3,"b":"z"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 1 );

	QVERIFY ( view->cell_delete () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );

	undo->undo ();

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"},{"a":3,"b":"z"}]})" )
	);
}

void TestFormView::a_ragged_column_delete_removes_only_the_members_that_exist ()
{
	// Elements 0 and 2 carry "b"; element 1 does not. The removal must not disturb the element that never had it, and
	// -- because a member's pointer is its KEY rather than a position -- it needs none of EDIT-14's descending order.
	//
	// This pins the BEHAVIOUR, not delete_column's has_member guard: that guard bounds the work rather than the answer
	// (see its own comment), so a build without it produces exactly this result by a slower route. The case is worth
	// having anyway -- the ragged array is where a positional implementation would go wrong -- but it is not evidence
	// about the guard, and saying so here is cheaper than someone re-deriving it from a clean neuter.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2},{"a":3,"b":"z"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 1 );

	QVERIFY ( view->cell_delete () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::a_column_delete_is_refused_on_a_scalar_array ()
{
	// A single-value table's one column IS the elements, so there is no member to remove -- and emptying the array is
	// a delete of every ROW wearing this command's name. Refused, and said out loud in both channels.

	load ( R"({"items":["a","b","c"]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );

	ModalRecord modal;

	answer_next_modal ( &modal, QMessageBox::Ok );

	QVERIFY ( view->cell_delete () );

	QVERIFY  ( modal.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["a","b","c"]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );
}

void TestFormView::cutting_a_row_places_what_copy_would_and_removes_the_element ()
{
	// THE COPY AND THE CUT ARE ON DIFFERENT ROWS, and that is what makes this falsifiable. Written on the same row it
	// passes against a cut that never copies at all: the clipboard still holds what the preceding copy put there, and
	// the two readings agree because they were always going to.

	load ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	const TableSelectionValue afterCopy = clipboard->table_selection ();

	select_row ( 2 );
	QVERIFY ( view->cell_cut () );

	const TableSelectionValue afterCut = clipboard->table_selection ();

	// Cut places exactly what Copy would from the SAME cell, so the two gestures can never disagree about what was
	// taken -- the shape and the arity are the copy's, and the value is the row that was actually cut.

	QCOMPARE ( afterCut.shape, afterCopy.shape );
	QCOMPARE ( afterCut.cells.size (), afterCopy.cells.size () );
	QCOMPARE ( afterCut.cells [ 0 ].value->number_token (), QStringLiteral ( "3" ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::a_header_selection_takes_precedence_over_the_current_cell ()
{
	// EDITOR-16's routing rule, asserted where it is actually decided. With a column selected, Ctrl+C copies the
	// COLUMN; with the selection cleared, the same gesture on the same current cell copies the CELL.

	load ( R"({"items":[{"a":"x"},{"a":"y"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	QCOMPARE ( clipboard->table_selection ().cells.size (), std::size_t ( 2 ) );

	view->array_table_controller ()->clear_header_selection ();

	QVERIFY ( view->cell_copy () );

	// The cell route writes the single format and no table format at all, so the next paste onto a row or column has
	// nothing of the wrong shape to find.

	QVERIFY  ( clipboard->table_selection ().is_empty () );
	QCOMPARE ( clipboard->value ()->string_value (), QStringLiteral ( "x" ) );
}

void TestFormView::the_provisional_row_is_not_among_a_columns_cells ()
{
	// EDITOR-12's provisional row is not an element, so it has no cell to copy and none to paste into. Growing the
	// array is that row's own gesture.

	load ( R"({"items":[{"a":"x"},{"a":"y"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QTest::keyClick ( tableView, Qt::Key_Down );

	QVERIFY  ( view->table_model ()->has_provisional_row () );
	QCOMPARE ( view->table_model ()->rowCount (), 3 );

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	QCOMPARE ( clipboard->table_selection ().cells.size (), std::size_t ( 2 ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Clear Contents, and the column header's context menu (EDITOR-19)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::clearing_a_column_nulls_its_cells_and_keeps_the_column ()
{
	// EDITOR-19: the column SURVIVES. That is the whole difference from Delete Column, and it is why the two are
	// separate commands rather than one with a modifier -- a cleared column is still a column to paste into.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	record_edit_reports ();

	select_column ( 1 );

	QVERIFY ( view->array_table_controller ()->clear_table_column () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":null},{"a":2,"b":null}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );

	QCOMPARE ( editReport.count,   1 );
	QCOMPARE ( editReport.command, EditCommand::ClearColumn );
	QCOMPARE ( editReport.outcome, EditOutcome::Applied );

	// The selection STAYS, unlike a delete's: its subject is still there, still selected, and still the obvious thing
	// to paste into next.

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Column );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 1 );

	undo->undo ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"}]})" ) );
}

void TestFormView::clearing_leaves_an_absent_member_absent ()
{
	// THE RULE IS INVISIBLE ON A UNIFORM ARRAY, which is why this case is written on a RAGGED one -- lesson D25's
	// shape, and the third phase running in which a fill-scope rule has needed a ragged fixture to bite at all.
	//
	// Filling the absent member with null would be Normalize (EDIT-11) wearing this command's name, and it would
	// erase the distinction section 2.12 keeps between "does not have that member" and "has it, and it is null" --
	// the same distinction the CSV export writes an empty cell for.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2},{"a":3,"b":"z"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 1 );

	QVERIFY ( view->array_table_controller ()->clear_table_column () );

	QCOMPARE ( document_text (),
	           QStringLiteral ( R"({"items":[{"a":1,"b":null},{"a":2},{"a":3,"b":null}]})" ) );
}

void TestFormView::clearing_an_already_empty_column_is_unchanged ()
{
	// Unchanged rather than Applied, so the reporting can say why nothing happened -- and, more to the point, so the
	// gesture leaves NO undo step behind. That is lesson D19 reached from a new command: one that dirties the
	// document and pushes a step undoing nothing visible is worse than one that says it did nothing.

	load ( R"({"items":[{"a":1,"b":null},{"a":2}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	record_edit_reports ();

	select_column ( 1 );

	QVERIFY ( view->array_table_controller ()->clear_table_column () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":null},{"a":2}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );

	// The outcome is the whole of the claim here, and it is the ONLY observable difference between a correct build
	// and one that clears nothing and calls it a success -- neither touches the document or the stack.

	QCOMPARE ( editReport.count,   1 );
	QCOMPARE ( editReport.outcome, EditOutcome::Unchanged );
}

void TestFormView::a_scalar_column_clears_where_it_cannot_be_deleted ()
{
	// THE OPPOSING PAIR that makes the divergence a decision rather than an accident. Delete Column REFUSES a
	// single-value table, there being no member to remove, and Clear Contents ACCEPTS it, there being somewhere to
	// write null -- [ "a", "b" ] becomes [ null, null ], still an array of two elements.
	//
	// Asserting only the clear would pass against a clear_column that had copied delete_column's guard and simply
	// been given a different outcome to return.

	load ( R"({"items":["a","b"]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );

	ModalRecord modal;

	answer_next_modal ( &modal, QMessageBox::Ok );

	QVERIFY  ( view->cell_delete () );
	QVERIFY  ( modal.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["a","b"]})" ) );

	select_column ( 0 );

	QVERIFY  ( view->array_table_controller ()->clear_table_column () );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[null,null]})" ) );
}

void TestFormView::the_column_menu_offers_the_clipboard_then_the_column_commands ()
{
	// Populating the menu is split from showing it because QMenu::exec blocks and no offscreen test can drive a
	// popup -- TreeViewPane's context menu and the JSONPath results menu are split the same way, for the same reason.
	// What that buys is exactly this: the ORDER, the separator and the enabled states are readable.
	//
	// TWO GROUPS. Above the separator, EDITOR-18's clipboard acting on the whole column; below it, the two commands
	// about the column ITSELF. This supersedes the order the menu shipped with on 2026-08-20 -- Clear Contents first
	// and separated from Delete Column -- on the user's direction: the two belong together as one group, and Delete
	// reads first of the pair.
	//
	// EVERY ITEM IS ALWAYS ENABLED, which is section 2.12's rule rather than Phase 9's: a context menu vanishes on the
	// next click, so a disabled row in it has nowhere to explain itself and no time in which to do it. Each command's
	// own refusal runs and reports in words instead (VAL-05).

	load ( R"({"items":[{"a":1,"b":"x"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );

	QMenu menu;

	view->array_table_controller ()->populate_column_menu ( menu );

	const QList<QAction*> actions = menu.actions ();

	QCOMPARE ( actions.size (), 9 );

	QCOMPARE ( actions.at ( 0 )->text (), QStringLiteral ( "Cut" ) );
	QCOMPARE ( actions.at ( 1 )->text (), QStringLiteral ( "Copy" ) );
	QCOMPARE ( actions.at ( 2 )->text (), QStringLiteral ( "Paste" ) );
	QCOMPARE ( actions.at ( 3 )->text (), QStringLiteral ( "Paste Over" ) );
	QVERIFY  ( actions.at ( 4 )->isSeparator () );
	QCOMPARE ( actions.at ( 5 )->text (), QStringLiteral ( "Rename Column" ) );
	QVERIFY  ( actions.at ( 6 )->isSeparator () );
	QCOMPARE ( actions.at ( 7 )->text (), QStringLiteral ( "Delete Column" ) );
	QCOMPARE ( actions.at ( 8 )->text (), QStringLiteral ( "Clear Contents" ) );

	for ( QAction* const action : actions )
	{
		QVERIFY ( action->isSeparator () || action->isEnabled () );
	}

	// The keys the menu SHOWS, and the two that matter are the two nobody would guess: Paste Over's, and Delete's,
	// which started working from the keyboard on 2026-09-23.

	QCOMPARE ( actions.at ( 3 )->shortcut (), QKeySequence ( Qt::CTRL | Qt::SHIFT | Qt::Key_V ) );
	QCOMPARE ( actions.at ( 7 )->shortcut (), QKeySequence ( QKeySequence::Delete ) );

	// And the items act on the selected column. Triggered rather than merely present: an item that is there and does
	// nothing is the failure this split makes visible.

	actions.at ( 8 )->trigger ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":null,"b":"x"}]})" ) );
}

void TestFormView::the_column_menus_clipboard_items_act_on_the_column ()
{
	// The three new items are EDITOR-18's commands reached from the menu rather than from the keyboard, so what is
	// asserted is that they route there -- an item wired to the CELL clipboard would copy one value and look almost
	// right.
	//
	// Copy then Paste onto another array, through the menu both times, which also closes the loop: what the menu put
	// on the clipboard is what the menu takes off it. Paste INSERTS (2026-09-23), so the copied `a` arrives in front of
	// the target's own `a` and, the name being taken there, as `a (copy)`; Paste Over, the item below it, overwrites.

	load ( R"({"items":[{"a":"x"},{"a":"y"}],"other":[{"a":"1"},{"a":"2"}]})" );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );

	{
		QMenu menu;
		view->array_table_controller ()->populate_column_menu ( menu );
		menu.actions ().at ( 1 )->trigger ();                                    // Copy.
	}

	view->present ( pointer ( QStringLiteral ( "/other" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );

	{
		QMenu menu;
		view->array_table_controller ()->populate_column_menu ( menu );
		menu.actions ().at ( 2 )->trigger ();                                    // Paste.
	}

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"j({"items":[{"a":"x"},{"a":"y"}],"other":[{"a (copy)":"x","a":"1"},{"a (copy)":"y","a":"2"}]})j" )
	);

	undo->undo ();

	select_column ( 0 );

	{
		QMenu menu;
		view->array_table_controller ()->populate_column_menu ( menu );
		menu.actions ().at ( 3 )->trigger ();                                    // Paste Over.
	}

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x"},{"a":"y"}],"other":[{"a":"x"},{"a":"y"}]})" ) );

	// And Cut takes the column away while leaving it on the clipboard -- the whole column, not one cell.

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );

	{
		QMenu menu;
		view->array_table_controller ()->populate_column_menu ( menu );
		menu.actions ().at ( 0 )->trigger ();                                    // Cut.
	}

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{},{}],"other":[{"a":"x"},{"a":"y"}]})" ) );
}

void TestFormView::a_value_cell_right_click_offers_the_field_clipboard ()
{
	// EDITOR-20. The object form's value cell answered a right-click with nothing at all until 2026-08-20 -- the code
	// said so in a comment naming Phase 9 as the phase that would claim it, and Phase 9 never did.

	load ( R"({"target":{"name":"first","other":"second"}})" );
	view->present ( pointer ( QStringLiteral ( "/target" ) ), SelectionOrigin::Programmatic );

	QMenu menu;

	view->object_form_controller ()->populate_field_menu ( menu );

	const QList<QAction*> actions = menu.actions ();

	QCOMPARE ( actions.size (), 3 );

	QCOMPARE ( actions.at ( 0 )->text (), QStringLiteral ( "Cut" ) );
	QCOMPARE ( actions.at ( 1 )->text (), QStringLiteral ( "Copy" ) );
	QCOMPARE ( actions.at ( 2 )->text (), QStringLiteral ( "Paste" ) );

	// It acts on the FIELD, which is what makes it EDITOR-14's commands rather than a second implementation of them:
	// copy row 0's value, land on row 1, paste, and the second member carries the first's value.

	QTableView* const formGrid = view->object_form_view ();

	formGrid->setCurrentIndex ( view->form_model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) );
	actions.at ( 1 )->trigger ();

	formGrid->setCurrentIndex ( view->form_model ()->index ( 1, JsonFormModel::VALUE_COLUMN ) );
	actions.at ( 2 )->trigger ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"target":{"name":"first","other":"first"}})" ) );
}

void TestFormView::a_right_click_routes_by_column_and_by_face ()
{
	// EDITOR-20's routing, asserted as the DECISION rather than by delivering the gesture. Showing the menu is not an
	// option here: QMenu::exec spins a nested event loop that the offscreen platform cannot be driven out of, so the
	// popup opens and never closes -- a test written that way HANGS the suite for five minutes rather than failing
	// it, which is how this case was first written and what sent it back.
	//
	// Three answers, and each of the other two is what stops a build satisfying the one before it. The KEY column
	// keeps EDITOR-02's node menu, which MainWindow builds from the shared NodeContextActions; the VALUE column takes
	// the field clipboard; and the ARRAY TABLE's cells raise nothing at all, their commands living on the column
	// header where the selection they act on is made (EDITOR-19).

	load ( R"({"target":{"name":"first"},"items":[{"a":1}]})" );

	view->present ( pointer ( QStringLiteral ( "/target" ) ), SelectionOrigin::Programmatic );

	FormGridController* const form = view->object_form_controller ();

	QCOMPARE
	(
		form->menu_for_cell ( view->form_model ()->index ( 0, JsonFormModel::KEY_COLUMN ) ),
		FormGridController::CellMenu::Node
	);

	QCOMPARE
	(
		form->menu_for_cell ( view->form_model ()->index ( 0, JsonFormModel::VALUE_COLUMN ) ),
		FormGridController::CellMenu::Field
	);

	QCOMPARE ( form->menu_for_cell ( QModelIndex () ), FormGridController::CellMenu::None );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QCOMPARE
	(
		view->array_table_controller ()->menu_for_cell ( view->table_model ()->index ( 0, 0 ) ),
		FormGridController::CellMenu::None
	);

	// THE GESTURE ITSELF IS NOT DELIVERED HERE, and that is a stated gap rather than an oversight. BOTH columns end
	// in a QMenu::exec -- the field menu in FormGridController, the node menu in FormView's own handler -- and the
	// offscreen platform cannot be driven out of the nested loop either of them opens. A case that delivered a right
	// click would hang the suite for five minutes rather than fail it, which is how this one was first written.
	//
	// So what is covered here is the RULE, which is why it was extracted; that handle_context_menu consults it, and
	// that the node route reaches MainWindow, are the manual smoke's (development-plan section 3.16h.4, items 5-7).
}

void TestFormView::a_column_name_is_left_aligned ()
{
	// The cells beneath are left aligned (JsonTableModel::data), and Qt's default for a horizontal header section is
	// CENTRED -- so a column wider than its name had the name floating over the middle of a column of values pinned
	// to its left edge.
	//
	// The VERTICAL header is RIGHT aligned (revised 2026-09-25), which is the half that makes this a decision rather
	// than a sweep: a row number lines up by its last digit, as a spreadsheet's does. This case used to assert the
	// vertical header stated NO alignment, on the belief that Qt then centred it -- Qt left-aligns a vertical header,
	// and the numbers had shipped left aligned under that belief. row_numbers_are_right_aligned_a_space_clear_of_the_edge
	// checks the painted result.

	load ( R"({"items":[{"a":1}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	const QVariant columnAlignment = view->table_model ()->headerData ( 0, Qt::Horizontal, Qt::TextAlignmentRole );

	QVERIFY  ( columnAlignment.isValid () );
	QCOMPARE ( columnAlignment.toInt () & Qt::AlignHorizontal_Mask, static_cast<int> ( Qt::AlignLeft ) );

	const QVariant rowAlignment = view->table_model ()->headerData ( 0, Qt::Vertical, Qt::TextAlignmentRole );

	QVERIFY  ( rowAlignment.isValid () );
	QCOMPARE ( rowAlignment.toInt () & Qt::AlignHorizontal_Mask, static_cast<int> ( Qt::AlignRight ) );
}

void TestFormView::an_auto_fit_leaves_room_for_the_sort_zone ()
{
	// EDIT-15's sort zone is painted INSIDE the section, so a section measured from its text alone is exactly the
	// zone's width too narrow and the zone lands on the last characters of the name.
	//
	// The auto-fit a user reaches by double-clicking the divider goes through sectionSizeFromContents, which is why
	// the allowance moved there from FormView::size_columns -- the double click was the one measuring path
	// size_columns could not reach.
	//
	// Asserted against the BASE class's own answer rather than against a number, so it tracks the font, the style and
	// the display scaling, and cannot pass by coincidence.

	load ( R"({"items":[{"aVeryLongColumnNameIndeed":1}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	// The measurement itself, as an OPPOSING PAIR: horizontally the override adds exactly the zone, and vertically it
	// adds exactly the row number's gap (one space in the header's font, 2026-09-25) and not the zone. Without the
	// second half a build that widened every section by the zone would satisfy the first.

	HeaderProbe columns ( Qt::Horizontal );
	HeaderProbe rows    ( Qt::Vertical );

	columns.setModel ( view->table_model () );
	rows.setModel    ( view->table_model () );

	QCOMPARE ( columns.own_hint ( 0 ) - columns.base_hint ( 0 ), config::form::SORT_ZONE_WIDTH );
	QCOMPARE ( rows.own_hint    ( 0 ) - rows.base_hint    ( 0 ), rows.fontMetrics ().horizontalAdvance ( QLatin1Char ( ' ' ) ) );

	// And end to end, through resizeColumnToContents -- which is the public API a double click on the section divider
	// ends up inside, and the one the auto-fit shares with every other ResizeToContents pass. The gesture itself is
	// not driven here: QTest::mouseDClick on the divider does nothing under the offscreen platform (measured, 64 px
	// against a name needing 128), so a case written that way would assert about a resize that never happened.

	QTableView* const tableView = view->array_table_view ();

	tableView->resize ( 600, 200 );
	tableView->setColumnWidth ( 0, config::form::MINIMUM_COLUMN_WIDTH );

	tableView->resizeColumnToContents ( 0 );

	QVERIFY2
	(
		tableView->columnWidth ( 0 ) >= ( columns.base_hint ( 0 ) + config::form::SORT_ZONE_WIDTH ),
		qPrintable ( QStringLiteral ( "auto-fit gave %1 against a name needing %2 plus a %3 zone" )
		             .arg ( tableView->columnWidth ( 0 ) )
		             .arg ( columns.base_hint ( 0 ) )
		             .arg ( config::form::SORT_ZONE_WIDTH ) )
	);
}

void TestFormView::a_column_that_appears_is_sized_to_its_name ()
{
	// EDIT-16 / EDITOR-03. Columns are sized ONCE per presented node and Interactive after that, deliberately -- so a
	// column the array did not have when it was presented has never been measured at all, and takes whatever default
	// the header gives it. A pasted column arrives with a name (15h.2's collision marker makes it longer still) that
	// is routinely wider than that default, so the name was elided under its own sort zone the moment it appeared.
	//
	// The column is pasted through the TREE route's plan, which is what actually adds one in practice.

	load ( R"({"items":[{"a":1},{"a":2}],"source":[{"aVeryLongPastedColumnName":"x"},{"aVeryLongPastedColumnName":"y"}]})" );

	view->present ( pointer ( QStringLiteral ( "/source" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	const int columnsBefore = view->table_model ()->columnCount ();

	std::vector<UndoController::PastedValue> pasted;

	pasted.push_back ( { JsonNode::make_string ( QStringLiteral ( "x" ) ) } );
	pasted.push_back ( { JsonNode::make_string ( QStringLiteral ( "y" ) ) } );

	QCOMPARE
	(
		undo->paste_value_list
		(
			pointer ( QStringLiteral ( "/items" ) ),
			pasted,
			QStringLiteral ( "aVeryLongPastedColumnName" ),
			PasteRoute::Node
		),
		EditOutcome::Applied
	);

	// The deferred sizing runs on the event loop, exactly as the implementation says it does.

	QCoreApplication::sendPostedEvents ();
	QCoreApplication::processEvents ();

	const int newColumn = view->table_model ()->columnCount () - 1;

	QCOMPARE ( view->table_model ()->columnCount (), columnsBefore + 1 );

	QTableView* const tableView = view->array_table_view ();

	// Wide enough for the header's own hint, which since this pass includes the sort zone. Asserted as ">=" rather
	// than "==" because MINIMUM_COLUMN_WIDTH and COLUMN_PADDING are both in the answer and neither is what this case
	// is about.

	QVERIFY2
	(
		tableView->columnWidth ( newColumn ) >= view->column_header ()->sectionSizeHint ( newColumn ),
		qPrintable ( QStringLiteral ( "column %1 is %2 wide against a header hint of %3" )
		             .arg ( newColumn )
		             .arg ( tableView->columnWidth ( newColumn ) )
		             .arg ( view->column_header ()->sectionSizeHint ( newColumn ) ) )
	);
}

void TestFormView::the_name_zone_and_the_row_index_raise_the_menu_and_the_sort_zone_does_not ()
{
	// EDITOR-19's boundary, and it is the boundary EDITOR-03 already drew: the sort zone is a CONTROL, and a
	// control's own area is not somewhere to right-click a menu about the thing underneath it. One rect --
	// hits_sort_zone -- answers both questions, so the line the user learned by clicking is the line the menu obeys.
	//
	// Written as an opposing pair, so neither half passes against a header that raises the request everywhere or
	// nowhere. The controller is disconnected for the duration: a menu that DID open would exec() and hang the
	// suite, and what this case is about is whether the header raises the request at all.

	load ( R"({"items":[{"a":1,"b":"x"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	GridHeaderView* const header = view->column_header ();

	QObject::disconnect ( header, &GridHeaderView::section_menu_requested, nullptr, nullptr );

	QSignalSpy requests ( header, &GridHeaderView::section_menu_requested );

	send_context_menu ( header, sort_zone ( 0 ).center () );

	QCOMPARE ( requests.count (), 0 );

	send_context_menu ( header, section_body_point ( header, 1 ) );

	QCOMPARE ( requests.count (), 1 );
	QCOMPARE ( requests.at ( 0 ).at ( 0 ).toInt (), 1 );

	// And the ROW index raises one too (EDITOR-22), naming the row -- which REVERSES what this case asserted until
	// 2026-09-23, when a row index raised nothing on the reasoning that Delete already reached a row from the keyboard.
	// It did not; see the_delete_key_removes_a_selected_row. A row index has no sort zone, so there is no second half
	// to the rule there: the whole section is the region a left click selects from.

	GridHeaderView* const rowHeader = view->row_header ();

	QObject::disconnect ( rowHeader, &GridHeaderView::section_menu_requested, nullptr, nullptr );

	QSignalSpy rowRequests ( rowHeader, &GridHeaderView::section_menu_requested );

	send_context_menu ( rowHeader, row_index_point ( 0 ) );

	QCOMPARE ( rowRequests.count (), 1 );
	QCOMPARE ( rowRequests.at ( 0 ).at ( 0 ).toInt (), 0 );
}

void TestFormView::opening_the_column_menu_selects_the_column ()
{
	// SELECT FIRST, which is what a spreadsheet does and is what makes the menu's two commands need no column of
	// their own -- they read the header selection, so the gesture that opens the menu is the gesture that decides
	// what the menu is about.
	//
	// The popup is closed from a timer for the reason answer_next_modal exists: QMenu::exec spins its own event
	// loop, so the close has to arrive from inside it.

	load ( R"({"items":[{"a":1,"b":"x"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	PopupRecord popup;

	close_next_popup ( &popup );

	send_context_menu ( view->column_header (), section_body_point ( view->column_header (), 1 ) );

	QVERIFY ( popup.appeared );

	const HeaderSelection& selected = view->array_table_controller ()->header_selection ();

	QCOMPARE ( selected.kind,  HeaderSelectionKind::Column );
	QCOMPARE ( selected.index, 1 );
}

//---------------------------------------------------------------------------------------------------------------------
// The Delete key on a selected row or column (EDITOR-18)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::the_delete_key_removes_a_selected_row ()
{
	// THE REPORTED DEFECT. EDITOR-18 has promised this key since it was written, and it never arrived: the Delete
	// QAction is scoped to the TREE, so with the table focused nothing claimed the key. Every earlier case reached the
	// row delete through view->cell_delete (), which proves the command and says nothing about the key -- which is how
	// the gap survived a phase of green runs. This case presses the KEY.
	//
	// Pressed on the table rather than on the view that hosts it, because the table is what holds the keyboard after
	// a row-index click (the header takes no focus and Qt passes it to the table).

	load ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	record_edit_reports ();

	QTableView* const tableView = view->array_table_view ();

	select_row ( 1 );

	QTest::keyClick ( tableView, Qt::Key_Delete );

	// The element goes and the ones after it move up to close the gap -- which an array does by construction.

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":3}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );

	QCOMPARE ( editReport.count,   1 );
	QCOMPARE ( editReport.command, EditCommand::Delete );
	QCOMPARE ( editReport.outcome, EditOutcome::Applied );

	// The keypad's Delete, which arrives carrying KeypadModifier. The tree's shortcut answers it (the shortcut map
	// strips the modifier before matching), so the table does too.

	select_row ( 0 );

	QTest::keyClick ( tableView, Qt::Key_Delete, Qt::KeypadModifier );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":3}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 2 );
}

void TestFormView::the_delete_key_removes_a_selected_column ()
{
	// EDITOR-18 names both selections, so restoring the key restores both. The column half is the same filter branch
	// reached through the other header, asserted so a build that answered only rows could not pass.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 1 );

	QTest::keyClick ( view->array_table_view (), Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::the_delete_key_leaves_a_current_cell_alone ()
{
	// THE OPPOSING HALF, and the user's own condition: Delete on an ordinary cell stays what it was, which is nothing.
	// It is what rules out the plausible wrong designs -- a Delete that nulls the cell the way a spreadsheet clears
	// one, or one that deletes the current cell's ROW because a row is the nearest thing to delete. Both pass the
	// row case above; neither passes this.
	//
	// It also stands in for the fix NOT taken. Scoping the tree's action to the window would have made this key
	// reach delete_node, which falls back to the tree's node when the table has nothing selected -- removing the whole
	// array. There is no MainWindow harness to assert that against, so the reasoning lives in handle_delete_key.

	load ( R"({"items":[{"a":1},{"a":2}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QVERIFY ( !view->array_table_controller ()->header_selection ().is_active () );

	QTest::keyClick ( tableView, Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );
}

void TestFormView::a_header_selection_change_is_announced ()
{
	// Document > Delete Node's enablement asks cell_delete_active () while the editor pane holds the keyboard, and
	// nothing else the window listens to moves when a row is selected -- so without this signal a ROOT array's
	// selected row stays undeletable from the menu until something unrelated recomputes the enablement.
	//
	// The pane finds the signal BY NAME (EditorPane::update_tabs), so the name is pinned here as well as the emission:
	// renaming it on this side would compile, pass every emission check, and silently disconnect the window.

	QVERIFY ( view->metaObject ()->indexOfSignal ( "cell_delete_active_changed()" ) >= 0 );

	load ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	// THE DISTINCTION THE ENABLEMENT NEEDS. A current cell makes the cell CLIPBOARD active -- and Delete has no cell
	// meaning at all, so it must not make the delete active. A build answering one question with the other would
	// enable Delete Node over an ordinary cell and route it to the tree's node.

	tableView->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QVERIFY (  view->cell_clipboard_active () );
	QVERIFY ( !view->cell_delete_active () );

	QSignalSpy changes ( view.get (), &FormView::cell_delete_active_changed );

	select_row ( 1 );

	QCOMPARE ( changes.count (), 1 );
	QVERIFY  ( view->cell_delete_active () );

	// Re-selecting the row already selected is not a change, and says nothing.

	click_header ( view->row_header (), row_index_point ( 1 ) );

	QCOMPARE ( changes.count (), 1 );

	// A different selection is, and so is the end of one -- here by a cell move, one of EDITOR-16's four enders.

	click_header ( view->column_header (), section_body_point ( view->column_header (), 0 ) );

	QCOMPARE ( changes.count (), 2 );

	tableView->setCurrentIndex ( view->table_model ()->index ( 2, 0 ) );

	QCOMPARE ( changes.count (), 3 );
	QVERIFY  ( !view->cell_delete_active () );
}

//---------------------------------------------------------------------------------------------------------------------
// Clear Contents on a row, and the row index's context menu (EDITOR-22)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::clearing_a_row_nulls_its_cells_and_keeps_the_row ()
{
	// EDITOR-22: the ELEMENT survives, which is the whole difference from Delete Row -- the array keeps its length and
	// every later element keeps its index. The two container members are there because "clear" has to mean null for a
	// cell that shows a placeholder rather than a value, and a build that skipped them would look almost right.

	load ( R"({"items":[{"a":1,"b":"x","c":{"k":1}},{"a":2,"b":"y","c":[1]}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	record_edit_reports ();

	select_row ( 0 );

	QVERIFY ( view->array_table_controller ()->clear_table_row () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":null,"b":null,"c":null},{"a":2,"b":"y","c":[1]}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );

	QCOMPARE ( editReport.count,   1 );
	QCOMPARE ( editReport.command, EditCommand::ClearRow );
	QCOMPARE ( editReport.outcome, EditOutcome::Applied );

	// The selection STAYS, as a cleared column's does: its subject is still there and still the thing to paste into.

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 0 );

	undo->undo ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":"x","c":{"k":1}},{"a":2,"b":"y","c":[1]}]})" ) );
}

void TestFormView::clearing_a_row_leaves_an_absent_member_absent ()
{
	// Written on a RAGGED array for clearing_leaves_an_absent_member_absent's reason: on a uniform one the rule is
	// invisible, every cell existing. Element 1 lacks "b", and a clear is not a Normalize.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 1 );

	QVERIFY ( view->array_table_controller ()->clear_table_row () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":"x"},{"a":null}]})" ) );
}

void TestFormView::clearing_an_already_empty_row_is_unchanged ()
{
	// Unchanged, no undo step, and the outcome as the only observable difference from a build that clears nothing and
	// calls it a success -- clearing_an_already_empty_column_is_unchanged's argument, one axis over. The row is ragged
	// as well as null, so "already empty" has to cover an absent cell and a null one together.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":null}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	record_edit_reports ();

	select_row ( 1 );

	QVERIFY ( view->array_table_controller ()->clear_table_row () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":"x"},{"a":null}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );

	QCOMPARE ( editReport.count,   1 );
	QCOMPARE ( editReport.command, EditCommand::ClearRow );
	QCOMPARE ( editReport.outcome, EditOutcome::Unchanged );
}

void TestFormView::a_scalar_row_clears_to_a_null_element ()
{
	// A single-value table's one cell IS the element, so clearing the row writes null over the element itself and the
	// array keeps its length -- [ "a", "b", "c" ] becomes [ "a", null, "c" ], never [ "a", "c" ].

	load ( R"({"items":["a","b","c"]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 1 );

	QVERIFY ( view->array_table_controller ()->clear_table_row () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["a",null,"c"]})" ) );
}

void TestFormView::the_provisional_row_cannot_be_cleared ()
{
	// EDITOR-12's provisional row is selectable as a row (so a copied row can reach an empty array) and has no element
	// behind it -- so there is nothing to clear, and the controller says so in its own words rather than letting the
	// model's Rejected reach the ClearRow wording, which describes a different failure and would raise a modal.

	load ( R"({"items":[{"a":1}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QTest::keyClick ( tableView, Qt::Key_Down );

	QVERIFY ( view->table_model ()->has_provisional_row () );

	record_edit_reports ();

	view->array_table_controller ()->select_current_row ();

	QVERIFY ( view->array_table_controller ()->clear_table_row () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );
	QCOMPARE ( editReport.count, 0 );
}

void TestFormView::the_row_menu_offers_the_clipboard_then_the_row_commands ()
{
	// EDITOR-22, split from showing for the column menu's reason: QMenu::exec blocks, and building the menu separately
	// is what makes the order, the separator and the enabled states readable. The user asked for exactly this list,
	// in exactly this order -- Cut, Copy, Paste, a separator, Delete Row, Clear Contents -- and there is no rename, a
	// row having no name.

	load ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 1 );

	QMenu menu;

	view->array_table_controller ()->populate_row_menu ( menu );

	const QList<QAction*> actions = menu.actions ();

	QCOMPARE ( actions.size (), 7 );

	QCOMPARE ( actions.at ( 0 )->text (), QStringLiteral ( "Cut" ) );
	QCOMPARE ( actions.at ( 1 )->text (), QStringLiteral ( "Copy" ) );
	QCOMPARE ( actions.at ( 2 )->text (), QStringLiteral ( "Paste" ) );
	QCOMPARE ( actions.at ( 3 )->text (), QStringLiteral ( "Paste Over" ) );
	QVERIFY  ( actions.at ( 4 )->isSeparator () );
	QCOMPARE ( actions.at ( 5 )->text (), QStringLiteral ( "Delete Row" ) );
	QCOMPARE ( actions.at ( 6 )->text (), QStringLiteral ( "Clear Contents" ) );

	for ( QAction* const action : actions )
	{
		QVERIFY ( action->isSeparator () || action->isEnabled () );
	}

	// Triggered, not merely present. Delete Row removes the element and the rows below it move up one position; the
	// element that was row 2 is row 1 now.

	actions.at ( 5 )->trigger ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":3}]})" ) );

	// And Clear Contents clears -- through a fresh menu, on row 1, which the delete has already selected as the row that
	// took the deleted one's place; select_row says so explicitly rather than leaving the case to depend on it.

	select_row ( 1 );

	QMenu secondMenu;

	view->array_table_controller ()->populate_row_menu ( secondMenu );

	secondMenu.actions ().at ( 6 )->trigger ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":null}]})" ) );
}

void TestFormView::the_row_menus_clipboard_items_act_on_the_row ()
{
	// The clipboard items are EDITOR-18's row commands, reached from the menu. Copy row 0 and paste it at row 1
	// through the menu both times, so what the menu put on the clipboard is what the menu takes off it, and each item
	// is asserted by its EFFECT -- Copy wired to Cut would take row 0 away, and Paste wired to Copy would leave the
	// array as it was. Paste INSERTS (2026-09-23): the copy goes in front of row 1, which moves down one.
	//
	// Wiring an item to the CELL clipboard instead is not a failure this can see, and not one it needs to: cut_cell,
	// copy_cell and paste_cell take the row route first whenever a row is selected, and the menu has just selected one.

	load ( R"({"items":[{"a":"x","b":1},{"a":"y","b":2}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 0 );

	{
		QMenu menu;
		view->array_table_controller ()->populate_row_menu ( menu );
		menu.actions ().at ( 1 )->trigger ();                                    // Copy.
	}

	select_row ( 1 );

	{
		QMenu menu;
		view->array_table_controller ()->populate_row_menu ( menu );
		menu.actions ().at ( 2 )->trigger ();                                    // Paste.
	}

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x","b":1},{"a":"x","b":1},{"a":"y","b":2}]})" ) );

	// Paste Over, the item below Paste, overwrites the row instead: row 2's values become row 0's.

	select_row ( 2 );

	{
		QMenu menu;
		view->array_table_controller ()->populate_row_menu ( menu );
		menu.actions ().at ( 3 )->trigger ();                                    // Paste Over.
	}

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x","b":1},{"a":"x","b":1},{"a":"x","b":1}]})" ) );

	// Cut takes the row away and leaves it on the clipboard, shaped as a ROW.

	select_row ( 0 );

	{
		QMenu menu;
		view->array_table_controller ()->populate_row_menu ( menu );
		menu.actions ().at ( 0 )->trigger ();                                    // Cut.
	}

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x","b":1},{"a":"x","b":1}]})" ) );
	QCOMPARE ( clipboard->table_selection ().shape, TableSelectionShape::Row );
}

void TestFormView::opening_the_row_menu_selects_the_row ()
{
	// SELECT FIRST, as the column menu does, and through the call a left click on the row index makes -- so the
	// right-click selects the row the same way. A COLUMN is selected beforehand, so the case also shows the gesture
	// replacing whatever was selected, which is EDITOR-16's rule for every header gesture.
	//
	// The popup is closed from a timer, for opening_the_column_menu_selects_the_column's reason.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"},{"a":3,"b":"z"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );

	PopupRecord popup;

	close_next_popup ( &popup );

	send_context_menu ( view->row_header (), row_index_point ( 2 ) );

	QVERIFY ( popup.appeared );

	const HeaderSelection& selected = view->array_table_controller ()->header_selection ();

	QCOMPARE ( selected.kind,  HeaderSelectionKind::Row );
	QCOMPARE ( selected.index, 2 );
}

//---------------------------------------------------------------------------------------------------------------------
// Paste INSERTS, and Paste Over overwrites (EDITOR-18, revised 2026-09-23)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::a_pasted_row_is_inserted_in_front_of_the_selected_row ()
{
	// THE REQUEST. Paste used to overwrite the selected row; it now inserts the copy in front of it, the selected row
	// and every row after it moving down one -- the spreadsheet's Insert Copied Cells, and the user's choice of
	// "before" over "after". Nothing the array held is lost, which is the whole of the difference asked for.

	load ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 2 );
	QVERIFY ( view->cell_copy () );

	select_row ( 1 );
	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":3},{"a":2},{"a":3}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );

	// What was pasted is what is selected afterwards: it took the index the selection named.

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 1 );

	undo->undo ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" ) );
}

void TestFormView::a_pasted_row_is_mapped_by_key ()
{
	// BY KEY, the user's choice over by position. A copied `email` stays `email` rather than being renamed to whatever
	// the target's second column is called; a column the row lacks is null in it; and the column the row brought is
	// added to every OTHER element as null, so the array does not go ragged behind the user's back.

	load ( R"({"source":[{"id":7,"email":"e"}],"target":[{"id":1,"name":"x"},{"id":2,"name":"y"}]})" );

	view->present ( pointer ( QStringLiteral ( "/source" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/target" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_paste () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"source":[{"id":7,"email":"e"}],"target":[{"id":7,"name":null,"email":"e"},)"
		                 R"({"id":1,"name":"x","email":null},{"id":2,"name":"y","email":null}]})" )
	);

	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::a_value_row_goes_in_bare_and_an_object_row_as_an_object ()
{
	// The opposing pair behind the clipboard's keyed flag. Into an array with no shape yet, a row from an array of
	// VALUES arrives as that value, and a row from an array of OBJECTS -- a one-column one, whose single cell looks
	// exactly like the value row's by its name -- arrives as an object. Without the flag the two cannot be told apart
	// and one of these two lines is wrong.

	load ( R"({"values":["b"],"objects":[{"v":1}],"first":[],"second":[]})" );

	view->present ( pointer ( QStringLiteral ( "/values" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/first" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_paste () );

	view->present ( pointer ( QStringLiteral ( "/objects" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/second" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"values":["b"],"objects":[{"v":1}],"first":["b"],"second":[{"v":1}]})" ) );
}

void TestFormView::an_inserted_row_is_shape_checked_against_the_array ()
{
	// Planned before applied, EDITOR-18's rule. An object row inserted into an array of values would make it jagged,
	// and with SET-05's "Allow jagged-array paste" off (the default) that is refused -- whole, nothing inserted, in both
	// channels. There is no cell there to convert to, so this is the one question an insert can raise.

	load ( R"({"objects":[{"v":1,"w":2}],"values":["z"]})" );

	view->present ( pointer ( QStringLiteral ( "/objects" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/values" ) ), SelectionOrigin::Programmatic );
	select_row ( 0 );

	ModalRecord modal;

	answer_next_modal ( &modal, QMessageBox::Ok );

	QVERIFY ( view->cell_paste () );

	QVERIFY  ( modal.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"objects":[{"v":1,"w":2}],"values":["z"]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );
}

void TestFormView::a_pasted_column_is_inserted_in_front_of_the_selected_column ()
{
	// The column half of the request. The copy arrives as a NEW column in front of the selected one, which moves right
	// one and keeps every value it had -- where Paste used to overwrite it. What was pasted is what is selected after.

	load ( R"({"source":[{"b":"p"},{"b":"q"}],"target":[{"x":1,"y":2},{"x":3,"y":4}]})" );

	view->present ( pointer ( QStringLiteral ( "/source" ) ), SelectionOrigin::Programmatic );
	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	view->present ( pointer ( QStringLiteral ( "/target" ) ), SelectionOrigin::Programmatic );
	select_column ( 1 );
	QVERIFY ( view->cell_paste () );

	QCOMPARE
	(
		document_text (),
		QStringLiteral ( R"({"source":[{"b":"p"},{"b":"q"}],"target":[{"x":1,"b":"p","y":2},{"x":3,"b":"q","y":4}]})" )
	);

	QCOMPARE ( undo->stack ()->count (), 1 );

	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Column );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 1 );
	QCOMPARE ( view->table_model ()->column_key ( 1 ).value_or ( QString () ), QStringLiteral ( "b" ) );
}

void TestFormView::a_pasted_column_whose_name_is_taken_arrives_as_a_copy ()
{
	// The commonest case: a column pasted back into the table it came from, whose name the table already uses. It
	// arrives as `name (copy)` -- EDIT-07's sequence, which the tree route already applies (15h.2), and the user's choice
	// over refusing -- and the original column is untouched.

	load ( R"({"items":[{"a":1,"b":2},{"a":3,"b":4}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	select_column ( 1 );
	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"j({"items":[{"a":1,"a (copy)":1,"b":2},{"a":3,"a (copy)":3,"b":4}]})j" ) );

	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 1 );
	QCOMPARE ( view->table_model ()->column_key ( 1 ).value_or ( QString () ), QStringLiteral ( "a (copy)" ) );
}

void TestFormView::ctrl_shift_v_pastes_over_a_selection_and_nothing_else ()
{
	// Paste Over's key. With a column selected it OVERWRITES -- the whole difference from Ctrl+V beside it -- and with
	// no row or column selected it does nothing at all, so the key cannot reach an ordinary cell and act there.

	load ( R"({"items":[{"a":"x","b":"1"},{"a":"y","b":"2"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	select_column ( 0 );
	QVERIFY ( view->cell_copy () );

	select_column ( 1 );

	QTest::keyClick ( tableView, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x","b":"x"},{"a":"y","b":"y"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );

	view->array_table_controller ()->clear_header_selection ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QTest::keyClick ( tableView, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x","b":"x"},{"a":"y","b":"y"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );
}

//---------------------------------------------------------------------------------------------------------------------
// The selection follows a removal (EDITOR-18, revised 2026-09-23)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::deleting_rows_selects_each_row_that_takes_the_place ()
{
	// THE REQUEST, pressed on the key. After a row is deleted the row that moved up into its place is the selected row,
	// so a second Delete removes it in turn -- the highlight always shows what the next command acts on. Where the LAST
	// row goes, the one before it is selected; where none remain, nothing is. One chain walks all three.

	load ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const          tableView  = view->array_table_view ();
	FormGridController* const  controller = view->array_table_controller ();

	select_row ( 1 );

	QTest::keyClick ( tableView, Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":3}]})" ) );
	QCOMPARE ( controller->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( controller->header_selection ().index, 1 );

	// Row 1 was the last row, so the one before it takes the selection.

	QTest::keyClick ( tableView, Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1}]})" ) );
	QCOMPARE ( controller->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( controller->header_selection ().index, 0 );

	QTest::keyClick ( tableView, Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[]})" ) );
	QVERIFY  ( !controller->header_selection ().is_active () );
}

void TestFormView::deleting_columns_selects_each_column_that_takes_the_place ()
{
	// The column half, and the same three rules: the column that moved left into the place, the one before it where
	// the last went, nothing where none remain -- the last step leaving elements with no members, and so no columns.

	load ( R"({"items":[{"a":1,"b":2,"c":3},{"a":4,"b":5,"c":6}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const          tableView  = view->array_table_view ();
	FormGridController* const  controller = view->array_table_controller ();

	select_column ( 1 );

	QTest::keyClick ( tableView, Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"c":3},{"a":4,"c":6}]})" ) );
	QCOMPARE ( controller->header_selection ().kind,  HeaderSelectionKind::Column );
	QCOMPARE ( controller->header_selection ().index, 1 );
	QCOMPARE ( view->table_model ()->column_key ( 1 ).value_or ( QString () ), QStringLiteral ( "c" ) );

	QTest::keyClick ( tableView, Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":4}]})" ) );
	QCOMPARE ( controller->header_selection ().kind,  HeaderSelectionKind::Column );
	QCOMPARE ( controller->header_selection ().index, 0 );

	QTest::keyClick ( tableView, Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{},{}]})" ) );
	QVERIFY  ( !controller->header_selection ().is_active () );
}

void TestFormView::the_placeholder_row_is_no_replacement ()
{
	// EDITOR-12's placeholder trails the real rows, so after the last REAL row is deleted it sits at the index that
	// was removed. It is not a row that took the removed one's place -- it does not exist yet, and a Delete on it only
	// says so -- so the real row before it is selected instead. Counting rows the view's way would select it.

	load ( R"({"items":[{"a":1},{"a":2}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QTest::keyClick ( tableView, Qt::Key_Down );

	QVERIFY ( view->table_model ()->has_provisional_row () );

	select_row ( 1 );

	QTest::keyClick ( tableView, Qt::Key_Delete );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1}]})" ) );
	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 0 );
}

void TestFormView::cutting_selects_what_takes_the_place_too ()
{
	// Cut removes the row as Delete does, so it moves the selection the same way -- every route that removes a row or
	// a column goes through the one command that does, which is what makes this one assertion enough for all of them.

	load ( R"({"items":[{"a":1},{"a":2}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_row ( 0 );

	QVERIFY ( view->cell_cut () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":2}]})" ) );
	QCOMPARE ( view->array_table_controller ()->header_selection ().kind,  HeaderSelectionKind::Row );
	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 0 );
}

//---------------------------------------------------------------------------------------------------------------------
// An undo that brings a row back shows it (UNDO-05)
//---------------------------------------------------------------------------------------------------------------------

namespace
{
	// UNDO-05's claim is about what is ON SCREEN, so it is asked of the viewport rather than of any flag: the row's top
	// edge at or below the viewport's top, and its bottom edge at or above the viewport's bottom.

	bool row_on_screen ( const QTableView* tableView, int row )
	{
		const int top = tableView->rowViewportPosition ( row );

		return ( top >= 0 ) && ( ( top + tableView->rowHeight ( row ) ) <= tableView->viewport ()->height () );
	}

	// Long enough that its tail is well below any viewport the fixture has, and WIDE enough to scroll sideways, so
	// the horizontal half of the rule has something to keep.

	QByteArray long_wide_array ( int rows, int columns )
	{
		QByteArray json = "{\"items\":[";

		for ( int row = 0; row < rows; ++row )
		{
			json += "{";

			for ( int column = 0; column < columns; ++column )
			{
				json += ( column > 0 ) ? "," : "";
				json += "\"column" + QByteArray::number ( column ) + "\":" + QByteArray::number ( row );
			}

			json += ( row < ( rows - 1 ) ) ? "}," : "}";
		}

		json += "]}";

		return json;
	}
}

void TestFormView::undoing_a_delete_below_the_view_scrolls_the_restored_row_into_view ()
{
	// THE REQUEST. A row deleted near the end of a long array is restored by Ctrl+Z at the index it came from, which
	// is below the part of the table the user has scrolled back to -- so the undo worked and nothing on screen said so.
	// The table now scrolls to it, VERTICALLY: the column the user was reading at stays where it was.

	const QByteArray json = long_wide_array ( 200, 30 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/items/180" ) ) ), EditOutcome::Applied );

	tableView->scrollToTop ();
	tableView->horizontalScrollBar ()->setValue ( tableView->horizontalScrollBar ()->maximum () );

	QCoreApplication::processEvents ();

	const int across = tableView->horizontalScrollBar ()->value ();

	QVERIFY ( across > 0 );
	QVERIFY ( !row_on_screen ( tableView, 180 ) );

	undo->undo ();

	QTRY_VERIFY ( row_on_screen ( tableView, 180 ) );

	QCOMPARE ( view->table_model ()->element_node ( 180 )->find_member ( QStringLiteral ( "column0" ) )->number_token (), QStringLiteral ( "180" ) );
	QCOMPARE ( tableView->horizontalScrollBar ()->value (), across );
}

void TestFormView::a_restored_row_already_on_screen_does_not_move_the_view ()
{
	// The minimal move, and none at all where there is nothing to bring into view. Asked mid-table rather than at the
	// top, where a scroll that CENTRED the row would be clamped to the same place and pass by accident.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->scrollTo ( view->table_model ()->index ( 100, 0 ), QAbstractItemView::PositionAtTop );

	QCoreApplication::processEvents ();

	QCOMPARE ( undo->delete_node ( pointer ( QStringLiteral ( "/items/102" ) ) ), EditOutcome::Applied );

	QCoreApplication::processEvents ();

	const int down = tableView->verticalScrollBar ()->value ();

	QVERIFY ( row_on_screen ( tableView, 102 ) );

	undo->undo ();

	QCoreApplication::processEvents ();
	QCoreApplication::processEvents ();

	QVERIFY  ( row_on_screen ( tableView, 102 ) );
	QCOMPARE ( tableView->verticalScrollBar ()->value (), down );
}

void TestFormView::an_ordinary_insert_does_not_scroll_but_its_undo_and_redo_do ()
{
	// ONLY A REPLAY'S ROWS. An ordinary edit that inserts a row does so where the user is already working, and a table
	// that jumped on every insert would move under their hands -- so a row inserted below the view by an edit leaves
	// the view alone. Its UNDO takes the row away and goes to where it was (revised 2026-09-24: any row an undo acts
	// on -- until then this case asserted the undo did NOT scroll), and its REDO brings it back into view.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->scrollToTop ();

	std::unique_ptr<JsonNode> element = JsonNode::make_object ();

	element->append_member ( QStringLiteral ( "column0" ), JsonNode::make_string ( QStringLiteral ( "new" ) ) );

	QCOMPARE
	(
		undo->insert_element_at ( pointer ( QStringLiteral ( "/items" ) ), 180, std::move ( element ), QStringLiteral ( "Insert" ) ),
		EditOutcome::Applied
	);

	QCoreApplication::processEvents ();
	QCoreApplication::processEvents ();

	QVERIFY ( !row_on_screen ( tableView, 180 ) );

	undo->undo ();

	QTRY_VERIFY ( row_on_screen ( tableView, 180 ) );

	tableView->scrollToTop ();

	QCoreApplication::processEvents ();

	QVERIFY ( !row_on_screen ( tableView, 180 ) );

	undo->redo ();

	QTRY_VERIFY ( row_on_screen ( tableView, 180 ) );

	QCOMPARE ( view->table_model ()->element_node ( 180 )->find_member ( QStringLiteral ( "column0" ) )->string_value (), QStringLiteral ( "new" ) );
}

void TestFormView::pasting_onto_a_row_scrolled_out_of_view_scrolls_to_it ()
{
	// THE REQUEST. A row stays selected when the user scrolls away from it, so a paste there lands out of sight --
	// UNDO-05's "it worked and nothing said so", reached by a paste. The table now scrolls to the pasted row.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	tableView->setCurrentIndex ( view->table_model ()->index ( 180, 0 ) );
	view->array_table_controller ()->select_current_row ();

	tableView->scrollToTop ();

	QCoreApplication::processEvents ();

	QVERIFY ( !row_on_screen ( tableView, 180 ) );

	QVERIFY ( view->cell_paste () );

	QTRY_VERIFY ( row_on_screen ( tableView, 180 ) );

	// The copy of row 0, inserted in front of the row that was selected.

	QCOMPARE ( view->table_model ()->element_node ( 180 )->find_member ( QStringLiteral ( "column0" ) )->number_token (), QStringLiteral ( "0" ) );
	QCOMPARE ( view->table_model ()->element_node ( 181 )->find_member ( QStringLiteral ( "column0" ) )->number_token (), QStringLiteral ( "180" ) );
}

void TestFormView::pasting_over_a_row_scrolled_out_of_view_scrolls_to_it ()
{
	// Paste Over pastes a row onto the selected row too, so it brings that row into view the same way.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	tableView->setCurrentIndex ( view->table_model ()->index ( 180, 0 ) );
	view->array_table_controller ()->select_current_row ();

	tableView->scrollToTop ();

	QCoreApplication::processEvents ();

	QVERIFY ( !row_on_screen ( tableView, 180 ) );

	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QTRY_VERIFY ( row_on_screen ( tableView, 180 ) );

	QCOMPARE ( view->table_model ()->element_node ( 180 )->find_member ( QStringLiteral ( "column0" ) )->number_token (), QStringLiteral ( "0" ) );
}

void TestFormView::a_paste_onto_a_row_on_screen_does_not_move_the_view ()
{
	// The minimal move again, and asked mid-table for a_restored_row_already_on_screen_does_not_move_the_view's
	// reason: at the top a centring scroll would be clamped into passing.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const          tableView  = view->array_table_view ();
	FormGridController* const  controller = view->array_table_controller ();

	tableView->scrollTo ( view->table_model ()->index ( 100, 0 ), QAbstractItemView::PositionAtTop );

	QCoreApplication::processEvents ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 101, 0 ) );
	controller->select_current_row ();
	QVERIFY ( view->cell_copy () );

	tableView->setCurrentIndex ( view->table_model ()->index ( 102, 0 ) );
	controller->select_current_row ();

	QCoreApplication::processEvents ();

	const int down = tableView->verticalScrollBar ()->value ();

	QVERIFY ( view->cell_paste () );

	QCoreApplication::processEvents ();
	QCoreApplication::processEvents ();

	QVERIFY  ( row_on_screen ( tableView, 102 ) );
	QCOMPARE ( tableView->verticalScrollBar ()->value (), down );
}

void TestFormView::pasting_over_the_placeholder_row_out_of_view_scrolls_to_the_new_row ()
{
	// The case that makes the reveal's DEFERRAL a claim rather than a precaution. Pasted onto the placeholder row, Paste
	// Over appends the element inside a macro it still holds open when it returns -- so the table has not heard of the
	// row yet, and a reveal run at that moment finds nothing to scroll to. Every other reveal case passes against an
	// immediate call; this one does not.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	tableView->setCurrentIndex ( view->table_model ()->index ( 199, 0 ) );

	QTest::keyClick ( tableView, Qt::Key_Down );

	QVERIFY ( view->table_model ()->has_provisional_row () );

	view->array_table_controller ()->select_current_row ();

	QCOMPARE ( view->array_table_controller ()->header_selection ().index, 200 );

	tableView->scrollToTop ();

	QCoreApplication::processEvents ();

	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE ( view->table_model ()->element_count (), 201 );

	QTRY_VERIFY ( row_on_screen ( tableView, 200 ) );
}

void TestFormView::undoing_a_paste_below_the_view_scrolls_to_where_the_row_was ()
{
	// THE REQUEST. A row pasted (inserted) far down the table, the user scrolls back to the top, and Ctrl+Z takes the
	// row away again -- out of sight, so the undo worked and nothing said so. The table goes to where the pasted row
	// WAS, which is where the row that moved up into its place now is.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	tableView->setCurrentIndex ( view->table_model ()->index ( 180, 0 ) );
	view->array_table_controller ()->select_current_row ();

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( view->table_model ()->element_count (), 201 );

	QCoreApplication::processEvents ();

	tableView->scrollToTop ();

	QCoreApplication::processEvents ();

	QVERIFY ( !row_on_screen ( tableView, 180 ) );

	undo->undo ();

	QCOMPARE ( view->table_model ()->element_count (), 200 );

	QTRY_VERIFY ( row_on_screen ( tableView, 180 ) );

	QCOMPARE ( view->table_model ()->element_node ( 180 )->find_member ( QStringLiteral ( "column0" ) )->number_token (), QStringLiteral ( "180" ) );
}

void TestFormView::undoing_a_paste_over_below_the_view_scrolls_to_the_row ()
{
	// Paste Over changes a row's VALUES rather than the rows, so undoing it adds and removes nothing -- the row is
	// found from the change itself, which the document names at that element. Asserted with the old value back, so the
	// scroll is to the row the undo actually repaired.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	select_row ( 0 );
	QVERIFY ( view->cell_copy () );

	tableView->setCurrentIndex ( view->table_model ()->index ( 180, 0 ) );
	view->array_table_controller ()->select_current_row ();

	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCoreApplication::processEvents ();

	tableView->scrollToTop ();

	QCoreApplication::processEvents ();

	QVERIFY ( !row_on_screen ( tableView, 180 ) );

	undo->undo ();

	QTRY_VERIFY ( row_on_screen ( tableView, 180 ) );

	QCOMPARE ( view->table_model ()->element_node ( 180 )->find_member ( QStringLiteral ( "column0" ) )->number_token (), QStringLiteral ( "180" ) );
}

void TestFormView::undoing_a_cell_edit_below_the_view_scrolls_to_its_row ()
{
	// ANY row an undo changes, the user's choice -- not only pasted ones. The edit itself is an ordinary edit and
	// leaves the view alone; undoing it is a replay and goes to the row.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->scrollToTop ();

	QCOMPARE ( undo->set_number ( pointer ( QStringLiteral ( "/items/180/column1" ) ), QStringLiteral ( "999" ) ), EditOutcome::Applied );

	QCoreApplication::processEvents ();
	QCoreApplication::processEvents ();

	QVERIFY ( !row_on_screen ( tableView, 180 ) );

	undo->undo ();

	QTRY_VERIFY ( row_on_screen ( tableView, 180 ) );

	QCOMPARE ( view->table_model ()->element_node ( 180 )->find_member ( QStringLiteral ( "column1" ) )->number_token (), QStringLiteral ( "180" ) );
}

void TestFormView::undoing_the_last_row_scrolls_to_the_new_last_row ()
{
	// Where the row an undo took away was the LAST, the index it was at no longer exists -- the nearest thing to where
	// it was is the new last row, and that is what comes into view.

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->scrollToTop ();

	std::unique_ptr<JsonNode> element = JsonNode::make_object ();

	element->append_member ( QStringLiteral ( "column0" ), JsonNode::make_string ( QStringLiteral ( "new" ) ) );

	QCOMPARE ( undo->append_element ( pointer ( QStringLiteral ( "/items" ) ), std::move ( element ), QStringLiteral ( "Add" ) ), EditOutcome::Applied );

	QCoreApplication::processEvents ();

	QVERIFY ( !row_on_screen ( tableView, 199 ) );

	undo->undo ();

	QCOMPARE ( view->table_model ()->element_count (), 200 );

	QTRY_VERIFY ( row_on_screen ( tableView, 199 ) );
}

void TestFormView::an_undo_across_many_rows_does_not_scroll ()
{
	// An undo whose change spans rows -- a whole column cleared and put back -- names the ARRAY rather than a row, and
	// there is no single row to go to. The view stays where the user left it, mid-table, rather than being taken to
	// the top by a rule that read "the first row the array has" as "the row the undo acted on".

	const QByteArray json = long_wide_array ( 200, 3 );

	load ( json.constData () );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->scrollTo ( view->table_model ()->index ( 100, 0 ), QAbstractItemView::PositionAtTop );

	QCoreApplication::processEvents ();

	select_column ( 1 );

	QVERIFY ( view->array_table_controller ()->clear_table_column () );

	QCoreApplication::processEvents ();

	const int down = tableView->verticalScrollBar ()->value ();

	QVERIFY ( down > 0 );

	undo->undo ();

	QCoreApplication::processEvents ();
	QCoreApplication::processEvents ();

	QCOMPARE ( view->table_model ()->element_node ( 150 )->find_member ( QStringLiteral ( "column1" ) )->number_token (), QStringLiteral ( "150" ) );
	QCOMPARE ( tableView->verticalScrollBar ()->value (), down );
}

//---------------------------------------------------------------------------------------------------------------------
// Row height (config::form::OBJECT_ROW_VERTICAL_PADDING / ARRAY_ROW_VERTICAL_PADDING)
//---------------------------------------------------------------------------------------------------------------------

void TestFormView::the_array_tables_rows_are_sized_by_the_array_dial ()
{
	// EDITOR-03's rows come from the ARRAY dial, and the case exists because for a while they came from nowhere at
	// all. The array table's two headers are replaced with GridHeaderViews after the grid is built (Phase 15g), and
	// QTableView::setVerticalHeader carries nothing across from the header it displaces -- so the size set on the
	// stock header was discarded and the rows were Qt's own default (30 px, measured) whatever the dial said.
	//
	// Asserted as the FORMULA rather than as a pixel count, so it holds at any font, any display scaling and any
	// value of the dial. Against a build with the constructor's apply_row_height call removed this reads Qt's
	// default instead, which is the state that shipped.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ArrayTable );

	const int expected = tableView->fontMetrics ().height () + config::form::ARRAY_ROW_VERTICAL_PADDING;

	QCOMPARE ( tableView->rowHeight ( 0 ), expected );
	QCOMPARE ( tableView->rowHeight ( 1 ), expected );
}

void TestFormView::the_object_forms_rows_are_sized_by_the_object_dial ()
{
	// The other half, and it is asserted for the same reason the array half is: the object form keeps its STOCK
	// vertical header, so it was never affected by the replacement above -- but both faces now go through one
	// apply_row_height, and a case on only the face that broke would not notice the shared path breaking the other.
	//
	// Wrap strings is off here (the default), so the rows are Fixed and the formula is exact. Under ResizeToContents
	// this height becomes a floor rather than an answer, which tst_json_cell_delegate covers from the delegate side.

	view->present ( pointer ( QStringLiteral ( "" ) ), SelectionOrigin::Programmatic );

	QTableView* const formView = view->object_form_view ();

	QCOMPARE ( view->presentation_mode (), FormPresentation::Mode::ObjectForm );

	const int expected = formView->fontMetrics ().height () + config::form::OBJECT_ROW_VERTICAL_PADDING;

	QCOMPARE ( formView->rowHeight ( 0 ), expected );

	// WHILE THE TWO DIALS HOLD THE SAME VALUE THIS CASE CANNOT TELL THEM APART, and saying so is worth more than
	// leaving the reader to assume it can. What is pinned above is that each face is sized by the formula at all --
	// which is what actually broke. A build that read the ARRAY dial here would be caught only once the two values
	// differ, and that is the moment the drift the dials were split to allow becomes visible anyway.
}

void TestFormView::the_array_tables_rows_are_fixed_and_floored_at_the_dials_answer ()
{
	// TWO PROPERTIES THE HEIGHT ALONE DOES NOT CARRY, both lost with it and both restored by the same call.
	//
	// FIXED, because a QHeaderView defaults to Interactive and the divider between two row headers is then a resize
	// grip -- the array table's rows were draggable from Phase 15g until apply_row_height existed. It is also what
	// lets the view compute its visible range arithmetically rather than measuring every row (architecture.md
	// section 10), which is the half that matters on a large array.
	//
	// FLOORED AT THE FORMULA's OWN ANSWER, because QHeaderView clamps a section to its minimumSectionSize and an
	// unset minimum is the style's (17-20 px here). Without this the dial would stop shrinking the rows somewhere
	// the caller cannot see -- lesson Q54, which is the same finding one face further along.

	view->present ( pointer ( QStringLiteral ( "/projects" ) ), SelectionOrigin::Programmatic );

	QHeaderView* const rows = view->array_table_view ()->verticalHeader ();

	QCOMPARE ( rows->sectionResizeMode ( 0 ), QHeaderView::Fixed );

	const int expected = view->array_table_view ()->fontMetrics ().height ()
	                   + config::form::ARRAY_ROW_VERTICAL_PADDING;

	QCOMPARE ( rows->minimumSectionSize (), expected );

	// The floor is a floor rather than a fixture: a smaller section asked for is clamped up to it, which is exactly
	// what silently swallowed the dial before the minimum was set.

	rows->resizeSection ( 0, 1 );

	QCOMPARE ( rows->sectionSize ( 0 ), expected );
}

void TestFormView::renaming_a_column_renames_the_member_on_every_element ()
{
	// EDITOR-21. A column is a member ACROSS elements, so renaming it is one rename per element and one undo step for
	// the gesture -- the same asymmetry EDITOR-18 turns on, reached by a different command.
	//
	// Written on a RAGGED array deliberately: the element that lacks the member must be left alone rather than gaining
	// one, which is the rule Clear Contents states and which a uniform fixture cannot see.

	load ( R"({"items":[{"a":1,"old":"x"},{"a":2},{"a":3,"old":"z"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	select_column ( 1 );

	QCOMPARE ( view->table_model ()->rename_column ( 1, QStringLiteral ( "renamed" ) ), EditOutcome::Applied );

	QCOMPARE ( document_text (),
	           QStringLiteral ( R"({"items":[{"a":1,"renamed":"x"},{"a":2},{"a":3,"renamed":"z"}]})" ) );

	// One step for the gesture, whatever it touched.

	QCOMPARE ( undo->stack ()->count (), 1 );

	undo->undo ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"old":"x"},{"a":2},{"a":3,"old":"z"}]})" ) );

	// The name it already has is a no-op that says so, and leaves no step behind (D19 / Q43). On a FRESH load, so
	// the count is read against an empty stack -- after an undo the step is still counted, only the index moves, and
	// a push would then clear the redo history and land back at one, which is a claim that cannot fail.

	load ( R"({"items":[{"a":1,"old":"x"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	// Measured as a DIFFERENCE, because the fixture's load () does not clear the stack -- an absolute count here
	// would be reading the earlier undo step in this same case.

	const int stepsBeforeNoOp = undo->stack ()->count ();

	QCOMPARE ( view->table_model ()->rename_column ( 1, QStringLiteral ( "old" ) ), EditOutcome::Unchanged );
	QCOMPARE ( undo->stack ()->count (), stepsBeforeNoOp );

	// And a column no element carries is Unchanged too, rather than a refusal -- there is nothing to rename and
	// nothing went wrong.

	QCOMPARE ( view->table_model ()->rename_column ( 1, QStringLiteral ( "fresh" ) ), EditOutcome::Applied );

	// And a single-value table has no member to rename -- delete_column's refusal, for the same reason.

	load ( R"({"items":["a","b"]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QCOMPARE ( view->table_model ()->rename_column ( 0, QStringLiteral ( "anything" ) ), EditOutcome::Rejected );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["a","b"]})" ) );
}

void TestFormView::a_rename_that_would_collide_is_refused_whole ()
{
	// PLANNED BEFORE APPLIED (EDITOR-18's rule, reached by a rename). Element 0 would rename cleanly and element 1
	// already carries the target name -- so a rename applied as it went would leave the array with one element
	// renamed and one not, under a single undo step, and the column split in two.
	//
	// The collision is on the SECOND element deliberately: on the first, a build that applied as it went would refuse
	// before changing anything and pass this case by accident.

	load ( R"({"items":[{"old":1},{"old":2,"taken":9}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QCOMPARE ( view->table_model ()->rename_column ( 0, QStringLiteral ( "taken" ) ), EditOutcome::Rejected );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"old":1},{"old":2,"taken":9}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );

	// SET-03a lets it through, because the refusal is VAL-02's and that setting is what VAL-02 now answers to.

	undo->set_allow_duplicate_keys ( true );

	QCOMPARE ( view->table_model ()->rename_column ( 0, QStringLiteral ( "taken" ) ), EditOutcome::Applied );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"taken":1},{"taken":2,"taken":9}]})" ) );
}

void TestFormView::a_column_pasted_over_after_a_sort_undoes_back_through_the_sort ()
{
	// The combined interaction 15h's smoke test carried as its seventh item: a sort REORDERS THE DOCUMENT (EDIT-15),
	// so a column copied after it carries the sorted order, lands in the sorted order, and two undos walk back through
	// the paste and then the sort -- two steps, in that order, and nothing in between. Each half has cases of its own;
	// what only the pair can show is that the paste read positions the sort had already moved.

	load ( R"({"items":[{"n":2,"v":20},{"n":1,"v":10},{"n":3,"v":30}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	const QString original = document_text ();

	click_header ( view->column_header (), sort_zone ( 0 ).center () );

	const QString sorted = QStringLiteral ( R"({"items":[{"n":1,"v":10},{"n":2,"v":20},{"n":3,"v":30}]})" );

	QCOMPARE ( document_text (), sorted );

	// Copied AFTER the sort, so the column is 10, 20, 30 -- the unsorted order would be 20, 10, 30. Both columns are
	// numbers, so EDITOR-11's conversion matrix has nothing to convert and the values land exactly as copied.

	select_column ( 1 );
	QVERIFY ( view->cell_copy () );

	select_column ( 0 );
	QVERIFY ( view->array_table_controller ()->paste_over_table_selection () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"n":10,"v":10},{"n":20,"v":20},{"n":30,"v":30}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 2 );

	undo->undo ();

	QCOMPARE ( document_text (), sorted );

	undo->undo ();

	QCOMPARE ( document_text (), original );
	QVERIFY  ( !undo->can_undo () );
}

void TestFormView::lines_from_another_application_fill_down_from_the_current_cell ()
{
	// EDITOR-24, the reported case verbatim: three lines from Notepad pasted onto a cell used to land as ONE string,
	// shown as Four\nFive\nSix\n. They fill down from the cell instead, overwriting, as one undo step.

	load ( R"({"items":["p","q","r","s"]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QApplication::clipboard ()->setText ( QStringLiteral ( "Four\nFive\nSix\n" ) );

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["p","Four","Five","Six"]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );

	undo->undo ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["p","q","r","s"]})" ) );
}

void TestFormView::a_spreadsheet_block_fills_rows_and_columns_and_grows_the_array ()
{
	// A two-by-two range from Excel: tabs move right, lines move down, and a row past the last becomes a new element
	// -- whose cells the block supplies, in the columns they came from. Each field is read as external text always is,
	// so 3 is a number and z a string.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QApplication::clipboard ()->setText ( QStringLiteral ( "3\tz\r\n4\tw\r\n" ) );

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":"x"},{"a":3,"b":"z"},{"a":4,"b":"w"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::a_block_wider_than_the_columns_left_is_refused_whole ()
{
	// Two values from the LAST column have one column to land in. Refused whole, in both channels, with nothing
	// changed -- a paste that dropped the second value would do less than it looked like.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 0, 1 ) );

	QApplication::clipboard ()->setText ( QStringLiteral ( "p\tq\n" ) );

	ModalRecord modal;

	answer_next_modal ( &modal, QMessageBox::Ok );

	QVERIFY ( view->cell_paste () );

	QVERIFY  ( modal.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 0 );
}

void TestFormView::an_empty_field_leaves_its_cell_and_holds_its_place ()
{
	// An empty cell in the copied range leaves its target as it was -- VJE's own rule for an absent cell -- and holds
	// its column and its row, so every other value lands where the user saw it. A build that SKIPPED empty fields
	// would shift q into column a; one that nulled them would clear x and 2.

	load ( R"({"items":[{"a":1,"b":"x"},{"a":2,"b":"y"}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QApplication::clipboard ()->setText ( QStringLiteral ( "9\t\n\tq\n" ) );

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":9,"b":"x"},{"a":2,"b":"q"}]})" ) );
}

void TestFormView::a_json_value_over_several_lines_still_pastes_as_one_value ()
{
	// THE OPPOSING HALF of the first case: lines that together are ONE JSON value paste as that value, exactly as a
	// single-line paste of it always has. Onto an array of nulls, so EDITOR-11's shape check has nothing to object to
	// and the only question left is whether the text was split.

	load ( R"({"items":[null,null]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QApplication::clipboard ()->setText ( QStringLiteral ( "{\n  \"k\": 1\n}\n" ) );

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"k":1},null]})" ) );
}

void TestFormView::vje_s_own_copy_of_a_multi_line_string_is_not_read_as_a_block ()
{
	// A cell copied INSIDE VJE carries the private format, and its plain text is only a rendering for other
	// applications. A string holding a line break must paste back as that one string, not spread over two cells.

	load ( R"({"items":["a\nb","x","y"]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QVERIFY ( view->cell_copy () );

	tableView->setCurrentIndex ( view->table_model ()->index ( 1, 0 ) );

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["a\nb","a\nb","y"]})" ) );
}

void TestFormView::a_block_pasted_on_the_placeholder_row_appends_its_rows ()
{
	// EDITOR-12's placeholder row is where a block goes to extend an array from its end: every line becomes a new
	// element, and the highlight lands on the first of them.

	load ( R"({"items":["p"]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	QTableView* const tableView = view->array_table_view ();

	tableView->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QTest::keyClick ( tableView, Qt::Key_Down );

	QVERIFY ( view->table_model ()->is_provisional_row ( tableView->currentIndex ().row () ) );

	QApplication::clipboard ()->setText ( QStringLiteral ( "q\nr\n" ) );

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["p","q","r"]})" ) );
	QCOMPARE ( tableView->currentIndex ().row (), 1 );
	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::a_blank_line_past_the_end_still_holds_its_row ()
{
	// Past the end, an empty line still becomes an element -- null, as every cell a paste's growth does not supply
	// is -- or every line after it would land one row too high and the block would arrive with a row missing.

	load ( R"({"items":["p"]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QApplication::clipboard ()->setText ( QStringLiteral ( "a\n\nc\n" ) );

	QVERIFY ( view->cell_paste () );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["a",null,"c"]})" ) );
}

void TestFormView::a_block_with_the_wrong_types_asks_once ()
{
	// Words over a column of numbers: EDITOR-11's matrix has no conversion, so the paste ASKS -- once for the whole
	// block, never once per cell -- and a No leaves everything as it was.

	load ( R"({"items":[{"a":1},{"a":2}]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );

	QApplication::clipboard ()->setText ( QStringLiteral ( "x\ny\n" ) );

	ModalRecord declined;

	answer_next_modal ( &declined, QMessageBox::No );

	QVERIFY ( view->cell_paste () );

	QVERIFY  ( declined.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":2}]})" ) );

	ModalRecord accepted;

	answer_next_modal ( &accepted, QMessageBox::Yes );

	QVERIFY ( view->cell_paste () );

	QVERIFY  ( accepted.appeared );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":"x"},{"a":"y"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), 1 );
}

void TestFormView::row_numbers_are_right_aligned_a_space_clear_of_the_edge ()
{
	// EDITOR-17's row numbers, revised 2026-09-25: RIGHT aligned, so 9 and 10 end in the same pixel column, with a
	// space between the last digit and the column's edge. Read from rendered ink (Q12) -- and written against the
	// two ways it can go wrong: Qt's own default for a vertical header is LEFT aligned (which the numbers shipped as,
	// under a comment claiming they were centred), and a right-aligned label with no gap sits on the grid line.
	//
	// A runner with no fonts (Q20) draws no digits at all, which says nothing about alignment, so it skips.

	load ( R"({"items":[0,1,2,3,4,5,6,7,8,9,10,11,12]})" );
	view->present ( pointer ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

	GridHeaderView* const header = view->row_header ();

	const QImage image = header->grab ().toImage ();

	auto rightmost_ink = [ & ] ( int row ) -> int
	{
		const int top    = header->sectionViewportPosition ( row );
		const int bottom = top + header->sectionSize ( row ) - 1;
		const int middle = ( top + bottom ) / 2;

		// The label's background, taken at the section's LEFT edge on its middle line -- empty in a right-aligned
		// single digit's section, and the colour every non-ink pixel of the label band shares.

		const QColor background = image.pixelColor ( 2, middle );

		int rightmost = -1;

		for ( int y = top + 2; y <= bottom - 2; ++y )
		{
			for ( int x = 0; x < header->width () - 2; ++x )
			{
				const QColor pixel = image.pixelColor ( x, y );

				const int difference = std::abs ( pixel.lightness () - background.lightness () );

				if ( difference > 60 )
				{
					rightmost = std::max ( rightmost, x );
				}
			}
		}

		return rightmost;
	};

	// 9 AND 10, not 7 and 11: in a narrow font a "1" is so thin that "11" ends where "7" does whichever way the two
	// are aligned, and the first draft of this case passed against a LEFT-aligned build for exactly that reason.
	// A two-digit number with a zero in it is wider than any one digit in every font.

	const int nineRight = rightmost_ink ( 9 );
	const int tenRight  = rightmost_ink ( 10 );

	if ( ( nineRight < 0 ) || ( tenRight < 0 ) )
	{
		QSKIP ( "No glyphs were drawn -- this runner has no fonts (lesson Q20)." );
	}

	// Right aligned: a one-digit and a two-digit number end together.

	QVERIFY2 ( std::abs ( nineRight - tenRight ) <= 1, qPrintable ( QStringLiteral ( "9 ends at %1, 10 at %2" ).arg ( nineRight ).arg ( tenRight ) ) );

	// A space clear of the edge: the style's own label margin AND a space in the header's font between the last
	// inked column and the section's right edge. The margin alone is what a right-aligned label with no gap gets,
	// so asserting a space alone would pass against exactly that build.

	const int gap    = ( header->width () - 1 ) - nineRight;
	const int margin = header->style ()->pixelMetric ( QStyle::PM_HeaderMargin, nullptr, header );
	const int space  = header->fontMetrics ().horizontalAdvance ( QLatin1Char ( ' ' ) );

	QVERIFY2 ( gap >= margin + space, qPrintable ( QStringLiteral ( "gap %1 px, margin %2, space %3" ).arg ( gap ).arg ( margin ).arg ( space ) ) );
}

QTEST_MAIN ( TestFormView )

#include "tst_form_view.moc"
