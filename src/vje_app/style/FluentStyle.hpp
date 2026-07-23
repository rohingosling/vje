//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
//   Only metrics are overridden -- no painting. Fusion draws the separator rule at the vertical centre of the item's
//   rect, so simply making the separator item taller distributes the added space evenly above and below it.
//
//   The values themselves live in AppConfig.hpp (config::menu, config::toolbar), not here, so every compile-time
//   tunable in the application is found in one place. This class owns the MECHANISM; AppConfig owns the NUMBERS.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

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

		explicit FluentStyle ( QStyle* baseStyle = nullptr );

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
	};
}
