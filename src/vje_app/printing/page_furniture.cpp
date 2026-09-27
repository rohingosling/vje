//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   page_furniture implementation. See the header for why this takes a QPainter rather than living on PrintController,
//   and why the head carries the document's name alone.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "printing/page_furniture.hpp"

#include "AppConfig.hpp"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPointF>
#include <QRectF>

namespace vje
{
	qreal page_furniture_band_height ( const QFont& furnitureFont, const QPaintDevice* device )
	{
		const QFontMetricsF metrics ( furnitureFont, device );

		return metrics.height () * config::printing::FURNITURE_BAND_LINES;
	}

	void paint_page_furniture
	(
		QPainter&            painter,
		const QFont&         furnitureFont,
		const QSizeF&        pageSize,
		const PageFurniture& furniture
	)
	{
		painter.save ();
		painter.setFont ( furnitureFont );

		// Black explicitly: the painter's default pen colour comes from the paint device, and nothing about a printer
		// guarantees it is the colour the furniture should be.

		painter.setPen ( Qt::black );

		const QFontMetricsF metrics ( furnitureFont, painter.device () );
		const qreal         lineHeight = metrics.height ();

		// Elided rather than clipped, so a long file name ends in an ellipsis instead of mid-character.

		const QRectF headerRectangle ( 0.0, 0.0, pageSize.width (), lineHeight );

		painter.drawText
		(
			headerRectangle,
			Qt::AlignLeft | Qt::AlignVCenter,
			metrics.elidedText ( furniture.title, Qt::ElideMiddle, static_cast<int> ( pageSize.width () ) )
		);

		if ( furniture.rules )
		{
			const qreal headerRuleY = lineHeight + ( metrics.descent () / 2.0 );
			const qreal footerRuleY = pageSize.height () - lineHeight - ( metrics.descent () / 2.0 );

			painter.drawLine ( QPointF ( 0.0, headerRuleY ), QPointF ( pageSize.width (), headerRuleY ) );
			painter.drawLine ( QPointF ( 0.0, footerRuleY ), QPointF ( pageSize.width (), footerRuleY ) );
		}

		const QRectF footerRectangle ( 0.0, pageSize.height () - lineHeight, pageSize.width (), lineHeight );

		painter.drawText
		(
			footerRectangle,
			Qt::AlignHCenter | Qt::AlignVCenter,
			QCoreApplication::translate ( "vje", "Page %1 of %2" )
				.arg ( furniture.pageIndex + 1 )
				.arg ( furniture.pageCount )
		);

		painter.restore ();
	}
}
