//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   A TEMPORARY measurement harness for the array column clipboard (EDITOR-18, EDIT-16), written to answer one
//   question: which part of a column copy / paste / delete actually costs the time?
//
//   It times the same gestures through TWO paths at several array sizes:
//
//     - the WIDGET path, at FOUR observer sets, because the cost is not the edit but the reaction to it and the
//       reaction depends on WHICH views are alive. The first draft of this file built a FormView alone while its own
//       comment claimed the tree model was there too -- which made every number a best case, and would have ranked
//       the wrong cause. The sets are cumulative: FormView; + JsonTreeModel; + TextView; + CodeView.
//     - the CORE path, UndoController alone with no model attached, which is the same document work with none of the
//       projection or signal traffic.
//
//   The gap between them is the cost of the model/view amplification; the growth of each with n is the complexity.
//   Reporting both is what turns "it feels slow" into a ranked cause.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/JsonTableModel.hpp"
#include "models/JsonTreeModel.hpp"
#include "views/CodeView.hpp"
#include "views/TextView.hpp"
#include "services/ClipboardService.hpp"
#include "services/SelectionService.hpp"
#include "views/FormGridController.hpp"
#include "views/FormView.hpp"
#include "views/GridHeaderView.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeView>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QWidget>

#include <memory>

using namespace vje;

namespace
{
	// An array of `count` objects, each with two members, so the table is a real two-column projection.

	// `count` elements in the array the gestures act on, plus `ballast` elements ELSEWHERE in the same document.
	//
	// The two are separated because the views that cost the most do not read the array: CodeView re-serializes,
	// re-highlights and re-parses the WHOLE document on every notification, and TextView re-renders. Every earlier
	// draft of this file made the document BE the array, so the cost of a gesture appeared to scale with the column
	// -- which is exactly the variable a user copying nine rows out of a large file is not changing.

	QString array_of_objects ( int count, int ballast = 0 )
	{
		QStringList elements;

		for ( int index = 0; index < count; ++index )
		{
			elements << QStringLiteral ( "{\"a\":\"v%1\",\"b\":\"w%1\"}" ).arg ( index );
		}

		QStringList filler;

		for ( int index = 0; index < ballast; ++index )
		{
			filler << QStringLiteral ( "{\"k%1\":\"padding value %1\",\"n\":%1}" ).arg ( index );
		}

		return QStringLiteral ( "{\"items\":[%1],\"other\":[%2]}" )
		       .arg ( elements.join ( QLatin1Char ( ',' ) ), filler.join ( QLatin1Char ( ',' ) ) );
	}

	const QList<int> SIZES = { 9, 400 };

	// Elements elsewhere in the document -- the size of the FILE the nine copied rows are sitting in.

	const int BALLAST = 4000;

	// Which observers are alive. Cumulative: each set adds one to the previous.

	enum class Observers { FormOnly, PlusTree, PlusText, PlusCode };

	const char* observer_name ( Observers observers )
	{
		switch ( observers )
		{
			case Observers::FormOnly: return "form only";
			case Observers::PlusTree: return "+ tree";
			case Observers::PlusText: return "+ text";
			case Observers::PlusCode: return "+ CODE";
		}

		return "?";
	}
}

class TestClipboardBenchmark : public QObject
{
	Q_OBJECT

private slots:

	void the_widget_path ();
	void the_widget_path_data ();
	void the_core_path ();

private:

	void report ( const QString& what, int size, qint64 nanoseconds );
};

void TestClipboardBenchmark::report ( const QString& what, int size, qint64 nanoseconds )
{
	// MICROseconds. A 9-element gesture is reported as taking "a number of milliseconds", which millisecond
	// granularity cannot distinguish from zero -- and the question is precisely how far above zero it is.

	qInfo
	(
		"%-34s n=%-6d %9lld us", qPrintable ( what ), size, static_cast<long long> ( nanoseconds / 1000 )
	);
}

void TestClipboardBenchmark::the_widget_path_data ()
{
	QTest::addColumn<int> ( "observers" );
	QTest::addColumn<int> ( "ballast" );

	QTest::newRow ( "form only" )          << static_cast<int> ( Observers::FormOnly ) << 0;
	QTest::newRow ( "form only, ballast" ) << static_cast<int> ( Observers::FormOnly ) << BALLAST;
	QTest::newRow ( "+ tree" )             << static_cast<int> ( Observers::PlusTree ) << 0;
	QTest::newRow ( "+ tree, ballast" )    << static_cast<int> ( Observers::PlusTree ) << BALLAST;
	QTest::newRow ( "+ text" )             << static_cast<int> ( Observers::PlusText ) << 0;
	QTest::newRow ( "+ text, ballast" )    << static_cast<int> ( Observers::PlusText ) << BALLAST;
	QTest::newRow ( "+ CODE" )             << static_cast<int> ( Observers::PlusCode ) << 0;
	QTest::newRow ( "+ CODE, ballast" )    << static_cast<int> ( Observers::PlusCode ) << BALLAST;
}

void TestClipboardBenchmark::the_widget_path ()
{
	QFETCH ( int, observers );
	QFETCH ( int, ballast );

	const Observers set = static_cast<Observers> ( observers );

	QTemporaryDir settingsDirectory;

	for ( const int size : SIZES )
	{
		auto document  = std::make_unique<JsonDocument> ();
		auto undo      = std::make_unique<UndoController> ( document.get () );
		auto selection = std::make_unique<SelectionService> ();
		auto settings  = std::make_unique<SettingsStore> ( settingsDirectory.filePath ( QStringLiteral ( "s.json" ) ) );
		auto clipboard = std::make_unique<ClipboardService> ( QApplication::clipboard () );

		ParseResult parsed = JsonParser::parse ( array_of_objects ( size, ballast ) );
		document->set_root ( std::move ( parsed.root ) );

		// The tree MODEL is declared before the window that shows it, so it is destroyed after it.

		std::unique_ptr<JsonTreeModel> tree;

		// ONE WINDOW HOLDING EVERY VIEW, SHOWN. Two earlier drafts of this file understated the cost in the same way:
		// the observers were constructed but never shown, and the tree model was given no view at all -- so a
		// notification cost a model rebuild and none of the layout, delegate sizing and painting the rebuild triggers
		// in the running application, which is where the time actually goes.

		// The editor views go in a STACK with the Form View current, because that is what EditorPane is: they are
		// tabs, so at most one of them is visible and the rest are alive, observing, and unseen. A layout showing all
		// three side by side measures a window nobody has -- and, worse, hides the one property that matters here,
		// which is what a BACKGROUND view costs.

		QWidget window;

		auto* const layout = new QHBoxLayout ( &window );
		auto* const stack  = new QStackedWidget;

		auto* const view = new FormView
		(
			document.get (), undo.get (), selection.get (), settings.get (), nullptr, clipboard.get (), nullptr
		);

		stack->addWidget ( view );

		// The other observers of the same document, added cumulatively. Each is what the real application has alive
		// when the corresponding tab is open -- and VIEW-03 opens Form, Text and Code on a first run.

		QTreeView* treeView = nullptr;
		TextView*  text     = nullptr;
		CodeView*  code     = nullptr;

		if ( set >= Observers::PlusTree )
		{
			tree     = std::make_unique<JsonTreeModel> ( document.get () );
			treeView = new QTreeView;

			treeView->setModel ( tree.get () );

			layout->addWidget ( treeView );   // The tree is its own pane, always visible -- not a tab.
		}

		if ( set >= Observers::PlusText )
		{
			text = new TextView ( document.get (), settings.get () );

			stack->addWidget ( text );
		}

		if ( set >= Observers::PlusCode )
		{
			code = new CodeView ( document.get (), undo.get (), settings.get (), nullptr, nullptr );

			stack->addWidget ( code );
		}

		layout->addWidget ( stack );

		stack->setCurrentWidget ( view );

		window.resize ( 1200, 900 );
		window.show ();

		view->present ( JsonPointer::parse ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );

		if ( treeView != nullptr )
		{
			treeView->expandAll ();
		}

		if ( text != nullptr )
		{
			text->present ( JsonPointer::parse ( QStringLiteral ( "/items" ) ), SelectionOrigin::Programmatic );
		}

		if ( code != nullptr )
		{
			code->present ( JsonPointer (), SelectionOrigin::Programmatic );
		}

		// THE TIMED REGION INCLUDES THE RETURN TO THE EVENT LOOP. What a user perceives is not the edit but the moment
		// the window is correct again, and every repaint a notification triggers happens after the handler returns --
		// so a timer stopped at the end of the handler measures only the half of the gesture that is fast.

		const auto settle = []
		{
			QCoreApplication::sendPostedEvents ();
			QCoreApplication::processEvents ();
		};

		// OleSetClipboard FAILS under contention and Qt retries internally, which once hung this run for five minutes.
		// A pause before each clipboard gesture keeps the harness measuring our cost rather than another process's hold
		// on the clipboard -- it sits OUTSIDE every timed region.

		const auto settle_clipboard = [ & ] { QTest::qWait ( 50 ); settle (); };

		settle ();

		QElapsedTimer timer;

		// -- Copy -------------------------------------------------------------------------------------------------

		view->array_table_controller ()->select_current_column ();
		view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 0, 0 ) );
		view->array_table_controller ()->select_current_column ();

		settle_clipboard ();

		timer.start ();
		view->cell_copy ();
		settle ();
		report ( QStringLiteral ( "copy   [%1]" ).arg ( QLatin1String ( observer_name ( set ) ) ), size, timer.nsecsElapsed () );

		// -- Paste onto the OTHER column -------------------------------------------------------------------------

		view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 0, 1 ) );
		view->array_table_controller ()->select_current_column ();

		settle_clipboard ();

		timer.restart ();
		view->cell_paste ();
		settle ();
		report ( QStringLiteral ( "paste  [%1]" ).arg ( QLatin1String ( observer_name ( set ) ) ), size, timer.nsecsElapsed () );

		// -- Delete ---------------------------------------------------------------------------------------------

		view->array_table_view ()->setCurrentIndex ( view->table_model ()->index ( 0, 1 ) );
		view->array_table_controller ()->select_current_column ();

		settle ();

		timer.restart ();
		view->cell_delete ();
		settle ();
		report ( QStringLiteral ( "delete [%1]" ).arg ( QLatin1String ( observer_name ( set ) ) ), size, timer.nsecsElapsed () );
	}
}

void TestClipboardBenchmark::the_core_path ()
{
	// The same document work with NO model observing it: UndoController alone. Whatever the widget path costs above
	// this is the projection and signal traffic rather than the edit.

	for ( const int size : SIZES )
	{
		JsonDocument   document;
		UndoController undo { &document };

		ParseResult parsed = JsonParser::parse ( array_of_objects ( size ) );
		document.set_root ( std::move ( parsed.root ) );
		undo.clear ();

		std::vector<UndoController::PastedValue> values;

		for ( int index = 0; index < size; ++index )
		{
			values.push_back ( { JsonNode::make_string ( QStringLiteral ( "w%1" ).arg ( index ) ) } );
		}

		QElapsedTimer timer;
		timer.start ();

		undo.paste_value_list
		(
			JsonPointer::parse ( QStringLiteral ( "/items" ) ), values, QStringLiteral ( "c" ), PasteRoute::Node
		);

		report ( "core: paste_value_list", size, timer.nsecsElapsed () );

		// And a delete of every element of one column, the same shape delete_column applies.

		QList<JsonPointer> targets;

		for ( int index = 0; index < size; ++index )
		{
			targets.append ( JsonPointer::parse ( QStringLiteral ( "/items/%1/a" ).arg ( index ) ) );
		}

		timer.restart ();

		{
			UndoController::MacroScope macro ( undo, QStringLiteral ( "Delete Column" ) );

			for ( const JsonPointer& target : targets )
			{
				undo.delete_node ( target );
			}
		}

		report ( "core: delete column", size, timer.nsecsElapsed () );
	}
}

QTEST_MAIN ( TestClipboardBenchmark )

#include "tst_clipboard_benchmark.moc"
