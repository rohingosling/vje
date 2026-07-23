//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   FluentStyle unit tests. The class exists to change menu METRICS, so the tests assert metrics -- and, critically,
//   assert them AGAINST THE BASE STYLE rather than against bare constants. A test that only checked
//   "separator height == 9" would still pass if the override silently stopped being reached; comparing to the
//   unwrapped Fusion result is what actually proves the proxy is in the call path and is adding separation.
//
//   Runs under the offscreen QPA platform (set by CTest), so it needs no display.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/FluentStyle.hpp"

#include "AppConfig.hpp"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QTest>

#include <memory>

using namespace vje;

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
};

QTEST_MAIN ( TestFluentStyle )

#include "tst_fluent_style.moc"
