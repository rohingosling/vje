//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FluentStyle unit tests. The class exists to change menu METRICS, so the tests assert metrics -- and, critically,
//   assert them AGAINST THE BASE STYLE rather than against bare constants. A test that only checked
//   "separator height == 9" would still pass if the override silently stopped being reached; comparing to the
//   unwrapped Fusion result is what actually proves the proxy is in the call path and is adding separation.
//
//   SINCE 2026-08-03 IT ALSO PAINTS (SET-12), and the painting cases follow this project's rule for any claim about
//   painting: they RENDER the element onto a QImage and read the pixels back (lesson Q12). A flag saying "rounding is
//   on" would be satisfied by a class that computed the radius and never used it, which is exactly the failure a
//   pixel read cannot miss.
//
//   The corner cases are written as OPPOSITES -- Fluent shows the backdrop through the corner, Classic does not -- so
//   neither can pass against a style that ignores the setting, in either direction.
//
//   Runs under the offscreen QPA platform (set by CTest), so it needs no display.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/FluentStyle.hpp"

#include "AppConfig.hpp"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QStyleOptionMenuItem>
#include <QTabBar>
#include <QTest>

#include <memory>

using namespace vje;

namespace
{
	//-----------------------------------------------------------------------------------------------------------------
	// A QTabBar the test can hold directly. Nothing is overridden -- it exists so the close-button cases drive the
	// real widget's layout rather than calling subElementRect and trusting that QTabBar consults it.
	//-----------------------------------------------------------------------------------------------------------------

	class TabBarProbe : public QTabBar
	{
	};
}

//*********************************************************************************************************************
// Class: TestFluentStyle
//*********************************************************************************************************************

class TestFluentStyle : public QObject
{
	Q_OBJECT

private:

	// A fresh Fusion instance for the baseline, and a FluentStyle wrapping a second one. Two instances are needed
	// because QProxyStyle takes ownership of the style it wraps.

	std::unique_ptr<QStyle>       baseStyle;
	std::unique_ptr<FluentStyle>  fluentStyle;

	static QStyleOptionMenuItem menu_item_option ( QStyleOptionMenuItem::MenuItemType type )
	{
		QStyleOptionMenuItem option;

		option.menuItemType = type;
		option.text         = QStringLiteral ( "Sample" );

		return option;
	}

	//=================================================================================================================
	// Painting helpers (SET-12)
	//=================================================================================================================

	// Render a push-button panel onto an opaque MAGENTA field. Magenta is chosen because no palette in this
	// application contains it, so "the backdrop is still visible here" is unambiguous: a corner that was rounded away
	// reads as exactly this colour, and any pixel the style painted reads as something else.

	static QRgb backdrop ()
	{
		return qRgb ( 255, 0, 255 );
	}

	static QImage render_button_panel ( QStyle* style, const QSize& size )
	{
		QImage canvas ( size, QImage::Format_ARGB32_Premultiplied );

		canvas.fill ( backdrop () );

		QStyleOptionButton option;

		option.rect  = QRect ( QPoint ( 0, 0 ), size );
		option.state = QStyle::State_Enabled | QStyle::State_Raised;

		{
			QPainter painter ( &canvas );

			style->drawPrimitive ( QStyle::PE_PanelButtonCommand, &option, &painter, nullptr );
		}

		return canvas;
	}

	static FluentStyle* make_style ( config::appearance::InterfaceStyle style )
	{
		return new FluentStyle ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ), style );
	}

	// HOW MUCH BACKDROP SHOWS THROUGH THE CORNER, summed over the region the radius can reach.
	//
	// A single probe pixel was tried first and does not work, which is worth recording because the reason is not
	// obvious: BOTH styles antialias their corner arc, so at every pixel the arc touches the two produce a blend
	// rather than a clean yes/no. There is no pixel that is exactly the backdrop under Fluent and exactly the panel
	// under Classic -- the extreme corner pixel is backdrop under both (the lesson D16 trap, measured rather than
	// reasoned about this time), and one pixel in, both are partial.
	//
	// So the claim is measured as a QUANTITY instead: every colour the base style paints here is a neutral grey, and
	// the backdrop is pure magenta, so "min(R,B) - G" is zero for anything the style painted and 255 for untouched
	// backdrop, scaling smoothly through the blends between. Summing it over the corner gives how much of the corner
	// was cut away -- which is exactly what a corner radius does, and is what the two styles must disagree about.

	static int corner_backdrop_weight ( const QImage& image )
	{
		const int span  = 2 * config::appearance::CONTROL_CORNER_RADIUS;
		int       total = 0;

		for ( int y = 0; y < qMin ( span, image.height () ); ++y )
		{
			for ( int x = 0; x < qMin ( span, image.width () ); ++x )
			{
				const QColor colour = image.pixelColor ( x, y );

				total += qMax ( 0, qMin ( colour.red (), colour.blue () ) - colour.green () );
			}
		}

		return total;
	}

private slots:

	void init ()
	{
		baseStyle.reset   ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) );
		fluentStyle.reset ( new FluentStyle ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) ) );

		QVERIFY2 ( baseStyle != nullptr, "Fusion must be available on every supported platform" );
	}

	//=================================================================================================================
	// Separator spacing -- the original requirement.
	//=================================================================================================================

	// The separator height is applied ABSOLUTELY, so it tracks the configured value in both directions -- above or
	// below the base style's own. Asserting equality is what proves the override is genuinely in the call path.

	void separator_uses_the_configured_height ()
	{
		const QStyleOptionMenuItem option = menu_item_option ( QStyleOptionMenuItem::Separator );

		const QSize size = fluentStyle->sizeFromContents ( QStyle::CT_MenuItem, &option, QSize (), nullptr );

		QCOMPARE ( size.height (), config::menu::SEPARATOR_HEIGHT );
	}

	void separator_can_be_tuned_below_the_base_style ()
	{
		const QStyleOptionMenuItem option = menu_item_option ( QStyleOptionMenuItem::Separator );

		const int baseHeight   = baseStyle  ->sizeFromContents ( QStyle::CT_MenuItem, &option, QSize (), nullptr ).height ();
		const int fluentHeight = fluentStyle->sizeFromContents ( QStyle::CT_MenuItem, &option, QSize (), nullptr ).height ();

		// Guards against a regression to floor semantics (qMax), under which any value below the base style's own
		// separator height would be silently ignored and the configured number would simply not apply.

		if ( config::menu::SEPARATOR_HEIGHT < baseHeight )
		{
			QVERIFY2
			(
				fluentHeight < baseHeight,
				"a separator height below the base style's must actually take effect, not be clamped up to it"
			);
		}

		QVERIFY ( fluentHeight >= config::menu::MINIMUM_SEPARATOR_HEIGHT );
	}

	//=================================================================================================================
	// Menu item rhythm.
	//=================================================================================================================

	void normal_item_meets_the_declared_minimum_height ()
	{
		const QStyleOptionMenuItem option = menu_item_option ( QStyleOptionMenuItem::Normal );

		const QSize size = fluentStyle->sizeFromContents ( QStyle::CT_MenuItem, &option, QSize (), nullptr );

		QVERIFY ( size.height () >= config::menu::ITEM_MINIMUM_HEIGHT );
	}

	void normal_item_is_at_least_as_tall_as_the_base_style ()
	{
		const QStyleOptionMenuItem option = menu_item_option ( QStyleOptionMenuItem::Normal );

		const int baseHeight   = baseStyle  ->sizeFromContents ( QStyle::CT_MenuItem, &option, QSize (), nullptr ).height ();
		const int fluentHeight = fluentStyle->sizeFromContents ( QStyle::CT_MenuItem, &option, QSize (), nullptr ).height ();

		QVERIFY ( fluentHeight >= baseHeight );
	}

	//=================================================================================================================
	// The heights are FLOORS, never truncations. Fusion derives a menu item's height from the FONT METRICS and
	// ignores the incoming contentsSize entirely (measured: a 200 px contentsSize still yields 21), so a large font
	// is the only fixture that genuinely drives the height past the minimum and exercises the qMax.
	//=================================================================================================================

	void a_large_font_is_not_clipped_by_the_minimum_height ()
	{
		QFont largeFont;

		largeFont.setPointSize ( 72 );

		QStyleOptionMenuItem option = menu_item_option ( QStyleOptionMenuItem::Normal );

		option.fontMetrics = QFontMetrics ( largeFont );

		const int baseHeight   = baseStyle  ->sizeFromContents ( QStyle::CT_MenuItem, &option, QSize (), nullptr ).height ();
		const int fluentHeight = fluentStyle->sizeFromContents ( QStyle::CT_MenuItem, &option, QSize (), nullptr ).height ();

		// Guards the fixture itself: if the large font did not push the base past the floor, the assertion below
		// would pass vacuously and prove nothing.

		QVERIFY2
		(
			baseHeight > config::menu::ITEM_MINIMUM_HEIGHT,
			"fixture is not oversized -- the large font failed to exceed the minimum row height"
		);

		QVERIFY2 ( fluentHeight >= baseHeight, "the minimum height must not clip a taller item" );
	}


	//=================================================================================================================
	// Pixel metrics.
	//=================================================================================================================

	void menu_margins_use_the_declared_values ()
	{
		QCOMPARE ( fluentStyle->pixelMetric ( QStyle::PM_MenuVMargin ), config::menu::VERTICAL_MARGIN );
		QCOMPARE ( fluentStyle->pixelMetric ( QStyle::PM_MenuHMargin ), config::menu::HORIZONTAL_MARGIN );
	}

	void menu_vertical_margin_exceeds_the_base_style ()
	{
		const int baseMargin = baseStyle->pixelMetric ( QStyle::PM_MenuVMargin );

		QVERIFY2
		(
			fluentStyle->pixelMetric ( QStyle::PM_MenuVMargin ) > baseMargin,
			"the menu frame must gain padding over Fusion's default"
		);
	}

	void toolbar_separator_is_wider_than_the_base_style ()
	{
		const int baseExtent   = baseStyle  ->pixelMetric ( QStyle::PM_ToolBarSeparatorExtent );
		const int fluentExtent = fluentStyle->pixelMetric ( QStyle::PM_ToolBarSeparatorExtent );

		QVERIFY2 ( fluentExtent > baseExtent, "toolbar button groups must be separated more than Fusion's default" );

		QCOMPARE ( fluentExtent, config::toolbar::SEPARATOR_EXTENT );
	}

	//=================================================================================================================
	// Separation is meant to come from the GROUP BREAKS, not from padding every row. This bounds the row lift rather
	// than tying it to the separator height: the two are independent aesthetic dials, and an earlier version that
	// related them meant lowering one silently forced the other down too.
	//=================================================================================================================

	void row_lift_stays_restrained ()
	{
		const QStyleOptionMenuItem normalOption = menu_item_option ( QStyleOptionMenuItem::Normal );

		const int baseRowHeight   = baseStyle  ->sizeFromContents ( QStyle::CT_MenuItem, &normalOption, QSize (), nullptr ).height ();
		const int fluentRowHeight = fluentStyle->sizeFromContents ( QStyle::CT_MenuItem, &normalOption, QSize (), nullptr ).height ();

		const int rowLift = fluentRowHeight - baseRowHeight;

		QVERIFY2
		(
			rowLift <= config::menu::MAXIMUM_ROW_LIFT,
			"the menu row is being padded rather than lifted -- separation belongs to the group breaks"
		);

		QVERIFY2 ( rowLift >= 0, "the row must never be shorter than the base style's" );
	}

	//=================================================================================================================
	// Everything not named is delegated -- the proxy must not become a general style rewrite.
	//=================================================================================================================

	void unrelated_pixel_metrics_are_delegated ()
	{
		const QList<QStyle::PixelMetric> delegated =
		{
			QStyle::PM_ButtonMargin,
			QStyle::PM_IndicatorWidth,
			QStyle::PM_ScrollBarExtent,
			QStyle::PM_ToolBarIconSize,
			QStyle::PM_SmallIconSize
		};

		for ( const QStyle::PixelMetric metric : delegated )
		{
			QCOMPARE ( fluentStyle->pixelMetric ( metric ), baseStyle->pixelMetric ( metric ) );
		}
	}

	void unrelated_contents_types_are_delegated ()
	{
		QStyleOption option;

		const QSize contentsSize ( 80, 20 );

		const QSize baseSize   = baseStyle  ->sizeFromContents ( QStyle::CT_PushButton, &option, contentsSize, nullptr );
		const QSize fluentSize = fluentStyle->sizeFromContents ( QStyle::CT_PushButton, &option, contentsSize, nullptr );

		QCOMPARE ( fluentSize, baseSize );
	}

	void menu_item_with_a_foreign_option_type_is_delegated ()
	{
		// A CT_MenuItem whose option is not a QStyleOptionMenuItem must fall straight through rather than be treated
		// as a normal item -- the qstyleoption_cast guard.

		QStyleOption option;

		const QSize contentsSize ( 100, 4 );

		const QSize baseSize   = baseStyle  ->sizeFromContents ( QStyle::CT_MenuItem, &option, contentsSize, nullptr );
		const QSize fluentSize = fluentStyle->sizeFromContents ( QStyle::CT_MenuItem, &option, contentsSize, nullptr );

		QCOMPARE ( fluentSize, baseSize );
	}

	//=================================================================================================================
	// Control shape (SET-12 / STYLE-05)
	//=================================================================================================================

	// Fluent cuts the corner away, so the backdrop shows through where the panel's square corner used to be.

	// Fluent cuts the corner away, so more of the backdrop shows through it than under Classic.
	//
	// Asserted as a MARGIN rather than as "greater than", and the floor is explicable rather than tuned: 255 is one
	// whole backdrop pixel's worth, so a floor of 200 says the Fluent corner gives up the better part of an extra
	// pixel to the backdrop. That cannot be satisfied by a rasterization difference of one pixel's antialiasing,
	// which is what a bare ">" would let through.
	//
	// Measured at 916 against 506, so the bound has better than two-fold headroom -- and measured IDENTICALLY on both
	// platforms and both Qt versions in the matrix (Windows / MinGW / Qt 6.10.1 and Linux / GCC / Qt 6.8.3, same two
	// integers). That agreement is the reason a fixed threshold is safe here at all: Qt rasterizes and antialiases
	// this path deterministically, so the number is a property of the geometry rather than of the toolkit build.

	void the_fluent_style_rounds_a_button_panel ()
	{
		const std::unique_ptr<FluentStyle> fluent  ( make_style ( config::appearance::InterfaceStyle::Fluent ) );
		const std::unique_ptr<FluentStyle> classic ( make_style ( config::appearance::InterfaceStyle::Classic ) );

		const QSize size ( 80, 24 );

		const int roundedWeight = corner_backdrop_weight ( render_button_panel ( fluent.get (),  size ) );
		const int squareWeight  = corner_backdrop_weight ( render_button_panel ( classic.get (), size ) );

		QVERIFY2
		(
			roundedWeight - squareWeight > 200,
			qPrintable ( QStringLiteral ( "rounded=%1 square=%2" ).arg ( roundedWeight ).arg ( squareWeight ) )
		);
	}

	// And Classic rounds nothing -- asserted as its corner being EXACTLY the base style's, which is the negation of
	// the case above and cannot hold for a style that rounds regardless of the setting. The pair is what makes the
	// setting testable in both directions rather than only in the one it is switched to.

	void the_classic_style_leaves_a_button_panel_square ()
	{
		const std::unique_ptr<FluentStyle> classic ( make_style ( config::appearance::InterfaceStyle::Classic ) );

		const QSize size ( 80, 24 );

		QCOMPARE
		(
			corner_backdrop_weight ( render_button_panel ( classic.get (), size ) ),
			corner_backdrop_weight ( render_button_panel ( baseStyle.get (), size ) )
		);
	}

	// Classic is reached by NOT INTERVENING, which is the property that makes it the safe fallback (FluentStyle.hpp).
	// Asserted as pixel-for-pixel equality with the unwrapped base style over the whole panel rather than at a probe:
	// anything this class did to a Classic render -- including a "harmless" round trip through an image -- shows here.

	void the_classic_style_paints_exactly_what_the_base_style_paints ()
	{
		const std::unique_ptr<FluentStyle> classic ( make_style ( config::appearance::InterfaceStyle::Classic ) );

		const QSize size ( 80, 24 );

		QCOMPARE ( render_button_panel ( classic.get (), size ), render_button_panel ( baseStyle.get (), size ) );
	}

	// The "shape, never colour" half of the contract: rounding must change the corners and nothing else, so a Fluent
	// render and a Classic one have to agree everywhere the arc cannot reach.

	void rounding_changes_the_corners_and_nothing_else ()
	{
		const std::unique_ptr<FluentStyle> fluent  ( make_style ( config::appearance::InterfaceStyle::Fluent ) );
		const std::unique_ptr<FluentStyle> classic ( make_style ( config::appearance::InterfaceStyle::Classic ) );

		const QSize  size ( 80, 24 );
		const QImage roundedRender = render_button_panel ( fluent.get (),  size );
		const QImage squareRender  = render_button_panel ( classic.get (), size );

		const int radius = config::appearance::CONTROL_CORNER_RADIUS;

		for ( int y = 0; y < size.height (); ++y )
		{
			for ( int x = 0; x < size.width (); ++x )
			{
				const bool nearLeft   = ( x < radius );
				const bool nearRight  = ( x >= size.width () - radius );
				const bool nearTop    = ( y < radius );
				const bool nearBottom = ( y >= size.height () - radius );

				if ( ( nearLeft || nearRight ) && ( nearTop || nearBottom ) )
				{
					continue;
				}

				QCOMPARE ( roundedRender.pixel ( x, y ), squareRender.pixel ( x, y ) );
			}
		}
	}

	// An element OUTSIDE the rounded set is delegated whichever style is selected. Rounding a check-box indicator is
	// not the discipline STYLE-05 asks for -- a 4 px radius on a 13 px box is a different shape, not a fillet
	// (FluentStyle.cpp says why the list stops where it does) -- and this is what stops the set widening by accident.

	void an_element_outside_the_rounded_set_is_delegated_under_both_styles ()
	{
		const std::unique_ptr<FluentStyle> fluent ( make_style ( config::appearance::InterfaceStyle::Fluent ) );

		const QSize size ( 24, 24 );

		QImage viaFluent ( size, QImage::Format_ARGB32_Premultiplied );
		QImage viaBase   ( size, QImage::Format_ARGB32_Premultiplied );

		viaFluent.fill ( backdrop () );
		viaBase.fill   ( backdrop () );

		QStyleOptionButton option;

		option.rect  = QRect ( QPoint ( 0, 0 ), size );
		option.state = QStyle::State_Enabled | QStyle::State_Off;

		{
			QPainter painter ( &viaFluent );
			fluent->drawPrimitive ( QStyle::PE_IndicatorCheckBox, &option, &painter, nullptr );
		}

		{
			QPainter painter ( &viaBase );
			baseStyle->drawPrimitive ( QStyle::PE_IndicatorCheckBox, &option, &painter, nullptr );
		}

		QCOMPARE ( viaFluent, viaBase );
	}

	// The style reports what it was constructed with, which is what lets ThemeService rebuild rather than mutate.

	void the_style_reports_its_interface_style ()
	{
		const std::unique_ptr<FluentStyle> fluent  ( make_style ( config::appearance::InterfaceStyle::Fluent ) );
		const std::unique_ptr<FluentStyle> classic ( make_style ( config::appearance::InterfaceStyle::Classic ) );

		QCOMPARE ( static_cast<int> ( fluent ->interface_style () ), static_cast<int> ( config::appearance::InterfaceStyle::Fluent ) );
		QCOMPARE ( static_cast<int> ( classic->interface_style () ), static_cast<int> ( config::appearance::InterfaceStyle::Classic ) );

		// The default-constructed case is what a test or a stale caller gets, and it must be the application's
		// specified look rather than "every override switched off".

		const std::unique_ptr<FluentStyle> defaulted ( new FluentStyle ( QStyleFactory::create ( QStringLiteral ( "Fusion" ) ) ) );

		QCOMPARE
		(
			static_cast<int> ( defaulted->interface_style () ),
			static_cast<int> ( config::appearance::DEFAULT_INTERFACE_STYLE )
		);
	}

	//=================================================================================================================
	// The editor tabs' close button (STYLE-11, VIEW-03)
	//=================================================================================================================

	// Driven through a REAL QTabBar rather than by calling subElementRect directly, because what is claimed is where
	// the button ends up -- and QTabBar is what turns the sub-element rect into the widget's geometry. A direct call
	// would agree with a style whose rect the tab bar never consults.
	//
	// Both tabs are checked, and the UNSELECTED one is the case that matters: Fusion nudges an unselected tab's
	// contents down (PM_TabBarTabShiftVertical), so before this override the gaps there were 7 above and 5 below.

	void the_tab_close_button_sits_equally_from_three_edges ()
	{
		TabBarProbe tabs;

		tabs.setStyle ( fluentStyle.get () );
		tabs.setDocumentMode ( true );
		tabs.setTabsClosable ( true );
		tabs.addTab ( QStringLiteral ( "Form" ) );
		tabs.addTab ( QStringLiteral ( "Text" ) );
		tabs.setCurrentIndex ( 0 );
		tabs.resize ( 400, 40 );

		QCoreApplication::processEvents ();

		for ( int index = 0; index < tabs.count (); ++index )
		{
			QWidget* const button = tabs.tabButton ( index, QTabBar::RightSide );

			QVERIFY2 ( button != nullptr, "a closable tab must carry a close button on its right side" );

			const QRect tabRect    = tabs.tabRect ( index );
			const QRect buttonRect = button->geometry ();

			const int topGap    = buttonRect.top ()    - tabRect.top ();
			const int bottomGap = tabRect.bottom ()    - buttonRect.bottom ();
			const int rightGap  = tabRect.right ()     - buttonRect.right ();

			// The equality IS the requirement, so it is asserted as an equality rather than against a number -- the
			// margin is derived from the button's size and the tab's height, and both are the base style's to move.

			QCOMPARE ( topGap, bottomGap );
			QCOMPARE ( topGap, rightGap );

			// ...and it is a real inset, not three zeroes agreeing. A button flush into the corner would satisfy the
			// equalities above while being exactly what the rule exists to avoid.

			QVERIFY2 ( topGap > 0, "the close button must be inset from the tab's edges, not flush into the corner" );
		}
	}

	// The complement, and the reason this override exists at all: the BASE style does not do this. Without it the
	// right gap is Fusion's own inset (13 on Qt 6.10.1, derived from PM_TabBarTabHSpace and equal to nothing), so a
	// build that stopped reaching the override would be caught by the case above rather than quietly agreeing.

	void the_base_style_does_not_place_it_equally ()
	{
		TabBarProbe tabs;

		tabs.setStyle ( baseStyle.get () );
		tabs.setDocumentMode ( true );
		tabs.setTabsClosable ( true );
		tabs.addTab ( QStringLiteral ( "Form" ) );
		tabs.setCurrentIndex ( 0 );
		tabs.resize ( 400, 40 );

		QCoreApplication::processEvents ();

		QWidget* const button = tabs.tabButton ( 0, QTabBar::RightSide );

		QVERIFY ( button != nullptr );

		const QRect tabRect    = tabs.tabRect ( 0 );
		const QRect buttonRect = button->geometry ();

		QVERIFY2
		(
			( tabRect.right () - buttonRect.right () ) != ( buttonRect.top () - tabRect.top () ),
			"Fusion already places the close button equally -- this override would then be asserting nothing"
		);
	}
};

QTEST_MAIN ( TestFluentStyle )

#include "tst_fluent_style.moc"
