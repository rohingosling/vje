//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   FocusHighlight unit tests -- the selection bar (STYLE-12) and the pane chrome (STYLE-14) -- plus PaneHeader's
//   sizing and its claim to wear the same surface as the editor pane's tab strip (STYLE-13).
//
//   WHY THE RULE IS TESTED AS A FUNCTION AND NOT AS A FOCUS MOVE. Real keyboard focus cannot be driven under the
//   offscreen platform these suites run on: a window never becomes active, so hasFocus() answers false no matter how
//   the test asks, and every assertion downstream of it would be measuring the platform rather than the code. That is
//   the same trap in a second costume, so the decision lives in focus_adjusted_palette() and is asserted directly.
//   Whether real focus reaches it is a manual smoke item.
//
//   WHAT IS ASSERTED THROUGH THE WIDGET is only what a headless run can honestly answer: that an unfocused view wears
//   the muted bar, and that a THEME SWITCH rebuilds it. The second is the case that would otherwise rot silently --
//   writing a role onto a widget makes Qt stop propagating that role to it, so a class that writes the highlight once
//   keeps the old theme's grey forever and nothing but a repaint would ever say so.
//
//   MEASURED, NOT ASSUMED: on Qt 6.10 a bare QApplication::setPalette() delivers NO event to already-constructed
//   widgets and does not reach their resolved palettes at all. What does arrive is QEvent::StyleChange, from the
//   QApplication::setStyle() that ThemeService::apply() performs first. That ordering -- news of the new theme
//   arriving before the new palette does -- is why the refresh is deferred rather than immediate.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/FocusHighlight.hpp"

#include "AppConfig.hpp"
#include "services/SettingsStore.hpp"
#include "services/ThemeService.hpp"
#include "views/PaneHeader.hpp"
#include "views/ShadedTabBar.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QTreeView>

#include <memory>

using namespace vje;

//*********************************************************************************************************************
// Class: TestFocusHighlight
//*********************************************************************************************************************

class TestFocusHighlight : public QObject
{
	Q_OBJECT

private:

	std::unique_ptr<QTemporaryDir> temporaryDirectory;
	std::unique_ptr<SettingsStore> settings;
	std::unique_ptr<ThemeService>  theme;

	// The application palette's two selection colours -- the source FocusHighlight reads from. Assertions compare
	// against these rather than restating hex values the themes are free to change.

	static QColor accent_highlight ()
	{
		return QApplication::palette ().color ( QPalette::Active, QPalette::Highlight );
	}

	static QColor muted_highlight ()
	{
		return QApplication::palette ().color ( QPalette::Inactive, QPalette::Highlight );
	}

	// What the widget will actually paint its selection with: item views draw from the Active group while their window
	// is active, so that is the slot FocusHighlight writes and the one worth reading back.

	static QColor painted_highlight ( const QWidget* widget )
	{
		return widget->palette ().color ( QPalette::Active, QPalette::Highlight );
	}

	// Two obviously distinguishable palettes for the pure-rule tests, so a wrong group or a wrong role is a visible
	// mismatch rather than a coincidence of two similar greys.

	static QPalette theme_fixture ()
	{
		QPalette palette;

		palette.setColor ( QPalette::Active,   QPalette::Highlight,       QColor ( 0x00, 0x67, 0xC0 ) );
		palette.setColor ( QPalette::Active,   QPalette::HighlightedText, QColor ( 0xFF, 0xFF, 0xFF ) );
		palette.setColor ( QPalette::Inactive, QPalette::Highlight,       QColor ( 0xE1, 0xE1, 0xE1 ) );
		palette.setColor ( QPalette::Inactive, QPalette::HighlightedText, QColor ( 0x1A, 0x1A, 0x1A ) );

		return palette;
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
	// The theme supplies both colours.
	//=================================================================================================================

	// FocusHighlight is only ever a chooser. If the two colours are not actually different in the palette it chooses
	// from, every other test here would pass while the feature did nothing at all.

	void each_theme_defines_a_distinct_muted_selection_colour ()
	{
		theme->set_theme ( Theme::Light );

		QVERIFY2 ( accent_highlight () != muted_highlight (), "the light theme must distinguish the two bars" );

		const QColor lightMuted = muted_highlight ();

		theme->set_theme ( Theme::Dark );

		QVERIFY2 ( accent_highlight () != muted_highlight (), "the dark theme must distinguish the two bars" );

		QVERIFY2 ( muted_highlight () != lightMuted, "each theme must bring its own muted bar, not share one" );
	}

	// White on a light grey bar is the one pairing this must never produce, so the muted text colour is pinned to the
	// ordinary text colour rather than left as whatever HighlightedText happened to be.

	void the_muted_bar_carries_readable_text ()
	{
		for ( const Theme candidate : { Theme::Light, Theme::Dark } )
		{
			theme->set_theme ( candidate );

			const QPalette palette = QApplication::palette ();

			QCOMPARE
			(
				palette.color ( QPalette::Inactive, QPalette::HighlightedText ),
				palette.color ( QPalette::Active,   QPalette::Text )
			);
		}
	}

	//=================================================================================================================
	// The rule.
	//=================================================================================================================

	void holding_focus_takes_the_accent_colours ()
	{
		const QPalette source   = theme_fixture ();
		const QPalette adjusted = focus_adjusted_palette ( QPalette (), source, true );

		QCOMPARE ( adjusted.color ( QPalette::Active, QPalette::Highlight ),
		           source.color   ( QPalette::Active, QPalette::Highlight ) );

		QCOMPARE ( adjusted.color ( QPalette::Active, QPalette::HighlightedText ),
		           source.color   ( QPalette::Active, QPalette::HighlightedText ) );
	}

	// The Inactive colours are written into the ACTIVE slot, which is the whole trick: an item view paints from Active
	// while its window is active, so that is the only slot capable of muting a view the user has simply left.

	void losing_focus_takes_the_muted_colours ()
	{
		const QPalette source   = theme_fixture ();
		const QPalette adjusted = focus_adjusted_palette ( QPalette (), source, false );

		QCOMPARE ( adjusted.color ( QPalette::Active,   QPalette::Highlight ),
		           source.color   ( QPalette::Inactive, QPalette::Highlight ) );

		QCOMPARE ( adjusted.color ( QPalette::Active,   QPalette::HighlightedText ),
		           source.color   ( QPalette::Inactive, QPalette::HighlightedText ) );
	}

	// The trap that would make the accent a one-way door: feeding an already-muted palette back in as the SOURCE would
	// leave nothing to restore from. The rule reads the theme, never its own previous output.

	void the_rule_recovers_the_accent_from_its_own_muted_output ()
	{
		const QPalette source = theme_fixture ();

		QPalette carried = focus_adjusted_palette ( QPalette (), source, false );

		carried = focus_adjusted_palette ( carried, source, false );
		carried = focus_adjusted_palette ( carried, source, true );

		QCOMPARE ( carried.color ( QPalette::Active, QPalette::Highlight ),
		           source.color  ( QPalette::Active, QPalette::Highlight ) );
	}

	// Assigning the result to a widget marks every role it names as the widget's own, which stops Qt propagating that
	// role on a theme change. Writing two roles instead of a whole palette is what keeps that cost to two.

	void the_rule_leaves_every_other_role_alone ()
	{
		QPalette current;

		current.setColor ( QPalette::Active, QPalette::Window, QColor ( 0x11, 0x22, 0x33 ) );
		current.setColor ( QPalette::Active, QPalette::Base,   QColor ( 0x44, 0x55, 0x66 ) );
		current.setColor ( QPalette::Active, QPalette::Text,   QColor ( 0x77, 0x88, 0x99 ) );

		const QPalette adjusted = focus_adjusted_palette ( current, theme_fixture (), false );

		QCOMPARE ( adjusted.color ( QPalette::Active, QPalette::Window ), current.color ( QPalette::Active, QPalette::Window ) );
		QCOMPARE ( adjusted.color ( QPalette::Active, QPalette::Base   ), current.color ( QPalette::Active, QPalette::Base   ) );
		QCOMPARE ( adjusted.color ( QPalette::Active, QPalette::Text   ), current.color ( QPalette::Active, QPalette::Text   ) );
	}

	//=================================================================================================================
	// The pane surface (STYLE-14).
	//=================================================================================================================

	// Measured, not assumed: Fusion derives a tab's fill from Button and from nothing else — perturbing Window, Base,
	// Light, Midlight, Mid or Dark leaves it untouched. That single-role finding is the whole basis for the Surface
	// role set, so it is worth stating where a future Qt that widened it would be caught.

	void the_surface_role_set_is_the_button_alone ()
	{
		QCOMPARE ( palette_roles_for ( FocusRoles::Surface ), QList<QPalette::ColorRole> { QPalette::Button } );
	}

	void the_selection_and_surface_role_sets_do_not_overlap ()
	{
		// They are installed on different widgets and must be able to coexist on nested ones: a shared role would mean
		// the last watcher to run silently won.

		for ( const QPalette::ColorRole role : palette_roles_for ( FocusRoles::Surface ) )
		{
			QVERIFY ( !palette_roles_for ( FocusRoles::Selection ).contains ( role ) );
		}
	}

	// WHERE THE INTENT LIVES. Fusion turns Button into a tab surface through a private gradient, so the two Button
	// values in ThemeService cannot be read as a statement of what the user sees -- they were solved backwards from
	// these fills by rendering a tab at each candidate level. The rendered result is therefore the only honest place to
	// state the design, and this is it; ThemeService carries the solution to it.

	void the_surface_recedes_when_its_pane_loses_focus ()
	{
		const QPalette source = QApplication::palette ();

		const QPalette focused   = focus_adjusted_palette ( QPalette (), source, true,  FocusRoles::Surface );
		const QPalette unfocused = focus_adjusted_palette ( QPalette (), source, false, FocusRoles::Surface );

		QCOMPARE ( focused.color   ( QPalette::Active, QPalette::Button ), source.color ( QPalette::Active,   QPalette::Button ) );
		QCOMPARE ( unfocused.color ( QPalette::Active, QPalette::Button ), source.color ( QPalette::Inactive, QPalette::Button ) );
	}

	// NOTE ON WHAT LEFT THIS SUITE. Two tests here used to render a Fusion QTabWidget and assert its fills for the
	// Active and Inactive Button values. They were removed rather than repaired: the editor pane no longer takes its
	// tab shading from Fusion at all (Fusion always draws the unselected tab darker than the selected one, which is
	// backwards against white content), so those tests asserted the behaviour of a widget the product does not put on
	// screen. The rule that replaced them lives in style/tab_surface.hpp and is pinned by tst_tab_surface.
	//
	// FocusRoles::Surface still writes QPalette::Button and that is still correct -- it is simply no longer what the
	// tab strip and the Explorer band derive their fills from.

	//=================================================================================================================
	// Through the widget.
	//=================================================================================================================

	// The offscreen platform grants focus to nothing, so this is also the state every view in the suite is in -- which
	// makes it the honest assertion to write here, rather than a focus move the platform cannot perform.

	void an_unfocused_view_wears_the_muted_bar ()
	{
		QTreeView view;

		FocusHighlight::install ( &view );

		QCOMPARE ( painted_highlight ( &view ), muted_highlight () );
	}

	void switching_theme_rebuilds_the_bar ()
	{
		QTreeView view;

		FocusHighlight::install ( &view );

		theme->set_theme ( Theme::Light );

		QCoreApplication::processEvents ();

		const QColor lightMuted = muted_highlight ();

		QCOMPARE ( painted_highlight ( &view ), lightMuted );

		theme->set_theme ( Theme::Dark );

		// The refresh is deferred, because the news of a theme change arrives before the palette carrying it does.

		QCoreApplication::processEvents ();

		QVERIFY2 ( muted_highlight () != lightMuted, "the fixture must actually change colour for this to prove anything" );

		QCOMPARE ( painted_highlight ( &view ), muted_highlight () );
	}

	//=================================================================================================================
	// PaneHeader.
	//=================================================================================================================

	// STYLE-13's actual claim, asserted against a REAL QTabBar rather than against the formula PaneHeader uses. A test
	// that restated the formula would agree with the code by construction and would keep agreeing with it after the
	// header drifted away from the tab strip it is supposed to match.

	void the_pane_header_is_as_tall_as_a_real_tab ()
	{
		PaneHeader header ( QStringLiteral ( "Explorer" ) );

		QTabBar reference;

		reference.setDocumentMode ( true );
		reference.addTab ( QStringLiteral ( "Explorer" ) );

		reference.resize ( reference.sizeHint () );

		const int bandHeight = header.sizeHint ().height () - config::pane::HEADER_RULE_HEIGHT;

		QVERIFY2 ( reference.tabRect ( 0 ).height () > 0, "the reference bar must have laid its tab out" );

		QCOMPARE ( bandHeight, reference.tabRect ( 0 ).height () );
	}

	// STYLE-13's actual claim, checked as a claim: the band and the editor pane's SELECTED tab are the same colour.
	// Left to the eye this was wrong for a whole round and looked fine throughout, so it is asserted on rendered pixels.
	//
	// THE REFERENCE CHANGED, and the change is the point. It used to be a real Fusion QTabBar, because a selected tab's
	// fill lived in a private Fusion helper and rendering one was the only way to read it. The editor pane no longer
	// uses Fusion's tab shading -- it could not, since Fusion always draws the unselected tab darker and that is
	// backwards against white content -- so the reference is now a ShadedTabBar, which is what the application actually
	// puts on screen. Comparing against Fusion here would now be comparing against a widget the product does not use.
	//
	// Both focus states are checked, because the band and the strip have to move together: a band that tracked focus
	// while the strip did not would be a mismatch visible only while the user was working in the other pane.

	void the_pane_header_paints_the_same_fill_as_the_editor_tab ()
	{
		for ( const bool paneFocused : { false, true } )
		{
			PaneHeader header ( QStringLiteral ( "Explorer" ) );

			header.set_pane_focused ( paneFocused );
			header.resize ( 160, header.sizeHint ().height () );

			ShadedTabWidget reference;

			reference.setDocumentMode ( true );
			reference.addTab ( new QWidget, QStringLiteral ( "Form" ) );
			reference.addTab ( new QWidget, QStringLiteral ( "Text" ) );
			reference.resize ( 220, 120 );
			reference.shaded_tab_bar ()->set_pane_focused ( paneFocused );

			QTabBar* const bar = reference.tabBar ();

			const QRect selectedTab = bar->tabRect ( 0 );

			// Sampled above where the label sits, so the comparison is of fill and not of glyphs.

			const QColor bandFill = header.grab ().toImage ().pixelColor ( 40, 4 );
			const QColor tabFill  = bar->grab ().toImage ().pixelColor ( selectedTab.left () + 6, selectedTab.top () + 4 );

			QCOMPARE ( bandFill, tabFill );
		}
	}

	// Clicking a pane's chrome is a request to work in that pane. The band REPORTS the click rather than acting on it,
	// because it knows nothing about what its pane contains -- the tree pane routes this to the tree, since a title has
	// nothing to arrow through and focusing it would strand the keyboard.
	//
	// The signal is what can be asserted here: the offscreen platform grants focus to nothing, so
	// that the tree actually ends up with it is a manual smoke item.

	void a_left_click_on_the_band_reports_the_gesture ()
	{
		PaneHeader header ( QStringLiteral ( "Explorer" ) );

		header.resize ( 160, header.sizeHint ().height () );

		QSignalSpy clicks ( &header, &PaneHeader::clicked );

		QTest::mouseClick ( &header, Qt::LeftButton, Qt::NoModifier, QPoint ( 40, header.height () / 2 ) );

		QCOMPARE ( clicks.count (), 1 );
	}

	// The right button is left free for a future context menu on the band, so it must not be swallowed as a focus move.

	void a_right_click_on_the_band_is_not_the_gesture ()
	{
		PaneHeader header ( QStringLiteral ( "Explorer" ) );

		header.resize ( 160, header.sizeHint ().height () );

		QSignalSpy clicks ( &header, &PaneHeader::clicked );

		QTest::mouseClick ( &header, Qt::RightButton, Qt::NoModifier, QPoint ( 40, header.height () / 2 ) );

		QCOMPARE ( clicks.count (), 0 );
	}

	void the_pane_header_never_grows_with_the_pane ()
	{
		PaneHeader header ( QStringLiteral ( "Explorer" ) );

		// A header that stretched would eat the tree below it.

		QCOMPARE ( header.sizePolicy ().verticalPolicy (), QSizePolicy::Fixed );
	}

	void the_pane_header_reports_and_updates_its_title ()
	{
		PaneHeader header ( QStringLiteral ( "Explorer" ) );

		QCOMPARE ( header.title (), QStringLiteral ( "Explorer" ) );

		const int narrowHint = header.sizeHint ().width ();

		header.set_title ( QStringLiteral ( "A considerably longer pane title" ) );

		QCOMPARE ( header.title (), QStringLiteral ( "A considerably longer pane title" ) );

		QVERIFY2 ( header.sizeHint ().width () > narrowHint, "the width hint must follow the title" );
	}
};

QTEST_MAIN ( TestFocusHighlight )

#include "tst_focus_highlight.moc"


