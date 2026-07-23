//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for tab_surface and ShadedTabBar -- the editor pane's tab shading and the tree pane's Explorer band
//   (STYLE-13 / STYLE-14).
//
//   THE DEFECT THIS SUITE EXISTS TO PIN. Fusion derives both tab fills from QPalette::Button and always draws the
//   UNSELECTED tab darker than the selected one. Against the dark theme's #252526 content that reads correctly -- the
//   selected tab is the lightest thing on the strip. Against the light theme's #FFFFFF it is backwards: the selected
//   tab ends up NEARER white than its neighbours, so the unselected tabs are the heavier ones and the active view is
//   the hardest tab to pick out. It shipped that way because nothing stated what the relationship between the eight
//   fills was supposed to be, so the light theme could drift out of step with the dark one without contradicting
//   anything.
//
//   The rule is therefore asserted two ways, deliberately:
//
//     - AS A RULE, theme-independently: the selected tab stands furthest from the content, and the whole strip recedes
//       toward the content when the pane loses the keyboard. This holds for any theme that follows it and does not have
//       to be rewritten when a content colour moves.
//     - AS A DIRECTION, per theme: dark selects LIGHTER, light selects DARKER. This is the symptom that was actually
//       reported, and a rule-only test would have passed against the broken build -- Fusion's shading satisfies "the
//       selected tab differs from the unselected ones" perfectly well.
//
//   The dark theme's four values are also pinned against what it rendered BEFORE the change, since the whole design
//   claim is that dark is unchanged and light becomes its mirror.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/tab_surface.hpp"

#include "AppConfig.hpp"
#include "services/SettingsStore.hpp"
#include "services/ThemeService.hpp"
#include "views/ShadedTabBar.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPalette>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace vje;

class TestTabSurface : public QObject
{
	Q_OBJECT

private:

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;

	// Is this exact colour painted anywhere in the image? Geometry-independent by design -- see the note on
	// the_bar_paints_the_surfaces_the_rule_names.

	static bool image_contains ( const QImage& image, const QColor& colour )
	{
		const QRgb wanted = colour.rgb () | 0xFF000000u;

		for ( int y = 0; y < image.height (); ++y )
		{
			for ( int x = 0; x < image.width (); ++x )
			{
				if ( ( image.pixel ( x, y ) | 0xFF000000u ) == wanted )
				{
					return true;
				}
			}
		}

		return false;
	}

	static int content_lightness ()
	{
		return QApplication::palette ().color ( QPalette::Active, QPalette::Base ).lightness ();
	}

	static int surface_lightness ( bool selected, bool paneFocused )
	{
		return tab_surface ( QApplication::palette (), selected, paneFocused ).lightness ();
	}

	// How far a surface sits from the content, regardless of which side of it. Distance rather than raw lightness,
	// because "further from the content" means lighter on one theme and darker on the other -- comparing the numbers
	// directly would assert the opposite of the intent on one of them.

	static int distance_from_content ( bool selected, bool paneFocused )
	{
		return qAbs ( surface_lightness ( selected, paneFocused ) - content_lightness () );
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
	// The rule, theme-independently.
	//=================================================================================================================

	void the_selected_tab_stands_furthest_from_the_content ()
	{
		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			QVERIFY2 ( distance_from_content ( true, true ) > distance_from_content ( false, true ),
			           "a focused pane's selected tab must stand further from the content than its unselected tabs" );

			QVERIFY2 ( distance_from_content ( true, false ) > distance_from_content ( false, false ),
			           "an unfocused pane's selected tab must still stand further out than its unselected tabs" );
		}
	}

	void the_strip_recedes_when_the_pane_loses_the_keyboard ()
	{
		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			QVERIFY2 ( distance_from_content ( true, true ) > distance_from_content ( true, false ),
			           "the selected tab must sink toward the content when the pane loses focus (STYLE-14)" );

			QVERIFY2 ( distance_from_content ( false, true ) > distance_from_content ( false, false ),
			           "the unselected tabs must sink with it, so the strip moves as one" );
		}
	}

	//=================================================================================================================
	// The direction, per theme -- the reported symptom.
	//=================================================================================================================

	void dark_selects_lighter_and_light_selects_darker ()
	{
		theme->set_theme ( Theme::Dark );

		QVERIFY2 ( surface_lightness ( true, true ) > surface_lightness ( false, true ),
		           "on the dark theme the selected tab must be the LIGHTER shade" );

		theme->set_theme ( Theme::Light );

		// The defect, stated as its opposite. Fusion drew the selected tab lighter here too, which left the unselected
		// tabs visually heavier than the active one against white content.

		QVERIFY2 ( surface_lightness ( true, true ) < surface_lightness ( false, true ),
		           "on the light theme the selected tab must be the DARKER shade" );

		QVERIFY2 ( surface_lightness ( true, false ) < surface_lightness ( false, false ),
		           "and the same when the pane is unfocused" );
	}

	//=================================================================================================================
	// The two themes are one rule.
	//=================================================================================================================

	void every_surface_sits_the_same_distance_from_the_content_in_both_themes ()
	{
		struct Reading
		{
			int selectedFocused;
			int unselectedFocused;
			int selectedUnfocused;
			int unselectedUnfocused;
		};

		Reading readings [ 2 ];

		const Theme candidates [] = { Theme::Light, Theme::Dark };

		for ( int index = 0; index < 2; ++index )
		{
			theme->set_theme ( candidates [ index ] );

			readings [ index ] =
			{
				distance_from_content ( true,  true  ),
				distance_from_content ( false, true  ),
				distance_from_content ( true,  false ),
				distance_from_content ( false, false )
			};
		}

		// Mirror images: each light surface sits the same distance from white that its dark counterpart sits from
		// #252526. This is the property that makes one set of four constants generate all eight fills.

		QCOMPARE ( readings [ 0 ].selectedFocused,     readings [ 1 ].selectedFocused );
		QCOMPARE ( readings [ 0 ].unselectedFocused,   readings [ 1 ].unselectedFocused );
		QCOMPARE ( readings [ 0 ].selectedUnfocused,   readings [ 1 ].selectedUnfocused );
		QCOMPARE ( readings [ 0 ].unselectedUnfocused, readings [ 1 ].unselectedUnfocused );
	}

	void the_distances_are_the_configured_ones ()
	{
		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			QCOMPARE ( distance_from_content ( true,  true  ), config::editor::TAB_SELECTED_FOCUSED_CONTRAST );
			QCOMPARE ( distance_from_content ( false, true  ), config::editor::TAB_UNSELECTED_FOCUSED_CONTRAST );
			QCOMPARE ( distance_from_content ( true,  false ), config::editor::TAB_SELECTED_UNFOCUSED_CONTRAST );
			QCOMPARE ( distance_from_content ( false, false ), config::editor::TAB_UNSELECTED_UNFOCUSED_CONTRAST );
		}
	}

	// The design claim is that DARK is unchanged and only light is corrected. These are the fills the dark theme
	// rendered through Fusion before the change, measured back out of it -- so a future tweak to the distances cannot
	// quietly restyle the theme that was already right.

	void the_dark_theme_still_renders_its_previous_fills ()
	{
		theme->set_theme ( Theme::Dark );

		QCOMPARE ( surface_lightness ( true,  true  ), 71 );   // was #474747
		QCOMPARE ( surface_lightness ( false, true  ), 64 );   // was #404040
		QCOMPARE ( surface_lightness ( true,  false ), 54 );   // was #363636
		QCOMPARE ( surface_lightness ( false, false ), 48 );   // was #303030
	}

	//=================================================================================================================
	// Through the widget.
	//=================================================================================================================

	// WHAT THIS UNIQUELY PROVES is that the widget paints from tab_surface() at all -- the rule's own arithmetic is
	// already pinned by the six functions above. So it asserts that both surfaces are PRESENT in the rendered strip,
	// not that they sit at particular coordinates.
	//
	// That distinction is not fastidiousness. It first sampled a fixed offset inside each tab (left + 6, top + 4) and
	// failed on Windows CI with a colour that was none of the four surfaces -- because the runner has NO FONTS
	// installed ("QFontDatabase: Cannot find font directory ... Qt no longer ships fonts"), which makes glyph metrics
	// degenerate, which makes tab geometry undefined, which put the sample point somewhere unintended. Any assertion
	// keyed to laid-out geometry is unreliable in that environment; presence of the colour is not.

	void the_bar_paints_the_surfaces_the_rule_names ()
	{
		theme->set_theme ( Theme::Light );

		ShadedTabWidget tabs;

		tabs.setDocumentMode ( true );
		tabs.addTab ( new QWidget, QStringLiteral ( "Form" ) );
		tabs.addTab ( new QWidget, QStringLiteral ( "Text" ) );
		tabs.resize ( 260, 120 );
		tabs.shaded_tab_bar ()->set_pane_focused ( true );

		QTabBar* const bar = tabs.tabBar ();

		const QImage rendered = bar->grab ().toImage ();

		// Read against the palette the PAINTER uses -- the bar's own -- rather than the tab widget's, so the comparison
		// cannot pass or fail on palette propagation rather than on the shading.

		const QColor selectedSurface   = tab_surface ( bar->palette (), true,  true );
		const QColor unselectedSurface = tab_surface ( bar->palette (), false, true );

		QVERIFY2 ( image_contains ( rendered, selectedSurface ),
		           qPrintable ( QStringLiteral ( "selected surface %1 was not painted" ).arg ( selectedSurface.name () ) ) );

		QVERIFY2 ( image_contains ( rendered, unselectedSurface ),
		           qPrintable ( QStringLiteral ( "unselected surface %1 was not painted" ).arg ( unselectedSurface.name () ) ) );

		// And the symptom this whole rule exists to fix, on the light theme: the ACTIVE tab is the darker of the two.

		QVERIFY2 ( selectedSurface.lightness () < unselectedSurface.lightness (),
		           "the light theme's selected tab must be darker than its unselected tabs" );
	}

	// Clicking a tab hands the STRIP the keyboard, so the arrow keys move between tabs (NAV-04). The offscreen platform
	// grants focus to nothing, so what is asserted here is the POLICY that makes a click a focus
	// move; that focus actually lands there is a manual smoke item.

	void a_tab_click_is_a_focus_move ()
	{
		ShadedTabWidget tabs;

		tabs.addTab ( new QWidget, QStringLiteral ( "Form" ) );

		QVERIFY2 ( tabs.tabBar ()->focusPolicy () & Qt::ClickFocus,
		           "a click on a tab must be able to take the keyboard" );
	}
};

QTEST_MAIN ( TestTabSurface )

#include "tst_tab_surface.moc"
