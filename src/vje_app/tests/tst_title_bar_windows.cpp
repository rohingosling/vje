//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tst_title_bar_windows -- STYLE-19 on a REAL window on the real windows platform. WINDOWS ONLY, and deliberately not
//   offscreen: an offscreen window has no DWM frame to set anything on.
//
//   DARK OR LIGHT IS READ BACK FROM WINDOWS. The dark flag is Qt's, derived from the window's palette, and it is what
//   DwmGetWindowAttribute returns. The case sets VJE's theme AGAINST the OS's and pins that the flag follows VJE --
//   through a later palette change too, the event on which Qt re-derives it. It pins a Qt behaviour VJE relies on for
//   STYLE-19's "follows VJE's theme", so a Qt release that changed it fails here rather than in a user's title bar.
//
//   THE COLOUR IS READ OFF THE SCREEN. DwmGetWindowAttribute answers E_INVALIDARG for the caption colour (measured,
//   Windows 11 build 26200), so the colour case grabs the pixel in the middle of the title bar. It first grabs a pixel
//   of the window's own client area, filled with a known colour: where that does not come back -- no desktop to read,
//   as on a CI runner -- the case SKIPS rather than passing without having looked. Where the platform refuses the colour
//   (Windows 10), it skips too, and says so.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/ThemeService.hpp"

#include <platform/title_bar.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QApplication>
#include <QPalette>
#include <QPixmap>
#include <QScreen>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QWidget>

#include <windows.h>
#include <dwmapi.h>

namespace
{
	constexpr DWORD USE_IMMERSIVE_DARK_MODE = 20;

	const QColor CLIENT_FILL ( 0x12, 0x80, 0x34 );   // Nothing a title bar would ever be.

	// Deliberately NOT a grey: three different bytes, so a caption sent in the wrong byte order (COLORREF is 0x00BBGGRR,
	// the reverse of what the platform call is given) reads back as a different colour rather than the same one.

	constexpr QRgb TEST_CAPTION = 0xD0E4F8;

	// A plain window with an empty title, so the caption's middle is caption, not text.

	void prepare ( QWidget& window )
	{
		window.setWindowTitle ( QStringLiteral ( " " ) );
		window.setGeometry    ( 200, 200, 420, 160 );
	}

	QColor screen_pixel ( const QWidget& window, const QPoint& globalPoint )
	{
		return window.screen ()->grabWindow ( 0, globalPoint.x (), globalPoint.y (), 1, 1 ).toImage ().pixelColor ( 0, 0 );
	}

	bool dark_flag ( const QWidget& window )
	{
		BOOL value = FALSE;

		DwmGetWindowAttribute ( reinterpret_cast<HWND> ( window.winId () ), USE_IMMERSIVE_DARK_MODE, &value, sizeof ( value ) );

		return value != FALSE;
	}
}

//*********************************************************************************************************************
// Class: TestTitleBarWindows
//*********************************************************************************************************************

class TestTitleBarWindows : public QObject
{
	Q_OBJECT

private slots:

	// The application's own base style. Qt's native Windows 11 style paints a window's background itself under a dark
	// OS and ignores the palette's fill (measured: the client read back #181716), which would leave the screen guard
	// below nothing known to look for.

	void initTestCase ()
	{
		QApplication::setStyle ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) );
	}

	void cleanup ()
	{
		QGuiApplication::styleHints ()->setColorScheme ( Qt::ColorScheme::Unknown );
	}

	void vjes_theme_owns_the_dark_flag_through_a_palette_change ()
	{
		const Qt::ColorScheme os = QGuiApplication::styleHints ()->colorScheme ();

		if ( os == Qt::ColorScheme::Unknown )
		{
			QSKIP ( "the OS reports no colour scheme to disagree with" );
		}

		// VJE's theme against the OS's: this is the mismatch STYLE-19 ends.

		const bool       vjeDark = ( os == Qt::ColorScheme::Light );
		const vje::Theme theme = vjeDark ? vje::Theme::Dark : vje::Theme::Light;

		QTemporaryDir       directory;
		vje::SettingsStore  settings ( directory.filePath ( QStringLiteral ( "settings.json" ) ) );
		vje::ThemeService   themes   ( &settings );

		// set_theme may return early -- a fresh service is already on Light -- so apply () as the composition root does.

		themes.set_theme ( theme );
		themes.apply     ();

		QWidget window;

		prepare ( window );
		window.show ();

		QVERIFY ( QTest::qWaitForWindowExposed ( &window ) );

		QTRY_COMPARE ( dark_flag ( window ), vjeDark );

		// The palette changes again -- any theme application does it -- and Qt re-derives the flag from it.

		themes.apply ();

		QTest::qWait ( 200 );

		QCOMPARE ( dark_flag ( window ), vjeDark );
	}

	void the_caption_takes_the_colour_asked_for ()
	{
		QWidget window;

		prepare ( window );

		QPalette palette = window.palette ();

		palette.setColor ( QPalette::Window, CLIENT_FILL );

		window.setPalette            ( palette );
		window.setAutoFillBackground ( true );

		// On top, because a test is a background process and Windows will not let one bring its window forward: without
		// it the guard read the window in front (#1B1B1B) and the case skipped on a machine that has a desktop.

		window.setWindowFlag ( Qt::WindowStaysOnTopHint );
		window.show ();
		window.activateWindow ();

		QVERIFY ( QTest::qWaitForWindowActive ( &window ) );

		QTest::qWait ( 300 );

		// Guard: the screen can be read at all.

		if ( screen_pixel ( window, window.mapToGlobal ( QPoint ( 20, 20 ) ) ) != CLIENT_FILL )
		{
			QSKIP ( "no desktop to read the screen from" );
		}

		const quintptr handle = static_cast<quintptr> ( window.winId () );

		if ( !vje::platform::apply_title_bar_colour ( handle, TEST_CAPTION ) )
		{
			QSKIP ( "this Windows does not let an application colour its title bar (Windows 10)" );
		}

		QTest::qWait ( 300 );

		const QRect frame  = window.frameGeometry ();
		const QRect client = window.geometry ();

		const QPoint captionMiddle ( frame.left () + ( frame.width () * 45 / 100 ), frame.top () + ( ( client.top () - frame.top () ) / 2 ) );

		QCOMPARE ( screen_pixel ( window, captionMiddle ), QColor ( TEST_CAPTION ) );

		// And handed back: no colour restores the platform's own.

		QVERIFY ( vje::platform::apply_title_bar_colour ( handle, std::nullopt ) );

		QTest::qWait ( 300 );

		QVERIFY ( screen_pixel ( window, captionMiddle ) != QColor ( TEST_CAPTION ) );
	}
};

QTEST_MAIN ( TestTitleBarWindows )

#include "tst_title_bar_windows.moc"
