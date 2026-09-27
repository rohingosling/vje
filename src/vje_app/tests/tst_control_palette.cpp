//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tst_control_palette -- STYLE-16: a disabled control shades its FILL, not just its text.
//
//   THE TWO HALVES FAIL INDEPENDENTLY, which is why this is a suite rather than three cases bolted onto an existing
//   one. The first half is a fact about ThemeService's palettes -- that the Disabled group carries a shaded edit-area
//   and button fill, in the right direction for each theme. The second is a fact about Qt: that Fusion actually
//   HONOURS those roles when it draws a disabled control. A palette can be perfectly correct while nothing on screen
//   changes, and a test that only reads the palette back would report success either way.
//
//   So the second half reads RENDERED PIXELS (lesson Q12, and views/Card's precedent), and it reads them as a
//   DIRECTION rather than as an exact colour: Fusion draws a button face with a gradient, so an exact match holds for
//   the flat edit area and would be wrong for the drop-down. What both must satisfy is the requirement's own wording
//   -- lighter under the dark theme, darker under the light one.
//
//   READ-ONLY IS NOT DISABLED, and that case is here because it is a decision rather than an accident: a read-only
//   field's text is still selectable and copyable, which is why the XML import dialog's file name is read-only in the
//   first place.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "services/ThemeService.hpp"
#include "style/tone.hpp"

#include <vje_settings/SettingsStore.hpp>

#include <QtTest/QtTest>

#include <QComboBox>
#include <QImage>
#include <QLineEdit>
#include <QPalette>
#include <QTemporaryDir>
#include <QWidget>

#include <memory>

using namespace vje;

//*********************************************************************************************************************
// Class: TestControlPalette
//*********************************************************************************************************************

class TestControlPalette : public QObject
{
	Q_OBJECT

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

private:

	// The colour a control actually paints its face in, sampled from a rendered image rather than asked of the
	// palette. Sampled at a point inside the frame and away from any drop-down arrow, on a control with no text.

	static QColor rendered_fill ( QWidget& control )
	{
		control.resize ( 160, 28 );
		control.show ();

		QImage image ( control.size (), QImage::Format_RGB32 );

		image.fill ( Qt::magenta );   // Not a colour either theme uses, so an unpainted sample is obvious.

		control.render ( &image );

		return QColor ( image.pixel ( control.width () / 4, control.height () / 2 ) );
	}

	// A theme's own direction, stated once. The requirement is "lighter on dark, darker on light", which is the same
	// rule style/tone applies: away from whichever end of the lightness scale the surface is nearer.

	static bool moved_the_right_way ( const QColor& enabled, const QColor& disabled, bool darkTheme )
	{
		return darkTheme ? ( disabled.lightness () > enabled.lightness () )
		                 : ( disabled.lightness () < enabled.lightness () );
	}

	//=================================================================================================================
	// Test Cases
	//=================================================================================================================

private slots:

	void init ()
	{
		temporaryDirectory = std::make_unique<QTemporaryDir> ();

		settings = std::make_unique<SettingsStore> ( temporaryDirectory->filePath ( QStringLiteral ( "settings.json" ) ) );
		theme    = std::make_unique<ThemeService>  ( settings.get () );

		theme->apply ();
	}

	void cleanup ()
	{
		// Reverse construction order (lesson Q1).

		theme.reset    ();
		settings.reset ();

		temporaryDirectory.reset ();
	}

	// The palette half. Both fill roles, both themes, and the DISTANCE as well as the direction -- a rule stated as
	// "some shade darker" is satisfied by black.

	void both_themes_shade_the_disabled_fills_away_from_their_enabled_ones ()
	{
		const Theme themes [ 2 ] = { Theme::Light, Theme::Dark };

		for ( const Theme scheme : themes )
		{
			theme->set_theme ( scheme );

			// Constructed after the theme change: QApplication::setPalette reaches no already-built widget on Qt 6.10
			// (lesson Q9), so a host made once and read twice reports the palette it was born under.

			const QWidget host;

			const QPalette palette = host.palette ();

			const bool darkTheme = ( scheme == Theme::Dark );

			const QPalette::ColorRole roles [ 2 ] = { QPalette::Base, QPalette::Button };

			for ( const QPalette::ColorRole role : roles )
			{
				const QColor enabled  = palette.color ( QPalette::Active,   role );
				const QColor disabled = palette.color ( QPalette::Disabled, role );

				QVERIFY2
				(
					moved_the_right_way ( enabled, disabled, darkTheme ),
					qPrintable ( QStringLiteral ( "%1 -> %2 is the wrong direction for this theme" )
					             .arg ( enabled.name (), disabled.name () ) )
				);

				// The stated distance, so the dial is what decides it rather than whatever looked right once.

				QCOMPARE ( qAbs ( disabled.lightness () - enabled.lightness () ),
				           config::appearance::DISABLED_FILL_CONTRAST );

				QCOMPARE ( disabled.rgb (),
				           contrasting_tone ( enabled, config::appearance::DISABLED_FILL_CONTRAST ).rgb () );
			}
		}
	}

	// The rendering half, for a text box. Fusion fills an edit area flat, so this one can be exact -- and being exact
	// is what proves the palette reached the paint rather than merely existing beside it.

	void a_disabled_text_box_renders_the_shaded_edit_area ()
	{
		const Theme themes [ 2 ] = { Theme::Light, Theme::Dark };

		for ( const Theme scheme : themes )
		{
			theme->set_theme ( scheme );

			QLineEdit enabled;
			QLineEdit disabled;

			disabled.setEnabled ( false );

			const QColor enabledFill  = rendered_fill ( enabled );
			const QColor disabledFill = rendered_fill ( disabled );

			QVERIFY2
			(
				enabledFill != QColor ( Qt::magenta ),
				"the sample point was never painted, so the case is measuring the fill colour"
			);

			QVERIFY2
			(
				moved_the_right_way ( enabledFill, disabledFill, scheme == Theme::Dark ),
				qPrintable ( QStringLiteral ( "rendered %1 -> %2" )
				             .arg ( enabledFill.name (), disabledFill.name () ) )
			);

			QCOMPARE ( disabledFill.rgb (), disabled.palette ().color ( QPalette::Disabled, QPalette::Base ).rgb () );
		}
	}

	// The rendering half, for a drop-down list -- and it asks a DIFFERENT question from the text box's, because Fusion
	// draws a button face through a gradient AND dims a disabled one on its own account. Measured: the enabled face
	// samples #F0F0F0 and the disabled one #CDCDCD, a shift of 35 where the palette distance is 12. So neither an
	// exact match nor a comparison against the toned enabled sample is available here, and both were tried.
	//
	// Two earlier versions were wrong in opposite directions, and the neutered runs found both (D10). Direction plus
	// half the dial PASSED against a build with the Button shading removed -- Fusion's own dimming satisfies it
	// unaided. Comparing against the toned enabled sample FAILED against the correct build, for the same reason.
	//
	// What is actually at risk is whether Fusion reads QPalette::Disabled's button role at all; if it does not, our
	// shading is invisible however correct the palette is. That is answerable directly: give one disabled combo box a
	// near-white disabled face and another a near-black one, and see whether the rendering follows. It is not a
	// statement about Fusion's gradient, which is Qt's business and not ours.

	void a_disabled_drop_down_takes_its_face_from_the_disabled_palette ()
	{
		theme->set_theme ( Theme::Light );

		const QColor faces [ 2 ] = { QColor ( 0xF8, 0xF8, 0xF8 ), QColor ( 0x30, 0x30, 0x30 ) };

		QColor rendered [ 2 ];

		for ( int index = 0; index < 2; ++index )
		{
			QComboBox box;

			box.addItem ( QString () );
			box.setEnabled ( false );

			QPalette palette = box.palette ();

			palette.setColor ( QPalette::Disabled, QPalette::Button, faces [ index ] );

			box.setPalette ( palette );

			rendered [ index ] = rendered_fill ( box );
		}

		QVERIFY2
		(
			rendered [ 0 ].lightness () > rendered [ 1 ].lightness (),
			qPrintable ( QStringLiteral ( "a near-white disabled face rendered %1 and a near-black one %2" )
			             .arg ( rendered [ 0 ].name (), rendered [ 1 ].name () ) )
		);

		// A real difference rather than a rounding one, so a style ignoring the role cannot pass on noise.

		QVERIFY2
		(
			( rendered [ 0 ].lightness () - rendered [ 1 ].lightness () ) > MINIMUM_ROLE_RESPONSE,
			qPrintable ( QStringLiteral ( "the two faces rendered only %1 apart" )
			             .arg ( rendered [ 0 ].lightness () - rendered [ 1 ].lightness () ) )
		);
	}

	// STYLE-16's decision, pinned. Read-only is not unavailable: the text is still selectable and copyable, which is
	// the entire reason the XML import dialog's file-name field is read-only rather than disabled.
	//
	// THIS CASE HAS NO NEUTER, and that is stated rather than glossed. It holds by construction -- Qt reads the
	// Disabled colour group from the widget's ENABLED state, and a read-only widget is enabled -- so there is no line
	// in the implementation to delete that would make it fail today. Shading the ACTIVE fill instead does not do it
	// either: both samples move together and stay equal. What it is here for is the future change that reaches for
	// isReadOnly() somewhere and quietly widens the rule, and its non-vacuity rests on the case above, which proves
	// the same helper does detect a disabled/enabled difference.

	void a_read_only_text_box_is_not_shaded ()
	{
		theme->set_theme ( Theme::Light );

		QLineEdit enabled;
		QLineEdit readOnly;

		readOnly.setReadOnly ( true );

		QCOMPARE ( rendered_fill ( readOnly ).rgb (), rendered_fill ( enabled ).rgb () );
	}

	//=================================================================================================================
	// Data Members
	//=================================================================================================================

private:

	// How far apart a near-white and a near-black disabled button face must render before the drop-down case will
	// believe the style consulted the role at all. Far below the ~200 the two test colours differ by, and far above
	// anything a gradient or a rounding could produce on its own.

	static constexpr int MINIMUM_ROLE_RESPONSE = 60;

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;
};

QTEST_MAIN ( TestControlPalette )

#include "tst_control_palette.moc"
