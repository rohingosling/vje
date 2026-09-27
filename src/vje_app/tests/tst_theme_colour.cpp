//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Qt Test coverage for theme_colour (SET-14a): the lightness inversion against the requirement's own examples, hue and
//   saturation kept, the inversion exactly undoing itself over a grid of the colour cube, the colour for each theme,
//   and the CSS hexadecimal forms a user may type and the one form VJE writes. Headless.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/theme_colour.hpp"

#include <QtTest/QtTest>

#include <cstdlib>

using namespace vje;

namespace
{
	// HSL lightness in whole units of 1/510, from the channels -- stated here rather than read from QColor, so the case
	// does not trust the same library whose rounding the inversion was written to avoid.

	int lightness_units ( const QColor& colour )
	{
		return std::max ( { colour.red (), colour.green (), colour.blue () } ) + std::min ( { colour.red (), colour.green (), colour.blue () } );
	}
}

class TestThemeColour : public QObject
{
	Q_OBJECT

private slots:

	void the_requirement_s_examples_invert_as_stated ();
	void greys_black_white_and_a_pure_hue ();
	void hue_and_saturation_are_kept ();
	void the_inversion_undoes_itself_exactly ();
	void each_theme_gets_its_own_colour ();
	void the_hexadecimal_forms_a_user_may_type ();
	void anything_else_is_not_a_colour ();
	void the_written_form_is_upper_case_rrggbb ();
};

void TestThemeColour::the_requirement_s_examples_invert_as_stated ()
{
	// SET-14a: chosen in Dark at 60 %, drawn at 40 % in Light; chosen in Light at 10 %, drawn at 90 % in Dark. A blue of
	// each lightness, so the case is not a grey, which would pass on any channel-wise arithmetic.

	const QColor sixty = QColor::fromHslF ( 0.6f, 0.7f, 0.6f ).toRgb ();
	const QColor ten   = QColor::fromHslF ( 0.6f, 0.7f, 0.1f ).toRgb ();

	QVERIFY ( std::abs ( sixty.lightnessF () - 0.6f ) < 0.01f );
	QVERIFY ( std::abs ( ten.lightnessF ()   - 0.1f ) < 0.01f );

	QCOMPARE ( lightness_units ( inverted_lightness ( sixty ) ), 510 - lightness_units ( sixty ) );
	QCOMPARE ( lightness_units ( inverted_lightness ( ten ) ),   510 - lightness_units ( ten ) );

	QVERIFY ( std::abs ( inverted_lightness ( sixty ).lightnessF () - 0.4f ) < 0.01f );
	QVERIFY ( std::abs ( inverted_lightness ( ten ).lightnessF ()   - 0.9f ) < 0.01f );
}

void TestThemeColour::greys_black_white_and_a_pure_hue ()
{
	// The default: #808080 is a lightness of 128 / 255, a hair over half, so its inverse is #7F7F7F -- the one unit
	// that exactness costs, and a grey that reads on both backgrounds.

	QCOMPARE ( hex_colour ( inverted_lightness ( QColor ( 0x80, 0x80, 0x80 ) ) ), QStringLiteral ( "#7F7F7F" ) );

	QCOMPARE ( hex_colour ( inverted_lightness ( QColor ( 0, 0, 0 ) ) ),       QStringLiteral ( "#FFFFFF" ) );
	QCOMPARE ( hex_colour ( inverted_lightness ( QColor ( 255, 255, 255 ) ) ), QStringLiteral ( "#000000" ) );

	// A fully saturated primary sits at exactly half lightness, so it is its own inverse.

	QCOMPARE ( hex_colour ( inverted_lightness ( QColor ( 255, 0, 0 ) ) ), QStringLiteral ( "#FF0000" ) );
}

void TestThemeColour::hue_and_saturation_are_kept ()
{
	// Within QColor's own integer rounding of hue (degrees) and saturation (1 / 255): the inversion changes neither,
	// but the accessors round each colour separately.

	for ( int red = 5; red < 256; red += 50 )
	{
		for ( int green = 20; green < 256; green += 45 )
		{
			for ( int blue = 35; blue < 256; blue += 55 )
			{
				const QColor colour ( red, green, blue );
				const QColor inverse = inverted_lightness ( colour );

				if ( colour.hslSaturation () == 0 )
				{
					continue;                                  // A grey has no hue to keep.
				}

				const int hueDifference = std::abs ( colour.hslHue () - inverse.hslHue () );

				QVERIFY2 ( std::min ( hueDifference, 360 - hueDifference ) <= 1,               qPrintable ( hex_colour ( colour ) ) );
				QVERIFY2 ( std::abs ( colour.hslSaturation () - inverse.hslSaturation () ) <= 1, qPrintable ( hex_colour ( colour ) ) );
			}
		}
	}
}

void TestThemeColour::the_inversion_undoes_itself_exactly ()
{
	// Every 15th level of every channel, and both ends: switching the theme twice returns the chosen colour to the
	// unit, and the lightness is exactly the opposite each time. An HSL round trip fails this for some colours, which
	// is why the inversion is not one.

	for ( int red = 0; red < 256; red += 15 )
	{
		for ( int green = 0; green < 256; green += 15 )
		{
			for ( int blue = 0; blue < 256; blue += 15 )
			{
				const QColor colour ( red, green, blue );
				const QColor once  = inverted_lightness ( colour );

				QVERIFY2 ( inverted_lightness ( once ) == colour,                        qPrintable ( hex_colour ( colour ) ) );
				QVERIFY2 ( lightness_units ( once ) == 510 - lightness_units ( colour ), qPrintable ( hex_colour ( colour ) ) );
			}
		}
	}
}

void TestThemeColour::each_theme_gets_its_own_colour ()
{
	// Not at half lightness: such a colour is its own inverse, and would pass however the theme was treated (the first
	// version of this case used #3399CC, which is exactly half, and a neuter ignoring the theme passed it).

	const QColor dark ( 0x33, 0x66, 0x99 );

	QCOMPARE ( colour_for_theme ( dark, true ),  dark );
	QCOMPARE ( colour_for_theme ( dark, false ), QColor ( 0x66, 0x99, 0xCC ) );

	// And back: a colour chosen in Light, taken to the Dark value that is stored, shows in Light as chosen.

	const QColor chosenInLight ( 0x1A, 0x2B, 0x3C );
	const QColor stored        = colour_for_theme ( chosenInLight, false );

	QCOMPARE ( colour_for_theme ( stored, false ), chosenInLight );
}

void TestThemeColour::the_hexadecimal_forms_a_user_may_type ()
{
	QCOMPARE ( parse_hex_colour ( QStringLiteral ( "#808080" ) ),    std::optional<QColor> ( QColor ( 0x80, 0x80, 0x80 ) ) );
	QCOMPARE ( parse_hex_colour ( QStringLiteral ( "808080" ) ),     std::optional<QColor> ( QColor ( 0x80, 0x80, 0x80 ) ) );
	QCOMPARE ( parse_hex_colour ( QStringLiteral ( "#abc" ) ),       std::optional<QColor> ( QColor ( 0xAA, 0xBB, 0xCC ) ) );
	QCOMPARE ( parse_hex_colour ( QStringLiteral ( " #AbCdEf " ) ),  std::optional<QColor> ( QColor ( 0xAB, 0xCD, 0xEF ) ) );
}

void TestThemeColour::anything_else_is_not_a_colour ()
{
	const QStringList rejected =
	{
		QString (), QStringLiteral ( "#" ), QStringLiteral ( "#12" ), QStringLiteral ( "#1234" ), QStringLiteral ( "#12345" ),
		QStringLiteral ( "#1234567" ), QStringLiteral ( "#GGGGGG" ), QStringLiteral ( "red" ), QStringLiteral ( "rgb(1,2,3)" ),
		QStringLiteral ( "##123456" )
	};

	for ( const QString& text : rejected )
	{
		QVERIFY2 ( !parse_hex_colour ( text ).has_value (), qPrintable ( text ) );
	}
}

void TestThemeColour::the_written_form_is_upper_case_rrggbb ()
{
	QCOMPARE ( hex_colour ( QColor ( 0xab, 0xcd, 0xef ) ), QStringLiteral ( "#ABCDEF" ) );
	QCOMPARE ( hex_colour ( QColor ( 1, 2, 3 ) ),          QStringLiteral ( "#010203" ) );
}

QTEST_GUILESS_MAIN ( TestThemeColour )

#include "tst_theme_colour.moc"
