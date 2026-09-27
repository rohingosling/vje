//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   The MainWindow harness: the whole window, built around its own composition root exactly as main.cpp builds it,
//   and driven the way a user drives it -- a key goes to whichever widget holds the keyboard, a click lands on a
//   viewport, and every modal the window raises is answered by a script rather than bypassed.
//
//   It exists for the one class of claim no other suite can reach. Every command below is covered where it is
//   DECIDED -- tst_form_view, tst_paste_target_plan, tst_undo_controller, tst_json_form_model -- and none of those
//   suites contains the window, so none of them can say whether a key pressed in the running application reaches that
//   command. That gap is where two real defects lived: EDITOR-18's Delete key never reached a selected row, because
//   Delete Node's shortcut is scoped to the tree (lesson D37), and SET-03a appeared not to work, because a prompt ABOVE
//   UndoController refused the key before the policy was consulted. Both had green suites underneath them.
//
//   THE PREMISE IS THAT THE OFFSCREEN PLATFORM GRANTS FOCUS ONCE THE WINDOW IS ACTIVE -- which lesson Q10 says it does
//   not. Q10 was measured with show () + activateWindow () + setFocus () read back at once; activation arrives through
//   the platform's event queue, so waiting for it (QTest::qWaitForWindowActive) is what was missing. Measured on Qt
//   6.10.1 and 6.8.3: the window activates, setFocus sticks, a WidgetWithChildrenShortcut fires only for its own
//   widget's subtree, and a QMenu::exec popup is reachable through QApplication::activePopupWidget (). Each case below
//   asserts the focus it depends on, so a platform that stops granting it fails loudly rather than vacuously.
//
//   Keys are delivered to QApplication::focusWidget (), never to a named widget: naming the widget is what made
//   D37's gap invisible, since it decides the one thing under test.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "MainWindow.hpp"
#include "models/JsonFormModel.hpp"
#include "models/JsonTableModel.hpp"
#include "models/JsonTreeModel.hpp"
#include "services/BackgroundIo.hpp"
#include "services/ClipboardService.hpp"
#include "services/IconLibrary.hpp"
#include "services/SelectionService.hpp"
#include "services/settings_profiles.hpp"
#include "services/StatusService.hpp"
#include "services/ThemeService.hpp"
#include "style/tooltip_text.hpp"
#include "views/CodeEditor.hpp"
#include "views/CodeView.hpp"
#include "views/EditorPane.hpp"
#include "views/FormGridController.hpp"
#include "views/FindBar.hpp"
#include "views/FormView.hpp"
#include "views/GridHeaderView.hpp"
#include "views/toolbar_catalogue.hpp"
#include "views/TreeNodeDelegate.hpp"
#include "views/TreeViewPane.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonSerializer.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUndoStack>

#include <functional>
#include <memory>

using namespace vje;

//---------------------------------------------------------------------------------------------------------------------
// Suite
//---------------------------------------------------------------------------------------------------------------------

class TestMainWindow : public QObject
{
	Q_OBJECT

private slots:

	void init    ();
	void cleanup ();

	// EDIT-16's tree route -- the paste EDITOR-18's table selections reach when the TREE holds the keyboard (15h.1,
	// 15h.2, and 15h.7's "tree route unchanged").

	void a_column_copied_in_the_table_pastes_into_a_tree_selected_object ();
	void a_row_copied_in_the_table_pastes_onto_a_tree_selected_array_and_object ();
	void a_column_pasted_onto_a_tree_selected_scalar_is_refused_in_a_modal ();
	void a_column_pasted_onto_a_multiple_tree_selection_is_refused ();

	// EDITOR-18's Delete key and Document > Delete Node (15h.6).

	void the_delete_key_follows_the_keyboard_between_the_table_and_the_tree ();
	void delete_node_answers_a_selected_row_of_a_root_array ();
	void a_row_menu_dismissed_with_escape_leaves_delete_on_the_row ();

	// Paste inserts and Paste Over overwrites, through the window's own shortcuts (15h.7).

	void ctrl_v_inserts_and_ctrl_shift_v_pastes_over_in_the_window ();

	// SET-03a, Allow duplicate keys, reaching every surface it governs (15h.4, 15h.5).

	void rename_key_follows_the_duplicate_key_setting_live ();
	void add_child_re_prompts_on_a_duplicate_and_accepts_one_when_allowed ();
	void rename_column_follows_the_duplicate_key_setting ();
	void the_key_editor_accepts_a_duplicate_only_when_allowed ();
	void a_duplicate_survives_save_and_reopen_and_edits_its_own_member ();
	void the_selected_duplicate_is_the_one_a_command_acts_on ();

	// VIEW-04's unsaved-work marks (15h.5).

	void a_code_view_edit_marks_the_title_and_the_status_bar ();

	// EDITOR-16: a header selection hides the current cell, and a cell move brings it back (15h.3).

	void a_header_selection_hides_the_current_cell_and_an_arrow_restores_it ();

	// FIND-04's keyboard, EDITOR-24's paste from another application, and the find bar's close glyph (2026-09-25).

	void go_to_leaves_the_keyboard_in_the_tree_on_the_node_it_selected ();
	void a_cancelled_go_to_puts_the_keyboard_back ();
	void lines_pasted_from_another_application_fill_down_the_table ();
	void the_find_bar_closes_with_the_tab_close_glyph ();

	// Phase 15l -- the unsaved-change marks (TREE-10, SET-14) and the tooltip rule (STYLE-17), in the whole window.

	void the_mark_scope_setting_reaches_the_tree_live ();
	void a_save_through_the_window_clears_the_marks ();
	void every_toolbar_command_is_named_by_its_tooltip ();
	void only_the_menus_with_something_to_add_show_tooltips ();
	void the_dot_colour_follows_the_setting_and_the_theme ();

private:

	// The window and the document.

	void        start_window  ();
	bool        open          ( const char* json );
	QString     document_text () const;
	QString     document_path () const;

	// The widgets under test, found rather than exposed: MainWindow's members are private, and a test accessor on
	// the window would be a second way in that the application does not have.

	FormView*    form_view    () const;
	QTreeView*   tree_view    () const;
	TreeViewPane* tree_pane   () const;
	EditorPane*  editor_pane  () const;
	QAction*     action       ( const QString& objectName ) const;
	QString      status_document_text () const;

	// Gestures.

	QWidget* keyboard_holder  ();
	bool press                 ( Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier );
	void select_in_tree        ( const QString& pointerText );
	void click_tree_index      ( const QModelIndex& index );
	void click_row_index       ( int row );
	void click_column_header   ( int column );
	void click_cell            ( int row, int column );
	void right_click_row_index ( int row );

	// The modal script. The window's modals are REAL -- QInputDialog, QMessageBox, QMenu::exec -- because a fake
	// dialog service would replace exactly the half of SET-03a's defect that lived above the command layer. A poller
	// answers each one as it appears, in the order the case expected them; one the case did not expect is recorded and
	// closed, so a stray modal fails the case instead of hanging it.

	using ModalAnswer = std::function<QString ( QWidget* )>;   // Returns a problem; empty when answered as expected.

	void expect_text_prompt     ( const QString& answer );
	void expect_message         ();
	void expect_popup_dismissed ();
	void expect_go_to           ( const QString& pointerText );
	void answer_modals          ();
	bool every_modal_answered   ( QString* report ) const;

	QTemporaryDir directory;
	int           caseNumber = 0;

	std::unique_ptr<SettingsStore>    settings;
	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<StatusService>    status;
	std::unique_ptr<ThemeService>     theme;
	std::unique_ptr<ClipboardService> clipboard;
	std::unique_ptr<ThreadPoolIo>     backgroundIo;
	std::unique_ptr<IconLibrary>      icons;
	std::unique_ptr<MainWindow>       window;

	QTimer                   modalPoller;
	QList<ModalAnswer>       modalScript;
	QStringList              modalProblems;
	QStringList              messages;       // The text of every message box answered, in order.
	QList<QMessageBox::Icon> messageIcons;   // Each of those boxes' icon, in the same order (STYLE-18 (5)).
	QPointer<QWidget>        lastAnswered;   // So a modal still closing is not answered twice.
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::init ()
{
	// The services main.cpp builds, in its order and with its settings reads. The WINDOW is not built here: several
	// cases write a setting first, and the window reads some of them at construction (the toolbar layout among them).

	++caseNumber;

	settings = std::make_unique<SettingsStore>
	(
		directory.filePath ( QStringLiteral ( "settings-%1.json" ).arg ( caseNumber ) )
	);

	document     = std::make_unique<JsonDocument> ();
	undo         = std::make_unique<UndoController> ( document.get () );
	selection    = std::make_unique<SelectionService> ();
	status       = std::make_unique<StatusService> ();
	theme        = std::make_unique<ThemeService> ( settings.get () );
	clipboard    = std::make_unique<ClipboardService> ( QApplication::clipboard () );
	backgroundIo = std::make_unique<ThreadPoolIo> ();
	icons        = std::make_unique<IconLibrary> ( theme.get () );

	modalScript.clear ();
	modalProblems.clear ();
	messages.clear ();
	messageIcons.clear ();
	lastAnswered = nullptr;

	modalPoller.setInterval ( 15 );

	connect ( &modalPoller, &QTimer::timeout, this, &TestMainWindow::answer_modals, Qt::UniqueConnection );

	modalPoller.start ();
}

void TestMainWindow::cleanup ()
{
	modalPoller.stop ();

	// Strict reverse dependency order, as main.cpp's stack unwinds it: the window first, the document last.

	window.reset ();
	icons.reset ();
	backgroundIo.reset ();
	clipboard.reset ();
	theme.reset ();
	status.reset ();
	selection.reset ();
	undo.reset ();
	document.reset ();
	settings.reset ();

	QVERIFY2 ( modalProblems.isEmpty (), qPrintable ( modalProblems.join ( QStringLiteral ( "; " ) ) ) );
}

void TestMainWindow::start_window ()
{
	const config::appearance::InterfaceStyle interfaceStyle = interface_style ( settings.get () );

	icons->set_interface_style ( interfaceStyle );
	theme->set_interface_style ( interfaceStyle );

	theme->apply ();

	window = std::make_unique<MainWindow>
	(
		document.get (), undo.get (), settings.get (), theme.get (), selection.get (), status.get (), icons.get (),
		clipboard.get (), backgroundIo.get ()
	);

	window->resize ( 1200, 800 );
	window->show ();
	window->activateWindow ();

	QVERIFY ( QTest::qWaitForWindowActive ( window.get () ) );
}

bool TestMainWindow::open ( const char* json )
{
	// Through the window's own File > Open pipeline, from a real file, so a later Save has somewhere to write and a
	// reopen reads back what was written.

	QFile file ( document_path () );

	if ( !file.open ( QIODevice::WriteOnly | QIODevice::Truncate ) )
	{
		return false;
	}

	file.write ( json );
	file.close ();

	return window->open_document_from_path ( document_path () );
}

QString TestMainWindow::document_text () const
{
	return JsonSerializer::serialize ( *document->root () );
}

QString TestMainWindow::document_path () const
{
	return directory.filePath ( QStringLiteral ( "document-%1.json" ).arg ( caseNumber ) );
}

FormView* TestMainWindow::form_view () const
{
	return dynamic_cast<FormView*> ( editor_pane ()->view_for_id ( FormViewProvider::VIEW_ID ) );
}

QTreeView* TestMainWindow::tree_view () const
{
	return tree_pane ()->view ();
}

TreeViewPane* TestMainWindow::tree_pane () const
{
	return window->findChild<TreeViewPane*> ();
}

EditorPane* TestMainWindow::editor_pane () const
{
	return window->findChild<EditorPane*> ();
}

QAction* TestMainWindow::action ( const QString& objectName ) const
{
	// The toolbar catalogue's stable names are the actions' object names (MainWindow::create_toolbar).

	return window->findChild<QAction*> ( objectName );
}

QString TestMainWindow::status_document_text () const
{
	// The status bar's document pane, told apart from the node path and node info panes by carrying the file name.

	const QString fileName = QFileInfo ( document_path () ).fileName ();

	for ( QLabel* const label : window->statusBar ()->findChildren<QLabel*> () )
	{
		if ( label->text ().startsWith ( fileName ) )
		{
			return label->text ();
		}
	}

	return QString ();
}

//---------------------------------------------------------------------------------------------------------------------
// Gestures
//---------------------------------------------------------------------------------------------------------------------

QWidget* TestMainWindow::keyboard_holder ()
{
	// THE PLATFORM'S HALF OF CLOSING A MODAL. When a modal or a popup closes, the window system hands activation back
	// to the window beneath it and Qt then restores that window's own focus child; the offscreen platform does the
	// second and not the first, so after any modal the application has no focus widget at all. Re-activating is
	// the platform's step only -- which widget then holds the keyboard is still Qt's and the application's to decide,
	// and that is what the cases assert.

	if ( QApplication::activeWindow () != window.get () )
	{
		window->activateWindow ();

		if ( !QTest::qWaitForWindowActive ( window.get () ) )
		{
			return nullptr;
		}
	}

	return QApplication::focusWidget ();
}

bool TestMainWindow::press ( Qt::Key key, Qt::KeyboardModifiers modifiers )
{
	// To whatever holds the keyboard -- the whole point of the suite. Naming a widget here would decide the routing
	// the case exists to check.

	QWidget* const target = keyboard_holder ();

	if ( target == nullptr )
	{
		return false;
	}

	QTest::keyClick ( target, key, modifiers );

	return true;
}

void TestMainWindow::select_in_tree ( const QString& pointerText )
{
	// A click on a tree row takes the keyboard on the PRESS, before the selection moves -- so the tree is focused
	// first, then the node revealed (expanding its ancestors, as Go To does) and clicked. Revealing it with the
	// keyboard still in the Form View would be a different gesture: the view re-presents with focus inside it, and
	// its field write-back (EDITOR-04) answers with a selection of its own.

	const JsonPointer pointer = JsonPointer::parse ( pointerText );

	tree_view ()->setFocus ();

	selection->set_selection ( pointer, SelectionOrigin::GoTo );

	click_tree_index ( tree_pane ()->model ()->index_for_pointer ( pointer ) );
}

void TestMainWindow::click_tree_index ( const QModelIndex& index )
{
	QTreeView* const tree = tree_view ();

	tree->scrollTo ( index );

	QTest::mouseClick ( tree->viewport (), Qt::LeftButton, Qt::NoModifier, tree->visualRect ( index ).center () );
}

void TestMainWindow::click_row_index ( int row )
{
	// The middle of the row: the vertical header's resize grip sits at the row boundary.

	GridHeaderView* const header = form_view ()->row_header ();

	const QPoint point ( header->width () / 2, header->sectionViewportPosition ( row ) + ( header->sectionSize ( row ) / 2 ) );

	QTest::mouseClick ( header->viewport (), Qt::LeftButton, Qt::NoModifier, point );
}

void TestMainWindow::click_column_header ( int column )
{
	// A third of the way in, clear of both the resize grip on the left and the sort zone on the right.

	GridHeaderView* const header = form_view ()->column_header ();

	const QPoint point ( header->sectionViewportPosition ( column ) + ( header->sectionSize ( column ) / 3 ), header->height () / 2 );

	QTest::mouseClick ( header->viewport (), Qt::LeftButton, Qt::NoModifier, point );
}

void TestMainWindow::click_cell ( int row, int column )
{
	QTableView* const table = form_view ()->array_table_view ();

	const QRect cell = table->visualRect ( table->model ()->index ( row, column ) );

	QTest::mouseClick ( table->viewport (), Qt::LeftButton, Qt::NoModifier, cell.center () );
}

void TestMainWindow::right_click_row_index ( int row )
{
	// As the platform delivers it: the button's press and release, THEN the context-menu event. The press is what
	// moves the keyboard -- which is 15h.6's "the right-click moved the keyboard to the table as a left click does" --
	// and QTest::mouseClick with the right button produces the press and release and nothing else.

	GridHeaderView* const header = form_view ()->row_header ();

	const QPoint point ( header->width () / 2, header->sectionViewportPosition ( row ) + ( header->sectionSize ( row ) / 2 ) );

	QTest::mouseClick ( header->viewport (), Qt::RightButton, Qt::NoModifier, point );

	QContextMenuEvent event ( QContextMenuEvent::Mouse, point, header->viewport ()->mapToGlobal ( point ) );

	QApplication::sendEvent ( header->viewport (), &event );
}

//---------------------------------------------------------------------------------------------------------------------
// The modal script
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::expect_text_prompt ( const QString& answer )
{
	modalScript.append ( [ answer ] ( QWidget* widget ) -> QString
	{
		QInputDialog* const prompt = qobject_cast<QInputDialog*> ( widget );

		if ( prompt == nullptr )
		{
			return QStringLiteral ( "expected a text prompt, got a %1" ).arg ( QString::fromLatin1 ( widget->metaObject ()->className () ) );
		}

		prompt->setTextValue ( answer );
		prompt->accept ();

		return QString ();
	} );
}

void TestMainWindow::expect_message ()
{
	modalScript.append ( [ this ] ( QWidget* widget ) -> QString
	{
		QMessageBox* const box = qobject_cast<QMessageBox*> ( widget );

		if ( box == nullptr )
		{
			return QStringLiteral ( "expected a message box, got a %1" ).arg ( QString::fromLatin1 ( widget->metaObject ()->className () ) );
		}

		messages.append ( box->text () );
		messageIcons.append ( box->icon () );

		box->accept ();

		return QString ();
	} );
}

void TestMainWindow::expect_go_to ( const QString& pointerText )
{
	// GoToDialog is its own class rather than a QInputDialog (FIND-04 reports in place), so it is answered through
	// its one text field and the key a user presses -- Return, which is what runs the Go To.

	modalScript.append ( [ pointerText ] ( QWidget* widget ) -> QString
	{
		QLineEdit* const field = widget->findChild<QLineEdit*> ();

		if ( field == nullptr )
		{
			return QStringLiteral ( "expected the Go To dialog, got a %1" ).arg ( QString::fromLatin1 ( widget->metaObject ()->className () ) );
		}

		field->setText ( pointerText );

		QTest::keyClick ( field, Qt::Key_Return );

		return QString ();
	} );
}

void TestMainWindow::expect_popup_dismissed ()
{
	modalScript.append ( [] ( QWidget* widget ) -> QString
	{
		if ( qobject_cast<QMenu*> ( widget ) == nullptr )
		{
			return QStringLiteral ( "expected a popup menu, got a %1" ).arg ( QString::fromLatin1 ( widget->metaObject ()->className () ) );
		}

		// Esc, as the user closes it -- not close (), which would skip whatever the menu does on the key.

		QTest::keyClick ( widget, Qt::Key_Escape );

		return QString ();
	} );
}

void TestMainWindow::answer_modals ()
{
	// A popup outranks a modal: a QMenu::exec raised from inside a modal is the one on top.

	QWidget* const popup  = QApplication::activePopupWidget ();
	QWidget* const target = ( popup != nullptr ) ? popup : QApplication::activeModalWidget ();

	if ( ( target == nullptr ) || ( target == lastAnswered ) || !target->isVisible () )
	{
		return;
	}

	lastAnswered = target;

	if ( modalScript.isEmpty () )
	{
		QString text = target->windowTitle ();

		if ( QMessageBox* const box = qobject_cast<QMessageBox*> ( target ) )
		{
			text += QStringLiteral ( ": " ) + box->text ();
		}

		modalProblems.append ( QStringLiteral ( "unexpected %1 (%2)" ).arg ( QString::fromLatin1 ( target->metaObject ()->className () ), text ) );

		if ( QDialog* const dialog = qobject_cast<QDialog*> ( target ) )
		{
			dialog->reject ();
		}
		else
		{
			target->close ();
		}

		return;
	}

	const QString problem = modalScript.takeFirst () ( target );

	if ( !problem.isEmpty () )
	{
		modalProblems.append ( problem );

		if ( QDialog* const dialog = qobject_cast<QDialog*> ( target ) )
		{
			dialog->reject ();
		}
		else
		{
			target->close ();
		}
	}
}

bool TestMainWindow::every_modal_answered ( QString* report ) const
{
	if ( !modalScript.isEmpty () )
	{
		*report = QStringLiteral ( "%1 expected modal(s) never appeared" ).arg ( modalScript.size () );

		return false;
	}

	if ( !modalProblems.isEmpty () )
	{
		*report = modalProblems.join ( QStringLiteral ( "; " ) );

		return false;
	}

	return true;
}

// Checked after every gesture that should (or should not) have raised a modal, so a failure names the gesture.

#define VERIFY_MODALS()                                                  \
	do                                                                   \
	{                                                                    \
		QString modalReport;                                             \
		QVERIFY2 ( every_modal_answered ( &modalReport ), qPrintable ( modalReport ) ); \
	} while ( false )

//---------------------------------------------------------------------------------------------------------------------
// EDIT-16's tree route
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::a_column_copied_in_the_table_pastes_into_a_tree_selected_object ()
{
	// 15h.1's first item end to end: Ctrl+C with the TABLE holding the keyboard copies the selected column, and Ctrl+V
	// with the TREE holding it takes the node route -- the one tst_paste_target_plan decides and nothing else reaches.
	// An object takes the column as one member holding an array, named for the column, in one undo step.

	start_window ();
	QVERIFY ( open ( R"({"target":{"k":1},"people":[{"name":"a","role":"x"},{"name":"b","role":"y"}]})" ) );

	select_in_tree ( QStringLiteral ( "/people" ) );
	click_column_header ( 1 );

	QCOMPARE ( keyboard_holder (), form_view ()->array_table_view () );
	QVERIFY  ( press ( Qt::Key_C, Qt::ControlModifier ) );

	select_in_tree ( QStringLiteral ( "/target" ) );

	QCOMPARE ( keyboard_holder (), tree_view () );

	const int stepsBefore = undo->stack ()->count ();

	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"target":{"k":1,"role":["x","y"]},"people":[{"name":"a","role":"x"},{"name":"b","role":"y"}]})" ) );
	QCOMPARE ( undo->stack ()->count (), stepsBefore + 1 );

	// One Ctrl+Z takes the whole paste back (15h.1's fifth item).

	QVERIFY ( press ( Qt::Key_Z, Qt::ControlModifier ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"target":{"k":1},"people":[{"name":"a","role":"x"},{"name":"b","role":"y"}]})" ) );
}

void TestMainWindow::a_row_copied_in_the_table_pastes_onto_a_tree_selected_array_and_object ()
{
	// 15h.1's fourth item: a row is an element, so the node route places it as one -- appended to an array, and under
	// the synthesized key "item" in an object (EDIT-06).

	start_window ();
	QVERIFY ( open ( R"({"target":{"k":1},"people":[{"name":"a"},{"name":"b"}]})" ) );

	select_in_tree ( QStringLiteral ( "/people" ) );
	click_row_index ( 0 );

	QCOMPARE ( keyboard_holder (), form_view ()->array_table_view () );
	QVERIFY  ( press ( Qt::Key_C, Qt::ControlModifier ) );

	select_in_tree ( QStringLiteral ( "/people" ) );

	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"target":{"k":1},"people":[{"name":"a"},{"name":"b"},{"name":"a"}]})" ) );

	select_in_tree ( QStringLiteral ( "/target" ) );

	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"target":{"k":1,"item":{"name":"a"}},"people":[{"name":"a"},{"name":"b"},{"name":"a"}]})" ) );
}

void TestMainWindow::a_column_pasted_onto_a_tree_selected_scalar_is_refused_in_a_modal ()
{
	// 15h.1's third item: a column has no place on a scalar, and a refusal reaches BOTH channels (VAL-05) -- so the
	// modal is asserted, and so is the absence of an undo step (15h.1's fifth item: a refusal adds none).

	start_window ();
	QVERIFY ( open ( R"({"target":{"k":1},"people":[{"name":"a"},{"name":"b"}]})" ) );

	select_in_tree ( QStringLiteral ( "/people" ) );
	click_column_header ( 0 );

	QVERIFY ( press ( Qt::Key_C, Qt::ControlModifier ) );

	select_in_tree ( QStringLiteral ( "/target/k" ) );

	const QString before      = document_text ();
	const int     stepsBefore = undo->stack ()->count ();

	expect_message ();

	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), before );
	QCOMPARE ( undo->stack ()->count (), stepsBefore );
}

void TestMainWindow::a_column_pasted_onto_a_multiple_tree_selection_is_refused ()
{
	// 15h.1's other refusal: a column has ONE target or none. Two selected siblings are refused whole rather than
	// each receiving a copy, and the refusal is the modal's.

	start_window ();
	QVERIFY ( open ( R"({"first":{},"second":{},"people":[{"name":"a"},{"name":"b"}]})" ) );

	select_in_tree ( QStringLiteral ( "/people" ) );
	click_column_header ( 0 );

	QVERIFY ( press ( Qt::Key_C, Qt::ControlModifier ) );

	select_in_tree ( QStringLiteral ( "/first" ) );

	const JsonPointer first  = JsonPointer::parse ( QStringLiteral ( "/first" ) );
	const JsonPointer second = JsonPointer::parse ( QStringLiteral ( "/second" ) );

	selection->set_multiple_selection ( { first, second }, first, SelectionOrigin::Tree );

	QCOMPARE ( keyboard_holder (), tree_view () );

	const QString before = document_text ();

	expect_message ();

	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier ) );

	VERIFY_MODALS ();

	QCOMPARE ( messageIcons, QList<QMessageBox::Icon> { QMessageBox::Warning } );   // STYLE-18 (5).

	QCOMPARE ( document_text (), before );
}

//---------------------------------------------------------------------------------------------------------------------
// EDITOR-18's Delete key
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::the_delete_key_follows_the_keyboard_between_the_table_and_the_tree ()
{
	// The reported defect (15h.6, lesson D37) and its four neighbours, in the window where Delete Node's shortcut is
	// live. tst_form_view presses the key on a FormView with no window around it -- so no competing shortcut -- which
	// is exactly the configuration in which the defect could not appear.

	start_window ();
	QVERIFY ( open ( R"({"items":[{"a":1,"b":1},{"a":2,"b":2},{"a":3,"b":3}],"other":true})" ) );

	select_in_tree ( QStringLiteral ( "/items" ) );

	// A selected ROW: the row goes, and the array it was in stays.

	click_row_index ( 1 );

	QCOMPARE ( keyboard_holder (), form_view ()->array_table_view () );
	QVERIFY  ( press ( Qt::Key_Delete ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":1},{"a":3,"b":3}],"other":true})" ) );

	// The keypad's Delete (NumLock off) arrives carrying KeypadModifier.

	click_row_index ( 0 );

	QVERIFY ( press ( Qt::Key_Delete, Qt::KeypadModifier ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":3,"b":3}],"other":true})" ) );

	// A selected COLUMN: the member goes from every element.

	click_column_header ( 0 );

	QVERIFY ( press ( Qt::Key_Delete ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"b":3}],"other":true})" ) );

	// An ordinary CELL: nothing at all -- above all, not the array the table is showing.

	click_cell ( 0, 0 );

	QCOMPARE ( keyboard_holder (), form_view ()->array_table_view () );
	QVERIFY  ( !form_view ()->array_table_controller ()->header_selection ().is_active () );
	QVERIFY  ( press ( Qt::Key_Delete ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"b":3}],"other":true})" ) );

	// The TREE: the node goes, exactly as before the table answered the key.

	select_in_tree ( QStringLiteral ( "/other" ) );

	QCOMPARE ( keyboard_holder (), tree_view () );
	QVERIFY  ( press ( Qt::Key_Delete ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"b":3}]})" ) );
}

void TestMainWindow::delete_node_answers_a_selected_row_of_a_root_array ()
{
	// 15h.6's sixth item. The root cannot be deleted, so on a root array Delete Node was disabled whatever the table
	// showed; with a row selected it now answers for the row -- from the Document menu and from the toolbar, which is
	// the half no widget-level suite can see, since the toolbar button is the window's.

	settings->set_string_list
	(
		settings_keys::TOOLBAR_LAYOUT,
		{ toolbar_names::UNDO, toolbar_names::REDO, toolbar_names::DELETE_NODE }
	);

	start_window ();
	QVERIFY ( open ( R"([{"a":1},{"a":2},{"a":3}])" ) );

	QAction* const deleteNode = action ( toolbar_names::DELETE_NODE );

	QVERIFY ( deleteNode != nullptr );

	select_in_tree ( QString () );

	QVERIFY ( !deleteNode->isEnabled () );

	// The menu's route: a row selected, the action triggered.

	click_row_index ( 0 );

	QVERIFY ( deleteNode->isEnabled () );

	deleteNode->trigger ();

	QCOMPARE ( document_text (), QStringLiteral ( R"([{"a":2},{"a":3}])" ) );

	// The toolbar's route, clicked. The button takes no focus, so the table keeps the keyboard -- and the command,
	// which routes by focus, still reaches the row.

	QToolBar* const toolBar = window->findChild<QToolBar*> ( QStringLiteral ( "mainToolBar" ) );

	QVERIFY ( toolBar != nullptr );

	QWidget* const button = toolBar->widgetForAction ( deleteNode );

	QVERIFY ( button != nullptr );

	click_row_index ( 1 );

	QVERIFY ( deleteNode->isEnabled () );

	QTest::mouseClick ( button, Qt::LeftButton );

	QCOMPARE ( document_text (), QStringLiteral ( R"([{"a":2}])" ) );

	// An ordinary cell ends the row selection, and the subject is the root again.

	click_cell ( 0, 0 );

	QVERIFY ( !deleteNode->isEnabled () );

	VERIFY_MODALS ();
}

void TestMainWindow::a_row_menu_dismissed_with_escape_leaves_delete_on_the_row ()
{
	// 15h.6's eleventh item: the keyboard starts in the TREE, a right-click on a row index selects the row and opens
	// its menu, Esc closes it, and Delete then removes the ROW. Both halves are needed: the right-click must move the
	// keyboard (or Delete Node removes the tree's node, the whole array), and closing the menu must leave it there.

	start_window ();
	QVERIFY ( open ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" ) );

	select_in_tree ( QStringLiteral ( "/items" ) );

	QCOMPARE ( keyboard_holder (), tree_view () );

	expect_popup_dismissed ();

	right_click_row_index ( 1 );

	VERIFY_MODALS ();

	QCOMPARE ( keyboard_holder (), form_view ()->array_table_view () );
	QVERIFY  ( press ( Qt::Key_Delete ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":3}]})" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Paste and Paste Over
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::ctrl_v_inserts_and_ctrl_shift_v_pastes_over_in_the_window ()
{
	// 15h.7's first, eighth and ninth items. Ctrl+V is the WINDOW's shortcut (Edit > Paste) and reaches the table
	// through the cell clipboard route; Ctrl+Shift+V is the table's own. Each is checked where the other is live.

	start_window ();
	QVERIFY ( open ( R"({"items":[{"a":1},{"a":2},{"a":3}]})" ) );

	select_in_tree ( QStringLiteral ( "/items" ) );

	click_row_index ( 0 );

	QVERIFY ( press ( Qt::Key_C, Qt::ControlModifier ) );

	// Paste INSERTS in front of the selected row.

	click_row_index ( 1 );

	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":1},{"a":2},{"a":3}]})" ) );

	// Paste Over OVERWRITES the selected row.

	click_row_index ( 3 );

	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":1},{"a":2},{"a":1}]})" ) );

	// On an ordinary cell Paste Over does nothing: it names a row or a column, and a cell is neither.

	click_cell ( 2, 0 );

	QVERIFY ( !form_view ()->array_table_controller ()->header_selection ().is_active () );
	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1},{"a":1},{"a":2},{"a":1}]})" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// SET-03a, Allow duplicate keys
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::rename_key_follows_the_duplicate_key_setting_live ()
{
	// 15h.4's fifth item and 15h.5's tenth and eleventh: Rename Key onto a sibling's name is refused with the setting
	// No, succeeds with it Yes, and is refused again with it back at No -- the setting changed WHILE THE WINDOW RUNS,
	// which is the path the Settings dialog's OK takes (the store's changed signal).

	start_window ();
	QVERIFY ( open ( R"({"a":1,"b":2})" ) );

	select_in_tree ( QStringLiteral ( "/b" ) );

	expect_text_prompt ( QStringLiteral ( "a" ) );
	expect_message ();

	QVERIFY ( press ( Qt::Key_F2 ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"a":1,"b":2})" ) );

	settings->set_bool ( settings_keys::ALLOW_DUPLICATE_KEYS, true );

	select_in_tree ( QStringLiteral ( "/b" ) );

	expect_text_prompt ( QStringLiteral ( "a" ) );

	QVERIFY ( press ( Qt::Key_F2 ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"a":1,"a":2})" ) );

	// Back to No: the duplicates the document now carries stay (the setting governs what an edit may CREATE), and a
	// further one is refused.

	settings->set_bool ( settings_keys::ALLOW_DUPLICATE_KEYS, false );

	QVERIFY ( undo->can_undo () );

	undo->undo ();

	select_in_tree ( QStringLiteral ( "/b" ) );

	expect_text_prompt ( QStringLiteral ( "a" ) );
	expect_message ();

	QVERIFY ( press ( Qt::Key_F2 ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"a":1,"b":2})" ) );
}

void TestMainWindow::add_child_re_prompts_on_a_duplicate_and_accepts_one_when_allowed ()
{
	// 15h.5's tenth and eleventh items, and the defect behind them: the Add Child key prompt sits ABOVE
	// UndoController, so it refused a duplicate before the setting's policy was ever consulted. With the setting No it
	// refuses in a modal and ASKS AGAIN (the add is not lost); with it Yes it accepts at once, with no modal at all.

	start_window ();
	QVERIFY ( open ( R"({"a":1})" ) );

	QAction* const addString = action ( toolbar_names::ADD_STRING );

	QVERIFY ( addString != nullptr );

	select_in_tree ( QString () );

	expect_text_prompt ( QStringLiteral ( "a" ) );
	expect_message ();
	expect_text_prompt ( QStringLiteral ( "c" ) );

	addString->trigger ();

	VERIFY_MODALS ();

	QCOMPARE ( messages.size (), 1 );
	QCOMPARE ( messageIcons.first (), QMessageBox::Warning );   // A refusal, not a failure (STYLE-18 (5)).
	QCOMPARE ( document_text (), QStringLiteral ( R"({"a":1,"c":""})" ) );

	settings->set_bool ( settings_keys::ALLOW_DUPLICATE_KEYS, true );

	select_in_tree ( QString () );

	expect_text_prompt ( QStringLiteral ( "a" ) );

	addString->trigger ();

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"a":1,"c":"","a":""})" ) );
}

void TestMainWindow::rename_column_follows_the_duplicate_key_setting ()
{
	// 15h.5's seventh item: Rename Column onto a name another column already has is refused WHOLE, and goes through
	// with the setting on. The prompt is the window's real dialog service, reached from the column's own command.

	start_window ();
	QVERIFY ( open ( R"({"items":[{"a":1,"b":2},{"a":3,"b":4}]})" ) );

	select_in_tree ( QStringLiteral ( "/items" ) );
	click_column_header ( 1 );

	FormGridController* const controller = form_view ()->array_table_controller ();

	expect_text_prompt ( QStringLiteral ( "a" ) );
	expect_message ();

	controller->rename_table_column ();

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"b":2},{"a":3,"b":4}]})" ) );

	settings->set_bool ( settings_keys::ALLOW_DUPLICATE_KEYS, true );

	click_column_header ( 1 );

	expect_text_prompt ( QStringLiteral ( "a" ) );

	controller->rename_table_column ();

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":[{"a":1,"a":2},{"a":3,"a":4}]})" ) );
}

void TestMainWindow::the_key_editor_accepts_a_duplicate_only_when_allowed ()
{
	// 15h.5's eleventh item, the half with NO message: the in-place key editor refused a rival key as Intermediate, and
	// the delegate honoured that by declining to close -- so Enter did nothing, silently. Typed and committed here
	// exactly as a user does it, in both settings.

	start_window ();
	QVERIFY ( open ( R"({"a":1,"b":2})" ) );

	select_in_tree ( QString () );

	QTableView* const form = form_view ()->object_form_view ();

	auto rename_second_key_to_a = [ & ] ()
	{
		form->setFocus ();
		form->setCurrentIndex ( form->model ()->index ( 1, JsonFormModel::KEY_COLUMN ) );

		QVERIFY ( press ( Qt::Key_Return ) );

		QLineEdit* const editor = qobject_cast<QLineEdit*> ( keyboard_holder () );

		QVERIFY ( editor != nullptr );

		editor->selectAll ();

		QTest::keyClicks ( editor, QStringLiteral ( "a" ) );

		QVERIFY ( press ( Qt::Key_Return ) );
	};

	// No: Enter leaves the editor open on the rival, and nothing is written. Esc abandons it.

	rename_second_key_to_a ();

	QVERIFY  ( qobject_cast<QLineEdit*> ( keyboard_holder () ) != nullptr );
	QCOMPARE ( document_text (), QStringLiteral ( R"({"a":1,"b":2})" ) );

	QVERIFY ( press ( Qt::Key_Escape ) );

	// Yes: the same keystrokes commit.

	settings->set_bool ( settings_keys::ALLOW_DUPLICATE_KEYS, true );

	rename_second_key_to_a ();

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"a":1,"a":2})" ) );
}

void TestMainWindow::a_duplicate_survives_save_and_reopen_and_edits_its_own_member ()
{
	// 15h.5's twelfth, thirteenth and fourteenth items, through File > Save and File > Open: both members are written
	// and read back, the SECOND is edited in the Form View and the first is untouched -- and all of it with the setting
	// at its default No, since the setting governs what an edit may create and not what a document may contain.

	start_window ();
	QVERIFY ( open ( R"({"k":1,"k":2})" ) );

	select_in_tree ( QString () );

	QTableView* const form = form_view ()->object_form_view ();

	form->setFocus ();
	form->setCurrentIndex ( form->model ()->index ( 1, JsonFormModel::VALUE_COLUMN ) );

	QVERIFY ( press ( Qt::Key_Return ) );

	QLineEdit* const editor = qobject_cast<QLineEdit*> ( keyboard_holder () );

	QVERIFY ( editor != nullptr );

	editor->selectAll ();

	QTest::keyClicks ( editor, QStringLiteral ( "5" ) );

	QVERIFY ( press ( Qt::Key_Return ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"k":1,"k":5})" ) );

	QVERIFY ( press ( Qt::Key_S, Qt::ControlModifier ) );

	QVERIFY ( !document->is_dirty () );

	// Reopened from disk: the file, not the document in memory, is what is read.

	QVERIFY ( window->open_document_from_path ( document_path () ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"k":1,"k":5})" ) );
}

void TestMainWindow::the_selected_duplicate_is_the_one_a_command_acts_on ()
{
	// 15h.5's first three items: of two members sharing a key, the one selected in the TREE is the one Delete and
	// Rename Key act on. The reported defect deleted the first whichever was selected -- and Rename Key was DISABLED on
	// either, the one copy of the Phase 7 duplicate rule 15h.5 left behind in the window (found by this case).
	//
	// Each row is looked up afresh: an edit to the root object may rebuild the shadow item a QModelIndex points at,
	// so an index held across one is a dangling pointer rather than a stale row.

	start_window ();
	QVERIFY ( open ( R"({"name":[{"x":1}],"name":[1,2]})" ) );

	select_in_tree ( QString () );

	JsonTreeModel* const model = tree_pane ()->model ();

	auto member_row = [ model ] ( int row )
	{
		return model->index ( row, 0, model->index ( 0, 0 ) );
	};

	tree_view ()->expand ( model->index ( 0, 0 ) );

	// The SECOND, deleted: the scalars go and the objects stay.

	click_tree_index ( member_row ( 1 ) );

	QCOMPARE ( keyboard_holder (), tree_view () );
	QVERIFY  ( press ( Qt::Key_Delete ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"name":[{"x":1}]})" ) );

	// Then the FIRST, which leaves the other outcome.

	QVERIFY ( press ( Qt::Key_Z, Qt::ControlModifier ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"name":[{"x":1}],"name":[1,2]})" ) );

	click_tree_index ( member_row ( 0 ) );

	QVERIFY ( press ( Qt::Key_Delete ) );

	QCOMPARE ( document_text (), QStringLiteral ( R"({"name":[1,2]})" ) );

	// Rename Key on the second.

	QVERIFY ( press ( Qt::Key_Z, Qt::ControlModifier ) );

	click_tree_index ( member_row ( 1 ) );

	QVERIFY ( action ( toolbar_names::RENAME_KEY )->isEnabled () );

	expect_text_prompt ( QStringLiteral ( "other" ) );

	QVERIFY ( press ( Qt::Key_F2 ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"name":[{"x":1}],"other":[1,2]})" ) );
}

//---------------------------------------------------------------------------------------------------------------------
// VIEW-04
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::a_code_view_edit_marks_the_title_and_the_status_bar ()
{
	// 15h.5's eighth item: typing in the Code tab raises the title's marker and the status bar's at once -- before the
	// edit commits, while the document itself is still clean -- and both clear on the view's two ways out: Esc
	// discards, Ctrl+S commits and saves.
	//
	// The item's "in the background too" clause is NOT asserted, because it is unreachable for a valid edit: leaving
	// the Code tab runs EDITOR-09's gate, which COMMITS a valid edit on the way out rather than carrying it into the
	// background, and keeps the tab current for an invalid one. The aggregate across open views is tst_editor_pane's.

	start_window ();
	QVERIFY ( open ( R"({"a":1})" ) );

	const QString marker = QString::fromUtf8 ( config::window::TITLE_MODIFIED_MARKER );

	QVERIFY ( !window->windowTitle ().endsWith ( marker ) );
	QVERIFY ( !status_document_text ().endsWith ( QStringLiteral ( " *" ) ) );

	editor_pane ()->open_view ( CodeViewProvider::VIEW_ID );

	CodeView* const codeView = dynamic_cast<CodeView*> ( editor_pane ()->view_for_id ( CodeViewProvider::VIEW_ID ) );

	QVERIFY ( codeView != nullptr );

	codeView->editor ()->setFocus ();

	QCOMPARE ( keyboard_holder (), codeView->editor () );

	// Typed, not committed: both marks, and a clean document.

	QVERIFY ( press ( Qt::Key_End, Qt::ControlModifier ) );
	QVERIFY ( press ( Qt::Key_Space ) );

	QVERIFY ( window->windowTitle ().endsWith ( marker ) );
	QVERIFY ( status_document_text ().endsWith ( QStringLiteral ( " *" ) ) );
	QVERIFY ( !document->is_dirty () );

	// Esc discards, and both clear.

	QVERIFY ( press ( Qt::Key_Escape ) );

	QVERIFY ( !window->windowTitle ().endsWith ( marker ) );
	QVERIFY ( !status_document_text ().endsWith ( QStringLiteral ( " *" ) ) );

	// A real change, then Ctrl+S: committed, written, and both clear again.

	codeView->editor ()->selectAll ();

	QTest::keyClicks ( codeView->editor (), QStringLiteral ( R"({"a":2})" ) );

	QVERIFY ( window->windowTitle ().endsWith ( marker ) );

	QVERIFY ( press ( Qt::Key_S, Qt::ControlModifier ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"a":2})" ) );
	QVERIFY  ( !document->is_dirty () );
	QVERIFY  ( !window->windowTitle ().endsWith ( marker ) );
	QVERIFY  ( !status_document_text ().endsWith ( QStringLiteral ( " *" ) ) );
}

//---------------------------------------------------------------------------------------------------------------------
// EDITOR-16's current-cell indication
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::a_header_selection_hides_the_current_cell_and_an_arrow_restores_it ()
{
	// 15h.3's seventh item, which had been left to the eye because offscreen tests had no focus to paint (Q10) -- and
	// the current-cell indication is painted only while the table has it. Checked as a PROPERTY of rendered pixels
	// rather than a colour at a coordinate (Q20): three cells hold identical text in equal-width columns, so any
	// difference between two of them is the indication and nothing else.

	start_window ();
	QVERIFY ( open ( R"({"items":[{"a":"x","b":"x","c":"x"},{"a":"x","b":"x","c":"x"}]})" ) );

	select_in_tree ( QStringLiteral ( "/items" ) );

	QTableView* const table = form_view ()->array_table_view ();

	for ( int column = 0; column < 3; ++column )
	{
		table->horizontalHeader ()->resizeSection ( column, 80 );
	}

	auto cell_image = [ table ] ( int row, int column )
	{
		return table->viewport ()->grab ( table->visualRect ( table->model ()->index ( row, column ) ) ).toImage ();
	};

	click_cell ( 0, 0 );

	QCOMPARE ( keyboard_holder (), table );

	// The current cell is indicated: it does not look like its twin beside it.

	QVERIFY ( cell_image ( 0, 0 ) != cell_image ( 0, 1 ) );

	// A column header selection elsewhere hides it.

	click_column_header ( 2 );

	QVERIFY  ( form_view ()->array_table_controller ()->header_selection ().is_active () );
	QCOMPARE ( cell_image ( 0, 0 ), cell_image ( 0, 1 ) );

	// An arrow key ends the header selection and moves the current cell -- indicated again, where it landed.

	QVERIFY ( press ( Qt::Key_Right ) );

	QVERIFY ( !form_view ()->array_table_controller ()->header_selection ().is_active () );
	QVERIFY ( cell_image ( 0, 1 ) != cell_image ( 0, 0 ) );

	VERIFY_MODALS ();
}

//---------------------------------------------------------------------------------------------------------------------
// FIND-04, EDITOR-24, and the find bar
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::go_to_leaves_the_keyboard_in_the_tree_on_the_node_it_selected ()
{
	// The defect this harness found on its first run (15h.12) and 2026-09-25 fixed: Go To /target with the array
	// table holding the keyboard ended with the selection on /target/k. The table was swapped for the object form
	// behind the dialog, the keyboard fell to the form's grid when the dialog closed, and its first field wrote itself
	// back as the selection. Go To now leaves the keyboard in the TREE, on the node it chose.

	start_window ();
	QVERIFY ( open ( R"({"target":{"k":1},"people":[{"name":"a"},{"name":"b"}]})" ) );

	select_in_tree ( QStringLiteral ( "/people" ) );
	click_cell ( 0, 0 );

	QCOMPARE ( keyboard_holder (), form_view ()->array_table_view () );

	expect_go_to ( QStringLiteral ( "/target" ) );

	QVERIFY ( press ( Qt::Key_G, Qt::ControlModifier ) );

	VERIFY_MODALS ();

	const JsonPointer target = JsonPointer::parse ( QStringLiteral ( "/target" ) );

	QCOMPARE ( selection->selection (), target );
	QCOMPARE ( keyboard_holder (), tree_view () );
	QCOMPARE ( tree_pane ()->model ()->pointer_for_index ( tree_view ()->currentIndex () ), target );
}

void TestMainWindow::a_cancelled_go_to_puts_the_keyboard_back ()
{
	// The opposing half: a Go To that moved nothing leaves the keyboard where it was, rather than stranding it in the
	// tree the fix hands it to before the dialog opens.

	start_window ();
	QVERIFY ( open ( R"({"target":{"k":1},"people":[{"name":"a"},{"name":"b"}]})" ) );

	select_in_tree ( QStringLiteral ( "/people" ) );
	click_cell ( 0, 0 );

	const JsonPointer before = selection->selection ();

	modalScript.append ( [] ( QWidget* widget ) -> QString
	{
		QTest::keyClick ( widget, Qt::Key_Escape );

		return QString ();
	} );

	QVERIFY ( press ( Qt::Key_G, Qt::ControlModifier ) );

	VERIFY_MODALS ();

	QCOMPARE ( selection->selection (), before );
	QCOMPARE ( keyboard_holder (), form_view ()->array_table_view () );
}

void TestMainWindow::lines_pasted_from_another_application_fill_down_the_table ()
{
	// EDITOR-24 through the window's own Ctrl+V: the reported case, three lines from Notepad, which arrived as ONE
	// cell reading Four\nFive\nSix\n. Offscreen the clipboard is Qt's in-process stub (Q51), so the text is put there
	// directly -- what it holds is exactly what another application leaves: plain text and no private format.

	start_window ();
	QVERIFY ( open ( R"({"items":["p","q","r","s"]})" ) );

	select_in_tree ( QStringLiteral ( "/items" ) );
	click_cell ( 1, 0 );

	QApplication::clipboard ()->setText ( QStringLiteral ( "Four\nFive\nSix\n" ) );

	QVERIFY ( press ( Qt::Key_V, Qt::ControlModifier ) );

	VERIFY_MODALS ();

	QCOMPARE ( document_text (), QStringLiteral ( R"({"items":["p","Four","Five","Six"]})" ) );
}

void TestMainWindow::the_find_bar_closes_with_the_tab_close_glyph ()
{
	// The find bar's close button carries the TAB's cross (2026-09-25), not File > Close's document-with-an-x --
	// compared as rendered pixels against both glyphs, so a build still showing the old one fails on the second
	// comparison even where the two happened to share a size. And a click on it still closes the bar.

	start_window ();
	QVERIFY ( open ( R"({"a":1})" ) );

	QVERIFY ( press ( Qt::Key_F, Qt::ControlModifier ) );

	FindBar* const findBar = window->findChild<FindBar*> ();

	QVERIFY ( findBar != nullptr );
	QVERIFY ( findBar->isVisible () );

	QToolButton* closeButton = nullptr;

	for ( QToolButton* const button : findBar->findChildren<QToolButton*> () )
	{
		if ( button->text () == QStringLiteral ( "Close" ) )
		{
			closeButton = button;
		}
	}

	QVERIFY ( closeButton != nullptr );

	const QSize  size   ( 16, 16 );
	const QImage shown  = closeButton->icon ().pixmap ( size ).toImage ();
	const QImage tab    = icons->icon ( icon_names::TAB_CLOSE ).pixmap ( size ).toImage ();
	const QImage file   = icons->icon ( icon_names::DOCUMENT_CLOSE ).pixmap ( size ).toImage ();

	QVERIFY ( !tab.isNull () );
	QCOMPARE ( shown, tab );
	QVERIFY  ( shown != file );

	QTest::mouseClick ( closeButton, Qt::LeftButton );

	QVERIFY ( !findBar->isVisible () );

	VERIFY_MODALS ();
}

//---------------------------------------------------------------------------------------------------------------------
// Phase 15l
//---------------------------------------------------------------------------------------------------------------------

void TestMainWindow::the_mark_scope_setting_reaches_the_tree_live ()
{
	// SET-14 read at construction -- the store holds File node only before the window exists -- and again when it
	// changes while the window runs, which is the path the Settings dialog's OK takes.

	settings->set_string ( settings_keys::MARK_UNSAVED_CHANGES, settings_values::CHANGE_MARKS_FILE_NODE_ONLY );

	start_window ();
	QVERIFY ( open ( R"({"a":{"b":1}})" ) );

	JsonTreeModel* const model = tree_pane ()->model ();

	QCOMPARE ( model->change_mark_scope (), config::tree::ChangeMarkScope::FileNodeOnly );

	QCOMPARE ( undo->set_number ( JsonPointer::parse ( QStringLiteral ( "/a/b" ) ), QStringLiteral ( "2" ) ), EditOutcome::Applied );

	const QModelIndex editedIndex = model->index_for_pointer ( JsonPointer::parse ( QStringLiteral ( "/a/b" ) ) );

	QVERIFY ( !model->data ( editedIndex, JsonTreeModel::CHANGE_MARK_ROLE ).toBool () );
	QVERIFY (  model->data ( model->index ( 0, 0 ), JsonTreeModel::CHANGE_MARK_ROLE ).toBool () );

	settings->set_string ( settings_keys::MARK_UNSAVED_CHANGES, settings_values::CHANGE_MARKS_CHANGED_NODES_AND_ANCESTORS );

	QCOMPARE ( model->change_mark_scope (), config::tree::ChangeMarkScope::ChangedNodesAndAncestors );
	QVERIFY  ( model->data ( editedIndex, JsonTreeModel::CHANGE_MARK_ROLE ).toBool () );
}

void TestMainWindow::a_save_through_the_window_clears_the_marks ()
{
	start_window ();
	QVERIFY ( open ( R"({"a":{"b":1}})" ) );

	JsonTreeModel* const model = tree_pane ()->model ();

	undo->set_number ( JsonPointer::parse ( QStringLiteral ( "/a/b" ) ), QStringLiteral ( "2" ) );

	const QModelIndex editedIndex = model->index_for_pointer ( JsonPointer::parse ( QStringLiteral ( "/a/b" ) ) );

	QVERIFY ( model->data ( editedIndex, JsonTreeModel::CHANGE_MARK_ROLE ).toBool () );

	// File > Save, the window's own command, to the file the document came from.

	action ( toolbar_names::SAVE )->trigger ();

	QVERIFY ( !document->is_dirty () );
	QVERIFY ( !model->data ( editedIndex,          JsonTreeModel::CHANGE_MARK_ROLE ).toBool () );
	QVERIFY ( !model->data ( model->index ( 0, 0 ), JsonTreeModel::CHANGE_MARK_ROLE ).toBool () );

	// And an undo back past the save marks the edit again (TREE-10).

	undo->undo ();

	QVERIFY ( model->data ( editedIndex, JsonTreeModel::CHANGE_MARK_ROLE ).toBool () );
}

void TestMainWindow::every_toolbar_command_is_named_by_its_tooltip ()
{
	// STYLE-17 (1). The toolbar is icon-only, so a button's tooltip is the only thing naming it: its command, and its
	// shortcut where it has one. Every CATALOGUE command, not only those on the bar today -- any can be put there.
	// Restated as a list rather than read from the window, so a command that lost its object name fails here.

	start_window ();

	const QStringList names =
	{
		toolbar_names::NEW,  toolbar_names::OPEN,  toolbar_names::CLOSE, toolbar_names::SAVE, toolbar_names::SAVE_AS,
		toolbar_names::PAGE_SETUP, toolbar_names::PRINT, toolbar_names::SETTINGS,
		toolbar_names::VIEW_FORM, toolbar_names::VIEW_TEXT, toolbar_names::VIEW_CODE, toolbar_names::VIEW_JSONPATH,
		toolbar_names::FIND, toolbar_names::GO_TO, toolbar_names::COPY_POINTER, toolbar_names::COPY_QUERY_RESULT,
		toolbar_names::UNDO, toolbar_names::REDO, toolbar_names::CUT, toolbar_names::COPY, toolbar_names::PASTE,
		toolbar_names::ADD_OBJECT, toolbar_names::ADD_ARRAY, toolbar_names::ADD_STRING, toolbar_names::ADD_NUMBER,
		toolbar_names::ADD_BOOLEAN, toolbar_names::ADD_NULL,
		toolbar_names::RENAME_KEY, toolbar_names::DUPLICATE_NODE, toolbar_names::DELETE_NODE,
		toolbar_names::MOVE_UP, toolbar_names::MOVE_DOWN,
		toolbar_names::NORMALIZE_ARRAY, toolbar_names::ARRAY_TO_OBJECTS, toolbar_names::OBJECTS_TO_ARRAY,
		toolbar_names::CONVERT_TO_STRING, toolbar_names::CONVERT_TO_NUMBER, toolbar_names::CONVERT_TO_BOOLEAN,
		toolbar_names::CONVERT_TO_NULL,
		toolbar_names::EXPAND_ALL, toolbar_names::COLLAPSE_ALL, toolbar_names::ABOUT
	};

	for ( const QString& name : names )
	{
		QAction* const command = action ( name );

		QVERIFY2 ( command != nullptr, qPrintable ( name ) );

		const QString label    = toolbar_command_label ( command );
		const QString shortcut = command->shortcut ().toString ( QKeySequence::NativeText );
		const QString expected = shortcut.isEmpty () ? label : QStringLiteral ( "%1 (%2)" ).arg ( label, shortcut );

		QVERIFY2 ( !label.isEmpty (),                          qPrintable ( name ) );
		QVERIFY2 ( !label.contains ( QLatin1Char ( '&' ) ),    qPrintable ( name + QStringLiteral ( ": " ) + label ) );
		QCOMPARE ( command->toolTip (), expected );
	}
}

void TestMainWindow::only_the_menus_with_something_to_add_show_tooltips ()
{
	// STYLE-17 (2). A menu row already shows its label and its shortcut, so a menu shows tooltips only where they add
	// something: the export submenus, whose items say what is in the way, and Recent Files, whose items give the full
	// path the name leaves out. Every other menu leaves them off, so the tooltip each command carries for the toolbar
	// -- its label again -- is never repeated under the pointer.

	start_window ();
	QVERIFY ( open ( R"({"a":1})" ) );

	QStringList showing;
	QMenu*      recentFiles = nullptr;

	for ( QMenu* const menu : window->findChildren<QMenu*> () )
	{
		if ( menu->toolTipsVisible () )
		{
			showing.append ( menu->title () );
		}

		if ( menu->title () == QStringLiteral ( "&Recent Files" ) )
		{
			recentFiles = menu;
		}
	}

	showing.sort ();

	QCOMPARE ( showing, QStringList ( { "&Export", "&Recent Files", "Export from &Node" } ) );

	// The file just opened is on the list, named by its file name and described by its full path -- two different
	// things, which is the point of the tooltip.

	QVERIFY ( recentFiles != nullptr );

	bool found = false;

	for ( QAction* const entry : recentFiles->actions () )
	{
		if ( entry->toolTip () == tooltip_text ( document_path () ) )
		{
			found = true;

			QVERIFY2 ( entry->text ().endsWith ( QFileInfo ( document_path () ).fileName () ), qPrintable ( entry->text () ) );
			QVERIFY2 ( !entry->text ().contains ( QFileInfo ( document_path () ).absolutePath () ), qPrintable ( entry->text () ) );
		}
	}

	QVERIFY2 ( found, qPrintable ( document_path () ) );
}

void TestMainWindow::the_dot_colour_follows_the_setting_and_the_theme ()
{
	// SET-14a in the whole window: the colour reaches the tree at construction, when the setting changes while the
	// window runs, and on every theme change -- the Light theme's colour being the stored Dark one's lightness inverse.

	theme->set_theme ( Theme::Light );

	start_window ();

	const TreeNodeDelegate* const delegate = qobject_cast<const TreeNodeDelegate*> ( tree_view ()->itemDelegate () );

	QVERIFY ( delegate != nullptr );

	QCOMPARE ( delegate->mark_colour (), QColor ( 0x7F, 0x7F, 0x7F ) );     // The default, #808080, in Light.

	settings->set_string ( settings_keys::CHANGE_MARK_COLOUR, QStringLiteral ( "#336699" ) );

	QCOMPARE ( delegate->mark_colour (), QColor ( 0x66, 0x99, 0xCC ) );

	theme->set_theme ( Theme::Dark );

	QCOMPARE ( delegate->mark_colour (), QColor ( 0x33, 0x66, 0x99 ) );

	theme->set_theme ( Theme::Light );

	QCOMPARE ( delegate->mark_colour (), QColor ( 0x66, 0x99, 0xCC ) );     // And back, to the unit.
}

QTEST_MAIN ( TestMainWindow )

#include "tst_main_window.moc"
