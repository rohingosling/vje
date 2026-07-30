//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for toolbar_plan -- what the toolbar renders for a given user layout (SET-04).
//
//   THE INTERESTING CASES ARE ALL SEPARATORS, and they became more interesting on 2026-07-27. Under the superseded
//   per-button model a stray rule could only arise indirectly, by switching off every button of a group. A user
//   arranging their own layout can now author one DIRECTLY -- a leading separator, a trailing one, three in a row, a
//   layout of nothing else -- and the layout is deliberately kept verbatim rather than tidied on write, so this is the
//   only place the tidying happens. Every shape is stated here.
//
//   The other half is resolution: an entry naming no catalogue command is dropped rather than rendered as a null
//   button, so a settings file from a newer build opens instead of crashing.
//
//   Runs in the offscreen GUI harness only because QAction lives in QtGui in Qt 6; nothing here draws anything.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/toolbar_plan.hpp"

#include <QtTest/QtTest>

#include <QAction>
#include <QString>
#include <QToolBar>

#include <memory>
#include <vector>

using namespace vje;

class TestToolbarPlan : public QObject
{
	Q_OBJECT

private slots:

	void init ();

	void a_hidden_tool_button_does_not_stay_hidden ();
	void the_layout_order_is_the_rendered_order ();
	void a_command_left_out_of_the_layout_is_absent ();
	void an_emptied_group_takes_its_separator_with_it ();
	void an_authored_leading_or_trailing_separator_is_collapsed ();
	void a_run_of_separators_collapses_to_one ();
	void a_layout_of_nothing_but_separators_renders_nothing ();
	void an_entry_naming_no_command_is_dropped ();
	void an_empty_layout_renders_nothing ();

private:

	// Six commands in three notional pairs -- the smallest shape that can produce a leading, a trailing, AND a doubled
	// rule out of ordinary group removal.

	std::vector<ToolbarCommand>           catalogue;
	std::vector<std::unique_ptr<QAction>> actions;

	// A readable rendering of a plan: each button's name, and "|" for a separator.

	QStringList describe ( const std::vector<ToolbarItem>& plan ) const;

	// The default-shaped layout the cases start from: a1 a2 | b1 b2 | c1 c2.

	static QStringList grouped_layout ();
};

QStringList TestToolbarPlan::describe ( const std::vector<ToolbarItem>& plan ) const
{
	QStringList described;

	for ( const ToolbarItem& item : plan )
	{
		if ( item.separator )
		{
			described.append ( QStringLiteral ( "|" ) );

			continue;
		}

		for ( const ToolbarCommand& command : catalogue )
		{
			if ( command.action == item.action )
			{
				described.append ( command.name );

				break;
			}
		}
	}

	return described;
}

QStringList TestToolbarPlan::grouped_layout ()
{
	const QString bar = toolbar_names::SEPARATOR;

	return { "a1", "a2", bar, "b1", "b2", bar, "c1", "c2" };
}

void TestToolbarPlan::init ()
{
	catalogue.clear ();
	actions.clear ();

	const QStringList names = { "a1", "a2", "b1", "b2", "c1", "c2" };

	for ( const QString& name : names )
	{
		actions.push_back ( std::make_unique<QAction> ( name ) );

		catalogue.push_back ( { actions.back ().get (), name } );
	}
}

//---------------------------------------------------------------------------------------------------------------------
// Cases
//---------------------------------------------------------------------------------------------------------------------

void TestToolbarPlan::a_hidden_tool_button_does_not_stay_hidden ()
{
	// WHY THIS MODULE EXISTS AT ALL, measured rather than assumed. The obvious implementation of SET-04 hides the tool
	// BUTTON for a command the user left off (the QAction itself cannot be hidden -- the menus and context menus share
	// it). It does not hold: QToolBar's layout re-derives each button's visibility from its action's on every relayout,
	// so a directly hidden button comes back on its own. That was the reported defect behind lesson Q23, and it is why
	// the layout is realized as CONTENT.

	QToolBar toolBar;
	QAction  action ( QStringLiteral ( "Undo" ) );

	toolBar.addAction ( &action );

	QWidget* const button = toolBar.widgetForAction ( &action );

	QVERIFY ( button != nullptr );

	button->setVisible ( false );

	QVERIFY ( button->isHidden () );

	// A relayout -- which in the application is any show, resize, or action change.

	toolBar.show ();

	QCoreApplication::processEvents ();

	QVERIFY2 ( !button->isHidden (), "QToolBar no longer overrides a hidden button -- the content-based plan may be reconsidered" );
}

void TestToolbarPlan::the_layout_order_is_the_rendered_order ()
{
	// The capability the transfer list added: the layout is not a filter over a fixed order, it IS the order. A layout
	// that reverses the catalogue renders reversed.

	QCOMPARE ( describe ( toolbar_plan ( grouped_layout (), catalogue ) ),
	           QStringList ( { "a1", "a2", "|", "b1", "b2", "|", "c1", "c2" } ) );

	const QStringList reversed = { "c2", "c1", toolbar_names::SEPARATOR, "b2", "a1" };

	QCOMPARE ( describe ( toolbar_plan ( reversed, catalogue ) ),
	           QStringList ( { "c2", "c1", "|", "b2", "a1" } ) );
}

void TestToolbarPlan::a_command_left_out_of_the_layout_is_absent ()
{
	QStringList layout = grouped_layout ();

	layout.removeAll ( QStringLiteral ( "b1" ) );

	QCOMPARE ( describe ( toolbar_plan ( layout, catalogue ) ),
	           QStringList ( { "a1", "a2", "|", "b2", "|", "c1", "c2" } ) );
}

void TestToolbarPlan::an_emptied_group_takes_its_separator_with_it ()
{
	// Both rules around the middle group would otherwise survive and land side by side.

	QStringList layout = grouped_layout ();

	layout.removeAll ( QStringLiteral ( "b1" ) );
	layout.removeAll ( QStringLiteral ( "b2" ) );

	QCOMPARE ( describe ( toolbar_plan ( layout, catalogue ) ),
	           QStringList ( { "a1", "a2", "|", "c1", "c2" } ) );
}

void TestToolbarPlan::an_authored_leading_or_trailing_separator_is_collapsed ()
{
	// Reachable DIRECTLY now: a user may drop a separator at either end of their layout, and the stored layout keeps it
	// (so the dialog shows them what they arranged) while the bar does not draw a rule against nothing.

	const QString bar = toolbar_names::SEPARATOR;

	QCOMPARE ( describe ( toolbar_plan ( { bar, "a1", "a2" }, catalogue ) ),
	           QStringList ( { "a1", "a2" } ) );

	QCOMPARE ( describe ( toolbar_plan ( { "a1", "a2", bar }, catalogue ) ),
	           QStringList ( { "a1", "a2" } ) );

	QCOMPARE ( describe ( toolbar_plan ( { bar, "a1", bar }, catalogue ) ),
	           QStringList ( { "a1" } ) );
}

void TestToolbarPlan::a_run_of_separators_collapses_to_one ()
{
	const QString bar = toolbar_names::SEPARATOR;

	QCOMPARE ( describe ( toolbar_plan ( { "a1", bar, bar, bar, "b1" }, catalogue ) ),
	           QStringList ( { "a1", "|", "b1" } ) );
}

void TestToolbarPlan::a_layout_of_nothing_but_separators_renders_nothing ()
{
	const QString bar = toolbar_names::SEPARATOR;

	// Not "three separators": with no button to separate, there is nothing to draw.

	QVERIFY ( toolbar_plan ( { bar, bar, bar }, catalogue ).empty () );
}

void TestToolbarPlan::an_entry_naming_no_command_is_dropped ()
{
	// A command this build has not got -- a settings file written by a newer one, or a command withdrawn since. The bar
	// renders what it understands rather than refusing the whole layout.

	const QStringList layout = { "a1", QStringLiteral ( "aCommandFromTheFuture" ), "a2" };

	QCOMPARE ( describe ( toolbar_plan ( layout, catalogue ) ), QStringList ( { "a1", "a2" } ) );

	// And it does not keep a separator alive on its own account.

	QVERIFY ( toolbar_plan ( { toolbar_names::SEPARATOR, QStringLiteral ( "phantom" ) }, catalogue ).empty () );
}

void TestToolbarPlan::an_empty_layout_renders_nothing ()
{
	// SET-04 permits an empty toolbar outright; the window hides the bar rather than drawing an empty strip.

	QVERIFY ( toolbar_plan ( QStringList (), catalogue ).empty () );
}

QTEST_MAIN ( TestToolbarPlan )

#include "tst_toolbar_plan.moc"
