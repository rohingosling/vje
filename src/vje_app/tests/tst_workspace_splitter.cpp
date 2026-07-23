//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   WorkspaceSplitter unit tests (STYLE-04).
//
//   Both of this class's reasons to exist are claims about RENDERED PIXELS, so both are asserted as such. Fusion draws
//   the splitter grip with fixed translucent overlays rather than palette colours, which made it clearly visible on the
//   dark theme's background (+73 lightness) and invisible on the light theme's (+4); and it places the 2 px grip at
//   column offsets 4-5 of an 8 px handle, one pixel right of centre. Neither is the sort of thing a test of the code's
//   intentions would catch -- only reading the pixels back does.
//
//   Runs under the offscreen QPA platform (set by CTest), so it needs no display.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/WorkspaceSplitter.hpp"

#include "AppConfig.hpp"
#include "services/SettingsStore.hpp"
#include "services/ThemeService.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QList>
#include <QPalette>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

using namespace vje;

//*********************************************************************************************************************
// Class: TestWorkspaceSplitter
//*********************************************************************************************************************

class TestWorkspaceSplitter : public QObject
{
	Q_OBJECT

private:

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;

	// A rendered splitter, plus the handle's rect within the shot, so every test below reads real pixels rather than
	// the intentions of the paint code.

	struct Rendered
	{
		QImage shot;
		QRect  handle;
		QColor ground;
	};

	static Rendered render_splitter ( QWidget& host )
	{
		host.setAutoFillBackground ( true );
		host.resize ( 200, 120 );

		WorkspaceSplitter* const splitter = new WorkspaceSplitter ( Qt::Horizontal, &host );

		splitter->setHandleWidth ( config::workspace::SPLITTER_HANDLE_WIDTH );
		splitter->addWidget ( new QWidget ( splitter ) );
		splitter->addWidget ( new QWidget ( splitter ) );

		QVBoxLayout* const layout = new QVBoxLayout ( &host );

		layout->setContentsMargins ( 0, 0, 0, 0 );
		layout->addWidget ( splitter );

		host.show ();

		return Rendered
		{
			host.grab ().toImage (),
			splitter->handle ( 1 )->geometry (),
			QApplication::palette ().color ( QPalette::Window )
		};
	}

	// Which columns of the handle carry anything other than the background.

	static QList<int> grip_columns ( const Rendered& rendered )
	{
		QList<int> columns;

		for ( int x = rendered.handle.left (); x <= rendered.handle.right (); ++x )
		{
			for ( int y = rendered.handle.top (); y <= rendered.handle.bottom (); ++y )
			{
				if ( rendered.shot.pixelColor ( x, y ) != rendered.ground )
				{
					columns.append ( x - rendered.handle.left () );

					break;
				}
			}
		}

		return columns;
	}

	// The grip's dominant tone -- the one covering the most pixels, which is the main tone rather than the bevel.

	static QColor dominant_grip_tone ( const Rendered& rendered )
	{
		QMap<QRgb, int> tally;

		for ( int x = rendered.handle.left (); x <= rendered.handle.right (); ++x )
		{
			for ( int y = rendered.handle.top (); y <= rendered.handle.bottom (); ++y )
			{
				const QColor here = rendered.shot.pixelColor ( x, y );

				if ( here != rendered.ground )
				{
					tally [ here.rgb () ] += 1;
				}
			}
		}

		QRgb dominant = 0;
		int  best     = 0;

		for ( auto entry = tally.constBegin (); entry != tally.constEnd (); ++entry )
		{
			if ( entry.value () > best )
			{
				best     = entry.value ();
				dominant = entry.key ();
			}
		}

		return QColor ( dominant );
	}

private slots:

	void init ()
	{
		temporaryDirectory = std::make_unique<QTemporaryDir> ();

		QVERIFY ( temporaryDirectory->isValid () );

		settings = std::make_unique<SettingsStore> ( temporaryDirectory->filePath ( QStringLiteral ( "settings.json" ) ) );
		theme    = std::make_unique<ThemeService>  ( settings.get () );

		theme->apply ();
	}

	void cleanup ()
	{
		// Reverse dependency order -- ThemeService holds the SettingsStore.

		theme.reset ();
		settings.reset ();
		temporaryDirectory.reset ();
	}

	//=================================================================================================================
	// The tone rule.
	//=================================================================================================================

	// The whole principle in one assertion: the same number of steps, in whichever direction the ground allows. This is
	// what makes the light theme's grip the dark one's mirror rather than a second set of colours to keep in step.

	void a_tone_moves_away_from_whichever_end_the_ground_is_nearer ()
	{
		const QColor darkGround  ( 0x2D, 0x2D, 0x30 );
		const QColor lightGround ( 0xF3, 0xF3, 0xF3 );

		QCOMPARE ( contrasting_tone ( darkGround,  73 ).lightness (), darkGround.lightness  () + 73 );
		QCOMPARE ( contrasting_tone ( lightGround, 73 ).lightness (), lightGround.lightness () - 73 );
	}

	void a_tone_clamps_rather_than_wrapping_at_the_ends ()
	{
		// A ground close to either end must not overflow into the opposite one, which is what an unclamped add would
		// do -- and it would do it as a full inversion rather than a small error.

		QCOMPARE ( contrasting_tone ( QColor ( 0x02, 0x02, 0x02 ), 250 ).lightness (), 252 );
		QCOMPARE ( contrasting_tone ( QColor ( 0xFD, 0xFD, 0xFD ), 250 ).lightness (),   3 );
	}

	void a_tone_keeps_the_grounds_hue ()
	{
		// The dark theme's surfaces carry a faint blue. A grip mixed as plain grey would sit slightly apart from the
		// splitter it is drawn on, which is exactly the kind of difference nobody can name but everybody sees.

		const QColor tinted ( 0x2D, 0x2D, 0x30 );

		QCOMPARE ( contrasting_tone ( tinted, 40 ).hslHue (), tinted.hslHue () );
	}

	//=================================================================================================================
	// The rendered grip.
	//=================================================================================================================

	void the_grip_is_centred_in_the_handle ()
	{
		QWidget host;

		const Rendered rendered = render_splitter ( host );

		QCOMPARE ( rendered.handle.width (), config::workspace::SPLITTER_HANDLE_WIDTH );

		// An 8 px handle with a 2 px grip: offsets 3 and 4. The style's own sits at 4 and 5.

		const int expectedLeft = ( config::workspace::SPLITTER_HANDLE_WIDTH - config::workspace::GRIP_DOT_SIZE ) / 2;

		QList<int> expected;

		for ( int offset = 0; offset < config::workspace::GRIP_DOT_SIZE; ++offset )
		{
			expected.append ( expectedLeft + offset );
		}

		QCOMPARE ( grip_columns ( rendered ), expected );
	}

	void the_grip_contrasts_with_the_splitter_in_both_themes ()
	{
		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			QWidget host;

			const Rendered rendered = render_splitter ( host );

			const int delta = dominant_grip_tone ( rendered ).lightness () - rendered.ground.lightness ();

			QCOMPARE ( qAbs ( delta ), config::workspace::GRIP_MAIN_CONTRAST );
		}
	}

	// The direction, stated separately from the magnitude: on a dark splitter the grip is the lighter thing, and on a
	// light one it is the darker. Asserting only the magnitude would pass on a grip that vanished into the ground.

	void the_grip_lightens_on_dark_and_darkens_on_light ()
	{
		theme->set_theme ( Theme::Dark );

		QWidget darkHost;

		const Rendered onDark = render_splitter ( darkHost );

		QVERIFY ( dominant_grip_tone ( onDark ).lightness () > onDark.ground.lightness () );

		theme->set_theme ( Theme::Light );

		QWidget lightHost;

		const Rendered onLight = render_splitter ( lightHost );

		QVERIFY ( dominant_grip_tone ( onLight ).lightness () < onLight.ground.lightness () );
	}
};

QTEST_MAIN ( TestWorkspaceSplitter )

#include "tst_workspace_splitter.moc"
