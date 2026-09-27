//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tst_title_bar -- STYLE-19's rule and its reach: the title bar a step darker than the window on BOTH themes, the
//   user's own colour left alone, and every window with a title bar coloured by one
//   application-wide filter.
//
//   THE PLATFORM CALL IS FAKED. What reaches Windows -- and the dark flag Qt derives from VJE's palette -- is
//   tst_title_bar_windows's; this suite asks what TitleBarSync asked FOR, and of which windows, which the offscreen
//   platform can answer on every host.
//
//   THE DIRECTION CLAIM IS WRITTEN FOR THE DARK THEME, deliberately: on a light ground "darker" and "away from the
//   nearer end" agree, so a title bar cut with contrasting_tone would pass every light-theme case here and lift the
//   dark theme's title bar instead of lowering it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "dialogs/MessageBox.hpp"
#include "services/ThemeService.hpp"
#include "services/TitleBarSync.hpp"
#include "style/title_bar_surface.hpp"
#include "style/tone.hpp"

#include <QtTest/QtTest>

#include <QApplication>
#include <QDialog>
#include <QMainWindow>
#include <QMenu>
#include <QPalette>
#include <QWidget>

#include <memory>
#include <optional>

using namespace vje;

namespace
{
	// One call the filter made: to which window, asking for which colour.

	struct AppliedCaption
	{
		QWidget*              window;
		std::optional<QColor> caption;
	};

	QPalette palette_for ( bool dark )
	{
		ThemeService::apply_look ( config::appearance::DEFAULT_INTERFACE_STYLE, dark );

		return QApplication::palette ();
	}

	bool shown ( QWidget& window )
	{
		window.show ();

		return QTest::qWaitForWindowExposed ( &window );
	}
}

//*********************************************************************************************************************
// Class: TestTitleBar
//*********************************************************************************************************************

class TestTitleBar : public QObject
{
	Q_OBJECT

private:

	QList<AppliedCaption> applied;
	bool                  userOwns = false;

	// A sync whose platform call records rather than calls, and whose precedence the case sets.

	std::unique_ptr<TitleBarSync> recording_sync ()
	{
		return std::make_unique<TitleBarSync>
		(
			[ this ] ( QWidget& window, const std::optional<QColor>& caption ) { applied.append ( { &window, caption } ); },
			[ this ] () { return userOwns; }
		);
	}

	int count_for ( const QWidget* window ) const
	{
		int count = 0;

		for ( const AppliedCaption& entry : applied )
		{
			count += ( entry.window == window ) ? 1 : 0;
		}

		return count;
	}

private slots:

	void init ()
	{
		applied.clear ();

		userOwns = false;

		palette_for ( false );
	}

	//=================================================================================================================
	// The colour
	//=================================================================================================================

	void darker_tone_goes_darker_on_a_dark_ground_too ()
	{
		const QColor dark  = palette_for ( true  ).color ( QPalette::Window );
		const QColor light = palette_for ( false ).color ( QPalette::Window );

		QVERIFY ( dark.lightness () < 128 );   // Guard: the dark theme's surface really is on the dark half.

		const QColor darkResult  = darker_tone ( dark,  config::title_bar::DARKER_STEP );
		const QColor lightResult = darker_tone ( light, config::title_bar::DARKER_STEP );

		QCOMPARE ( darkResult.lightness  (), dark.lightness  () - config::title_bar::DARKER_STEP );
		QCOMPARE ( lightResult.lightness (), light.lightness () - config::title_bar::DARKER_STEP );

		// Hue kept: the dark surface's faint blue survives into its title bar.

		QCOMPARE ( darkResult.hslHue (), dark.hslHue () );
	}

	void the_title_bar_colours_are_the_measured_ones ()
	{
		// Dark reproduces Windows' own dark caption (#202020, measured) in lightness; light gets the same step below
		// #F3F3F3. Compared as RGB where a colour is named: the tone is built in HSL, and QColor's equality compares the
		// colour spec as well as the colour.

		QCOMPARE ( title_bar_colour ( palette_for ( true ) ).lightness (), QColor ( 0x20, 0x20, 0x20 ).lightness () );
		QCOMPARE ( title_bar_colour ( palette_for ( false ) ).rgb (), qRgb ( 0xE4, 0xE4, 0xE4 ) );
	}

	void the_users_own_colour_is_asked_for_as_no_colour ()
	{
		QVERIFY  ( title_bar_caption ( palette_for ( false ), false ).has_value () );
		QCOMPARE ( title_bar_caption ( palette_for ( false ), false )->rgb (), title_bar_colour ( palette_for ( false ) ).rgb () );
		QVERIFY  ( !title_bar_caption ( palette_for ( false ), true ).has_value () );
	}

	//=================================================================================================================
	// The filter
	//=================================================================================================================

	void every_window_with_a_title_bar_is_coloured_when_shown ()
	{
		const std::unique_ptr<TitleBarSync> sync = recording_sync ();

		QMainWindow window;
		QDialog     dialog;
		MessageBox  box ( MessageKind::Information, QStringLiteral ( "T" ), QStringLiteral ( "Text" ), QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		QVERIFY ( shown ( window ) );
		QVERIFY ( shown ( dialog ) );
		QVERIFY ( shown ( box ) );

		QCOMPARE ( count_for ( &window ), 1 );
		QCOMPARE ( count_for ( &dialog ), 1 );
		QCOMPARE ( count_for ( &box ),    1 );

		QVERIFY  ( applied.first ().caption.has_value () );
		QCOMPARE ( applied.first ().caption->rgb (), qRgb ( 0xE4, 0xE4, 0xE4 ) );
	}

	void a_window_without_a_title_bar_is_left_alone ()
	{
		const std::unique_ptr<TitleBarSync> sync = recording_sync ();

		QMenu   menu;
		QWidget frameless ( nullptr, Qt::FramelessWindowHint );

		menu.addAction ( QStringLiteral ( "Item" ) );

		QVERIFY ( shown ( frameless ) );

		menu.popup ( QPoint ( 10, 10 ) );

		QVERIFY ( QTest::qWaitForWindowExposed ( &menu ) );

		QVERIFY2 ( applied.isEmpty (), qPrintable ( QStringLiteral ( "%1 unexpected requests" ).arg ( applied.size () ) ) );

		menu.close ();
	}

	void refresh_recolours_every_visible_window_from_the_new_palette ()
	{
		// From the APPLICATION's palette: the window's own has not caught up when refresh () runs straight after the
		// palette is set (lesson Q9) -- measured, the first version coloured a switch to Dark from the old light surface.

		const std::unique_ptr<TitleBarSync> sync = recording_sync ();

		QMainWindow window;
		QDialog     hidden;

		QVERIFY ( shown ( window ) );

		applied.clear ();

		palette_for ( true );

		sync->refresh ();

		QCOMPARE ( count_for ( &window ), 1 );
		QCOMPARE ( count_for ( &hidden ), 0 );   // Not shown: it is coloured when it is.

		QCOMPARE ( applied.first ().caption->rgb (), title_bar_colour ( palette_for ( true ) ).rgb () );
	}

	void the_users_own_colour_reaches_the_platform_as_no_colour ()
	{
		const std::unique_ptr<TitleBarSync> sync = recording_sync ();

		userOwns = true;

		QDialog dialog;

		QVERIFY ( shown ( dialog ) );

		QCOMPARE ( count_for ( &dialog ), 1 );
		QVERIFY  ( !applied.first ().caption.has_value () );
	}
};

QTEST_MAIN ( TestTitleBar )

#include "tst_title_bar.moc"
