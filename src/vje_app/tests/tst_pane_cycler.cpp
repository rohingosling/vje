//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   PaneCycler unit tests (NAV-04).
//
//   WHAT IS TESTED, AND WHY NOT THE KEY ITSELF. The offscreen platform these suites run on grants keyboard focus to
//   nothing, so a real Tab cannot be delivered to a real focus widget and every assertion about
//   the event path would be measuring the platform. The class is therefore built with its decisions reachable without
//   focus -- cycle(), pane_index_of(), and is_open_cell_editor() -- and those are what is asserted here. Whether a
//   Tab keystroke reaches cycle() is a manual smoke item.
//
//   THE ONE WORTH READING is the open-cell-editor case. It is the whole reason Tab does not throw the user across the
//   workspace while they are halfway through typing a value, and it turns on a distinction invisible from the outside:
//   a cell editor is parented into the item view's VIEWPORT, while the scroll bars and headers hang off the view
//   itself. Getting that backwards would either strand Tab inside every grid or steal it from every open editor.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/PaneCycler.hpp"

#include <QLineEdit>
#include <QScrollBar>
#include <QStringList>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>
#include <QWidget>

#include <memory>

using namespace vje;

//*********************************************************************************************************************
// Class: TestPaneCycler
//*********************************************************************************************************************

class TestPaneCycler : public QObject
{
	Q_OBJECT

private:

	std::unique_ptr<PaneCycler> cycler;

	// Two stand-in panes and a record of which was asked to take the keyboard, in order. Names rather than indices, so
	// a failure reads as "it went to the wrong pane" instead of as arithmetic.

	std::unique_ptr<QWidget> leftPane;
	std::unique_ptr<QWidget> rightPane;

	QStringList focused;

private slots:

	void init ()
	{
		cycler = std::make_unique<PaneCycler> ();

		leftPane  = std::make_unique<QWidget> ();
		rightPane = std::make_unique<QWidget> ();

		focused.clear ();

		cycler->add_pane ( leftPane.get  (), [ this ] () { focused.append ( QStringLiteral ( "left"  ) ); } );
		cycler->add_pane ( rightPane.get (), [ this ] () { focused.append ( QStringLiteral ( "right" ) ); } );
	}

	void cleanup ()
	{
		// The cycler filters the application, so it goes first -- outliving the widgets it names would leave it
		// answering questions about freed memory.

		cycler.reset ();

		rightPane.reset ();
		leftPane.reset ();
	}

	//=================================================================================================================
	// Locating the focus
	//=================================================================================================================

	void a_pane_owns_itself_and_everything_inside_it ()
	{
		QWidget* const child      = new QWidget ( leftPane.get () );
		QWidget* const grandchild = new QWidget ( child );

		QCOMPARE ( cycler->pane_index_of ( leftPane.get () ), 0 );
		QCOMPARE ( cycler->pane_index_of ( child ),           0 );
		QCOMPARE ( cycler->pane_index_of ( grandchild ),      0 );

		QCOMPARE ( cycler->pane_index_of ( rightPane.get () ), 1 );
	}

	// A dialog, a menu, the toolbar. Tab there is ordinary widget navigation and the cycler must keep its hands off it.

	void a_widget_in_no_pane_belongs_to_no_pane ()
	{
		QWidget stranger;

		QCOMPARE ( cycler->pane_index_of ( &stranger ), -1 );
		QCOMPARE ( cycler->pane_index_of ( nullptr ),   -1 );
	}

	//=================================================================================================================
	// Cycling
	//=================================================================================================================

	// Focus is nowhere at all under the offscreen platform, which is also the real state right after a click on the
	// menu bar -- so the entry rule is exercised here rather than merely assumed.

	void cycling_from_outside_the_ring_enters_at_the_near_end ()
	{
		cycler->cycle ( true );

		QCOMPARE ( focused, QStringList { QStringLiteral ( "left" ) } );

		focused.clear ();

		cycler->cycle ( false );

		QCOMPARE ( focused, QStringList { QStringLiteral ( "right" ) } );
	}

	void an_empty_ring_has_nowhere_to_go ()
	{
		PaneCycler empty;

		empty.cycle ( true );
		empty.cycle ( false );

		// Nothing to assert but the absence of a crash: an empty ring must not index into itself.

		QVERIFY ( true );
	}

	//=================================================================================================================
	// What Tab is not taken from
	//=================================================================================================================

	// The distinction the whole rule turns on. A cell editor lives in the VIEWPORT; the grid's own furniture does not.

	void an_editor_in_the_viewport_is_an_open_cell_editor ()
	{
		QTableWidget grid ( 2, 2 );

		QLineEdit* const cellEditor = new QLineEdit ( grid.viewport () );

		QVERIFY ( PaneCycler::is_open_cell_editor ( cellEditor ) );
	}

	void the_grid_itself_is_not_an_open_cell_editor ()
	{
		QTableWidget grid ( 2, 2 );
		QTreeWidget  tree;

		QVERIFY ( !PaneCycler::is_open_cell_editor ( &grid ) );
		QVERIFY ( !PaneCycler::is_open_cell_editor ( &tree ) );
	}

	// The trap in the viewport rule: a scroll bar is a child of the VIEW, not of the viewport. Treating it as an editor
	// would quietly disable Tab whenever the keyboard happened to be on one.

	void a_scroll_bar_is_not_an_open_cell_editor ()
	{
		QTableWidget grid ( 2, 2 );

		QVERIFY ( !PaneCycler::is_open_cell_editor ( grid.verticalScrollBar   () ) );
		QVERIFY ( !PaneCycler::is_open_cell_editor ( grid.horizontalScrollBar () ) );
	}

	void a_widget_outside_any_grid_is_not_an_open_cell_editor ()
	{
		QLineEdit loose;

		QVERIFY ( !PaneCycler::is_open_cell_editor ( &loose ) );
		QVERIFY ( !PaneCycler::is_open_cell_editor ( nullptr ) );
	}
};

QTEST_MAIN ( TestPaneCycler )

#include "tst_pane_cycler.moc"
