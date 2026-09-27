//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FluentStyle -- a QProxyStyle over Fusion that applies the Fluent-inspired menu metrics (STYLE-01). It widens the air around menu separators, gives menu items a
//   taller minimum row, and pads the menu
//   frame, so the menus read with the vertical rhythm of a modern Fluent shell rather than Fusion's tight default.
//
//   Why a proxy style and not a style sheet. ThemeService applies the theme as explicit light/dark QPalettes; a QSS
//   rule would have to restate colours (or reach for palette(...) roles) and, once any QMenu sub-control is styled,
//   QStyleSheetStyle takes over that menu's painting and the rest of the menu has to be restated too. A proxy style
//   changes only the metrics it names and leaves every colour to the palette, so theming and layout stay independent.
//
//   Fusion draws the separator rule at the vertical centre of the item's rect, so simply making the separator item
//   taller distributes the added space evenly above and below it.
//
//   SHAPE, AS WELL AS METRICS (2026-08-03, SET-12). Every earlier version of this class overrode metrics and NOTHING
//   ELSE, and said so as a contract: with no painting overridden every colour still came from the palette, so theming
//   and layout stayed independent. STYLE-05's ~4 px control radius ends that, because a radius is a SHAPE and there
//   are exactly two places a shape can come from -- a stylesheet, or the style. A stylesheet was rejected in Phase 14
//   and the reason is unchanged: setting one puts QStyleSheetStyle into the paint path of a whole widget subtree, and
//   this application's subtrees are full of widgets that already paint their own surfaces.
//
//   THE CONTRACT IS NOW NARROWER, NOT WEAKER: this class changes SHAPE and METRICS, and never COLOUR. It states no
//   brush, no pen colour and no gradient anywhere -- the base style still draws the panel, from the palette, in
//   whatever state it decides -- and the override only confines the result to a rounded outline. So a theme change
//   still recolours every control without consulting this class, and this class cannot make a control disagree with
//   the palette, because it never names a colour. What is genuinely lost is the ease of CHECKING the claim: "it does
//   not paint" was true at a glance, while "it paints, but only shape" is what tst_fluent_style measures in pixels.
//
//   THE MECHANISM IS RENDER-MASK-RESTROKE, chosen because it restates nothing. The alternative was to draw the panels
//   outright -- fill, border, gradient, and a branch per state per control kind, in both themes -- which is a second
//   statement of what every control looks like, kept in step by hand with the one the base style already has. Instead:
//
//     1. The base style draws the element into a transparent image of the element's own size.
//     2. The border colour is SAMPLED from that image rather than computed, so the re-stroke below uses literally the
//        colour the base style just used -- which tracks the theme, and the hover / pressed / disabled / focused
//        states, for free and by construction.
//     3. The four corner wedges are erased, antialiased, leaving the rounded outline (the rule views/Card follows).
//     4. The outline is re-stroked in the sampled colour, because erasing the wedges takes the base style's own
//        border away with them and leaves the ring open at each corner.
//
//   Under InterfaceStyle::Classic every override is a straight delegation, so Classic is not a second look to
//   maintain -- it is the base style, reached by not intervening. That asymmetry is why Fluent is the default and
//   Classic the fallback rather than the other way round: the value that does nothing is the safe one to fall back to.
//
//   ONE HINT IS NOT A LOOK, AND HOLDS UNDER BOTH (LD-9, Phase 15j.1). styleHint answers Windows' button order and
//   right-aligned message-box buttons whatever the base style says, because Fusion forwards that question to the
//   platform theme and the Linux build would otherwise follow the desktop. It is a convention the application keeps
//   on every platform, not part of the Fluent look, so Classic keeps it too.
//
//   COST, STATED: one image allocation per painted control, freed immediately. Controls repaint on hover, focus and
//   state changes rather than continuously, and the images are small (a 200x24 panel is ~19 KB), so this is bounded
//   and was accepted rather than cached. If it ever shows, the fix is a pixmap cache keyed on ( size, state, style ),
//   not a return to stating the colours here.
//
//   The values themselves live in AppConfig.hpp (config::menu, config::toolbar, config::appearance), not here, so
//   every compile-time tunable in the application is found in one place. This class owns the MECHANISM; AppConfig owns
//   the NUMBERS.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "AppConfig.hpp"

#include <QProxyStyle>

namespace vje
{
	//*****************************************************************************************************************
	// Class: FluentStyle
	//*****************************************************************************************************************

	class FluentStyle : public QProxyStyle
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// Takes ownership of baseStyle, as QProxyStyle does. Passing nullptr proxies the application style.
		//
		// The interface style is fixed at construction rather than settable, because ThemeService::apply() already
		// builds a fresh instance every time it runs and re-installs it -- so "the setting changed" is answered by the
		// same call that answers "the theme changed", and there is no second path by which the two could disagree.

		explicit FluentStyle
		(
			QStyle*                            baseStyle = nullptr,
			config::appearance::InterfaceStyle style     = config::appearance::DEFAULT_INTERFACE_STYLE
		);

		//=============================================================================================================
		// QStyle Overrides
		//=============================================================================================================

	public:

		QSize sizeFromContents
		(
			ContentsType        type,
			const QStyleOption* option,
			const QSize&        contentsSize,
			const QWidget*      widget
		) const override;

		int pixelMetric
		(
			PixelMetric         metric,
			const QStyleOption* option = nullptr,
			const QWidget*      widget = nullptr
		) const override;

		// Where the editor tabs' close button sits (STYLE-11, VIEW-03). A rect rather than a metric, because the answer
		// is a POSITION inside the tab and Qt exposes that as a sub-element. Like the two above and unlike the painting
		// pair below, it applies under BOTH interface styles: where a button sits is not a corner-radius question.

		QRect subElementRect
		(
			SubElement          element,
			const QStyleOption* option,
			const QWidget*      widget = nullptr
		) const override;

		// The button order of every button box in the application (LD-9, STYLE-18): Windows' order, right-aligned, on
		// every platform. A hint rather than a shape, and like the two metrics above it applies under BOTH interface
		// styles -- Classic changes how a control is drawn, not which platform's conventions the application follows.

		int styleHint
		(
			StyleHint           hint,
			const QStyleOption* option      = nullptr,
			const QWidget*      widget      = nullptr,
			QStyleHintReturn*   returnData  = nullptr
		) const override;

		// The two paint entry points STYLE-05's control radius needs. Both are plain delegations under Classic, and
		// both are delegations under Fluent too for every element not in the rounded set -- see the .cpp for which
		// elements are in it, and why the list stops where it does.

		void drawPrimitive
		(
			PrimitiveElement    element,
			const QStyleOption* option,
			QPainter*           painter,
			const QWidget*      widget = nullptr
		) const override;

		void drawComplexControl
		(
			ComplexControl             control,
			const QStyleOptionComplex* option,
			QPainter*                  painter,
			const QWidget*             widget = nullptr
		) const override;

		//=============================================================================================================
		// Accessors
		//=============================================================================================================

	public:

		config::appearance::InterfaceStyle interface_style () const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		config::appearance::InterfaceStyle style;
	};
}
