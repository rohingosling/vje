//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   FluentStyle implementation -- see FluentStyle.hpp.
//
//---------------------------------------------------------------------------------------------------------------------

#include "style/FluentStyle.hpp"

#include "AppConfig.hpp"

#include <QStyleOptionMenuItem>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FluentStyle::FluentStyle ( QStyle* baseStyle )
		: QProxyStyle ( baseStyle )
	{
	}

	//=================================================================================================================
	// QStyle Overrides
	//=================================================================================================================

	QSize FluentStyle::sizeFromContents
	(
		ContentsType        type,
		const QStyleOption* option,
		const QSize&        contentsSize,
		const QWidget*      widget
	) const
	{
		QSize size = QProxyStyle::sizeFromContents ( type, option, contentsSize, widget );

		if ( type == CT_MenuItem )
		{
			const auto* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*> ( option );

			if ( menuItem != nullptr )
			{
				if ( menuItem->menuItemType == QStyleOptionMenuItem::Separator )
				{
					// ABSOLUTE, not a floor. A separator is a rule plus padding -- it has no text or icon to clip --
					// so it is safe to set outright, and it MUST be, or the value could not be tuned below the base
					// style's own separator height (13 on Fusion / Qt 6.10.1). A qMax here silently ignores any
					// smaller setting.

					size.setHeight ( config::menu::SEPARATOR_HEIGHT );
				}
				else
				{
					// A FLOOR, never a truncation: a row carries text and icons, so a large font or a tall icon must
					// still fit. The base style's computed height wins whenever it is already the larger of the two.

					size.setHeight ( qMax ( size.height (), config::menu::ITEM_MINIMUM_HEIGHT ) );
				}
			}
		}

		return size;
	}

	int FluentStyle::pixelMetric ( PixelMetric metric, const QStyleOption* option, const QWidget* widget ) const
	{
		switch ( metric )
		{
			case PM_MenuVMargin: return config::menu::VERTICAL_MARGIN;
			case PM_MenuHMargin: return config::menu::HORIZONTAL_MARGIN;

			// The toolbar's group breaks (File | Undo/Redo | Add) carry the same separation
			// role the menu separator does, so they get the same treatment.

			case PM_ToolBarSeparatorExtent: return config::toolbar::SEPARATOR_EXTENT;

			default: break;
		}

		return QProxyStyle::pixelMetric ( metric, option, widget );
	}
}
