//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for tab_surface / band_surface and ShadedTabBar -- the editor pane's tab shading and the tree pane's
//   Explorer band (STYLE-11 / 13 / 14).
//
//   THE ORIGINAL DEFECT THIS SUITE EXISTS TO PIN. Fusion derives both tab fills from QPalette::Button and always draws
//   the UNSELECTED tab darker than the selected one. Against the dark theme's #252526 content that reads correctly;
//   against the light theme's #FFFFFF it is backwards -- the selected tab lands nearer white than its neighbours, so
//   the unselected tabs are the heavier ones and the active view is the hardest tab to pick out. It shipped that way
//   because nothing stated the relationship between the fills.
//
//   THE RULE, AS REVISED AT THE 2026-07-24 REVIEW. The unselected tabs are a STATIC FIELD -- one shade per theme,
//   unmoved by focus -- and only the SELECTED tab answers the keyboard: furthest from the content while its pane holds
//   it, a middle shade (still clear of the field) when it does not. The Explorer band keeps its own strong / receded
//   pair, decoupled from the selected tab, and the light strip's field matches the receded band exactly.
//
//   WHAT IS ASSERTED, DELIBERATELY: the RELATIONS, per theme, plus the per-theme direction (dark selects lighter,
//   light selects darker -- the originally reported symptom, which a relation-only test would miss). The absolute
//   distances are NOT pinned: they are the hand-tuning dials in AppConfig.hpp, and a suite that repeated their values
//   would turn every tuning pass into a test edit. The one equality that IS pinned -- light field == receded band --
//   is pinned because it is a stated design rule of the review, not a coincidence of today's numbers.
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

	static int band_lightness ( bool paneFocused )
	{
		return band_surface ( QApplication::palette (), paneFocused ).lightness ();
	}

	// How far a surface sits from the content, regardless of which side of it. Distance rather than raw lightness,
	// because "further from the content" means lighter on one theme and darker on the other -- comparing the numbers
	// directly would assert the opposite of the intent on one of them.

	static int distance_from_content ( bool selected, bool paneFocused )
	{
		return qAbs ( surface_lightness ( selected, paneFocused ) - content_lightness () );
	}

	static int band_distance_from_content ( bool paneFocused )
	{
		return qAbs ( band_lightness ( paneFocused ) - content_lightness () );
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
	// The rule, per theme.
	//=================================================================================================================

	void the_selected_tab_stands_clear_of_the_field_in_both_focus_states ()
	{
		// The 2026-07-24 requirement in one sentence: whatever the focus state, the active view must be identifiable.

		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			QVERIFY2 ( distance_from_content ( true, true ) > distance_from_content ( false, true ),
			           "a focused pane's selected tab must stand further from the content than the field" );

			QVERIFY2 ( distance_from_content ( true, false ) > distance_from_content ( false, false ),
			           "an unfocused pane's selected tab must STILL stand clear of the field" );
		}
	}

	void only_the_selected_tab_answers_focus ()
	{
		// The field is static (the same shade focused and unfocused); the selected tab recedes to its middle shade on
		// focus loss. This replaces the earlier whole-strip shift, which lifted the unselected tabs with the pane.

		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			QCOMPARE ( surface_lightness ( false, true ), surface_lightness ( false, false ) );

			QVERIFY2 ( distance_from_content ( true, true ) > distance_from_content ( true, false ),
			           "the selected tab must recede toward the content when the pane loses the keyboard (STYLE-14)" );
		}
	}

	void the_band_recedes_but_the_field_does_not ()
	{
		// The Explorer band keeps the strong / receded pair (STYLE-13 / 14); its strong shade coincides with the
		// focused selected tab's, so the two panes read as one register whichever holds the keyboard.

		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			QVERIFY2 ( band_distance_from_content ( true ) > band_distance_from_content ( false ),
			           "the band must recede when the tree loses the keyboard (STYLE-14)" );

			QCOMPARE ( band_lightness ( true ), surface_lightness ( true, true ) );
		}
	}

	void the_light_field_matches_the_receded_band ()
	{
		// The stated cross-pane rule of the 2026-07-24 review: with neither pane holding the keyboard, the light
		// theme's unselected tabs and the Explorer band wear the SAME shade -- and the unfocused active tab sits a
		// step darker than both, which is what forced the band's decoupling from the selected-tab surface.

		theme->set_theme ( Theme::Light );

		QCOMPARE ( tab_surface ( QApplication::palette (), false, false ),
		           band_surface ( QApplication::palette (), false ) );

		QVERIFY2 ( surface_lightness ( true, false ) < band_lightness ( false ),
		           "the light theme's unfocused active tab must sit darker than the receded band and the field" );
	}

	//=================================================================================================================
	// The direction, per theme -- the originally reported symptom.
	//=================================================================================================================

	void dark_selects_lighter_and_light_selects_darker ()
	{
		theme->set_theme ( Theme::Dark );

		QVERIFY2 ( surface_lightness ( true, true ) > surface_lightness ( false, true ),
		           "on the dark theme the selected tab must be the LIGHTER shade" );

		QVERIFY2 ( surface_lightness ( true, false ) > surface_lightness ( false, false ),
		           "and still the lighter one when the pane is unfocused" );

		theme->set_theme ( Theme::Light );

		// The original defect, stated as its opposite. Fusion drew the selected tab lighter here too, which left the
		// unselected tabs visually heavier than the active one against white content.

		QVERIFY2 ( surface_lightness ( true, true ) < surface_lightness ( false, true ),
		           "on the light theme the selected tab must be the DARKER shade" );

		QVERIFY2 ( surface_lightness ( true, false ) < surface_lightness ( false, false ),
		           "and still the darker one when the pane is unfocused" );
	}

	//=================================================================================================================
	// The dials drive the surfaces.
	//=================================================================================================================

	void the_distances_are_the_configured_ones ()
	{
		// Each theme reads its OWN dial set (they are tuned separately, AppConfig.hpp). Asserted against the
		// constants rather than as numbers, so hand-tuning the dials does not fight the suite.

		theme->set_theme ( Theme::Dark );

		QCOMPARE ( distance_from_content ( true,  true  ), config::editor::DARK_TAB_SELECTED_FOCUSED_CONTRAST );
		QCOMPARE ( distance_from_content ( true,  false ), config::editor::DARK_TAB_SELECTED_UNFOCUSED_CONTRAST );
		QCOMPARE ( distance_from_content ( false, true  ), config::editor::DARK_TAB_UNSELECTED_CONTRAST );
		QCOMPARE ( distance_from_content ( false, false ), config::editor::DARK_TAB_UNSELECTED_CONTRAST );

		theme->set_theme ( Theme::Light );

		QCOMPARE ( distance_from_content ( true,  true  ), config::editor::LIGHT_TAB_SELECTED_FOCUSED_CONTRAST );
		QCOMPARE ( distance_from_content ( true,  false ), config::editor::LIGHT_TAB_SELECTED_UNFOCUSED_CONTRAST );
		QCOMPARE ( distance_from_content ( false, true  ), config::editor::LIGHT_TAB_UNSELECTED_CONTRAST );
		QCOMPARE ( distance_from_content ( false, false ), config::editor::LIGHT_TAB_UNSELECTED_CONTRAST );

		// The band's pair is theme-independent.

		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			QCOMPARE ( band_distance_from_content ( true  ), config::editor::BAND_FOCUSED_CONTRAST );
			QCOMPARE ( band_distance_from_content ( false ), config::editor::BAND_UNFOCUSED_CONTRAST );
		}
	}

	//=================================================================================================================
	// Through the widget.
	//=================================================================================================================

	// WHAT THIS UNIQUELY PROVES is that the widget paints from tab_surface() at all -- the rule's own arithmetic is
	// already pinned by the pure functions above. So it asserts that both surfaces are PRESENT in the rendered strip,
	// not that they sit at particular coordinates.
	//
	// That distinction is not fastidiousness. It first sampled a fixed offset inside each tab (left + 6, top + 4) and
	// failed on Windows CI with a colour that was none of the surfaces -- because the runner has NO FONTS installed
	// ("QFontDatabase: Cannot find font directory ... Qt no longer ships fonts"), which makes glyph metrics degenerate,
	// which makes tab geometry undefined, which put the sample point somewhere unintended. Any assertion keyed to
	// laid-out geometry is unreliable in that environment; presence of the colour is not.

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

		// And the symptom the original rule exists to fix, on the light theme: the ACTIVE tab is the darker of the two.

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
