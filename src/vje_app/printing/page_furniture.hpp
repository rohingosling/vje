//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   page_furniture -- what a printed page carries besides its content (FILE-12): the document's name at the head, the
//   page number at the foot, and the two hairlines between them and the body.
//
//   WHY IT IS A FREE FUNCTION RATHER THAN A METHOD ON PrintController. What it does is PAINT, and this project's rule
//   for a paint claim is that it is only checked by comparing rendered pixels (lessons-learned Q12). Behind a printer
//   there is nothing to read back; taking a QPainter instead means the same code can be run onto a QImage and the
//   result measured -- which is what makes SET-10's "show page rules" a checked behaviour rather than a setting nobody
//   can prove is connected to anything.
//
//   WHAT THE HEAD CARRIES, AND WHY IT IS ONLY THAT. The document's name, left aligned, and nothing else. It carried the
//   selected node's JSON Pointer and the view's name as well; both were dropped, because neither is what a reader
//   picking a printed page off a desk needs -- and a pointer in particular is long, moves with an edit, and means
//   nothing to anyone who was not driving the application.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

#include <QSizeF>

class QFont;
class QPaintDevice;
class QPainter;

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// One page's furniture. pageIndex is 0-based; pageCount is what "of N" reads.
	//-----------------------------------------------------------------------------------------------------------------

	struct PageFurniture
	{
		QString title;

		int pageIndex = 0;
		int pageCount = 1;

		// SET-10. The hairlines separate the head and the foot from the body with something other than white space,
		// which on a page of preformatted text reads as a blank line rather than as a margin. Off for printing onto
		// letterhead, or scanning the result.

		bool rules = true;
	};

	// Paint the furniture into the band at the top and bottom of a page pageSize device pixels across. The painter's
	// origin is the top-left of the printable area, which is what QPainter::begin on a QPrinter already gives.

	void paint_page_furniture
	(
		QPainter&            painter,
		const QFont&         furnitureFont,
		const QSizeF&        pageSize,
		const PageFurniture& furniture
	);

	// How tall one furniture band is for a given font, in device pixels: one line of text plus one of air, so the body
	// never sits hard against the page number. PrintController subtracts two of these from the page to find the body.

	qreal page_furniture_band_height ( const QFont& furnitureFont, const QPaintDevice* device );
}
