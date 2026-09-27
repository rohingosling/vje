//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   PrintController implementation. See the header for the one-printer-per-session rule, why printing does not run the
//   EDITOR-09 gate, and which outcomes report where.
//
//   WHY THE PAGINATION IS OURS RATHER THAN QTextDocument::print(). That call would do the page breaking for us, and it
//   is what the body layout below still uses underneath -- but it paints the whole page area, leaving no band for a
//   header or a footer, and it reports nothing about how many pages it produced. Page furniture is what makes a
//   forty-page print of a JSON document usable, and the page count is the one property of a print job a test can
//   actually assert without reading a PDF back. Both come from taking the loop.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/PrintController.hpp"

#include "AppConfig.hpp"
#include "controllers/FileController.hpp"
#include "printing/page_furniture.hpp"
#include "services/IDialogService.hpp"
#include "services/settings_profiles.hpp"
#include "services/StatusService.hpp"
#include "style/fixed_font.hpp"

#include <vje_core/document/JsonDocument.hpp>

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPointF>
#include <QPrinter>
#include <QRectF>
#include <QSizeF>
#include <QTextDocument>

#include <algorithm>
#include <utility>

namespace vje
{
	//=================================================================================================================
	// Constructors / Destructor
	//=================================================================================================================

	PrintController::PrintController
	(
		JsonDocument*   document,
		SettingsStore*  settings,
		IDialogService* dialogs,
		StatusService*  status,
		QObject*        parent
	)
		: QObject   ( parent )
		, document  ( document )
		, settings  ( settings )
		, dialogs   ( dialogs )
		, status    ( status )
	{
		// HighResolution rather than ScreenResolution: the body is laid out in the printer's own device pixels, so a
		// screen-resolution printer would round every glyph position and every table rule to a 96 dpi grid and print
		// text visibly coarser than the same document rendered by any other application.

		sessionPrinter = std::make_unique<QPrinter> ( QPrinter::HighResolution );
	}

	PrintController::~PrintController () = default;

	//=================================================================================================================
	// Wiring
	//=================================================================================================================

	void PrintController::set_content_source ( std::function<PrintContent ( int )> source )
	{
		contentSource = std::move ( source );
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	bool PrintController::page_setup ()
	{
		if ( dialogs == nullptr )
		{
			return false;
		}

		if ( !dialogs->run_page_setup_dialog ( *sessionPrinter ) )
		{
			return false;
		}

		report_status ( tr ( "Page setup updated." ) );

		return true;
	}

	bool PrintController::print ()
	{
		pagesPrinted = 0;

		if ( dialogs == nullptr )
		{
			return false;
		}

		// The content is taken BEFORE the dialog, so a refusal is reported without a print dialog ever appearing --
		// the rule the export refusals follow, and the reason the enablement and the command can disagree without the
		// user being shown a picker for something that was never going to work.

		int          columns = page_columns ( *sessionPrinter );
		PrintContent content = current_content ( columns );

		if ( content.is_empty () )
		{
			report_refusal ( tr ( "There is nothing to print. Open a document and select a node first." ) );

			return false;
		}

		if ( !dialogs->run_print_dialog ( *sessionPrinter ) )
		{
			return false;
		}

		// The dialog may have changed the paper or the printer, and a rendering wrapped to the width it had before is
		// wrapped to the wrong one. Re-asked only when the width ACTUALLY moved, because for a view whose rendering is
		// proportional to the document this is the one part of a print that is not cheap (NFR-03).

		const int finalColumns = page_columns ( *sessionPrinter );

		if ( finalColumns != columns )
		{
			columns = finalColumns;
			content = current_content ( columns );

			if ( content.is_empty () )
			{
				report_refusal ( tr ( "There is nothing to print. Open a document and select a node first." ) );

				return false;
			}
		}

		if ( !render ( *sessionPrinter, content ) )
		{
			pagesPrinted = 0;

			report_refusal ( tr ( "The document could not be sent to the printer." ) );

			return false;
		}

		report_status
		(
			( pagesPrinted == 1 )
				? tr ( "Printed %1 -- 1 page." ).arg ( header_text () )
				: tr ( "Printed %1 -- %2 pages." ).arg ( header_text () ).arg ( pagesPrinted )
		);

		return true;
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QPrinter& PrintController::printer ()
	{
		return *sessionPrinter;
	}

	int PrintController::pages_printed () const
	{
		return pagesPrinted;
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	PrintContent PrintController::current_content ( int availableColumns ) const
	{
		return contentSource ? contentSource ( availableColumns ) : PrintContent ();
	}

	int PrintController::page_columns ( const QPrinter& target ) const
	{
		QFont fixedFont = monospace_font ();

		fixedFont.setPointSize ( config::printing::FIXED_POINT_SIZE );

		const QFontMetricsF metrics ( fixedFont, &target );
		const qreal         advance = metrics.horizontalAdvance ( QLatin1Char ( '0' ) );

		if ( advance <= 0.0 )
		{
			return 0;
		}

		const qreal width = target.pageLayout ().paintRectPixels ( target.resolution () ).width ();

		// One column short of the page, for the reason the Text View is one short of its viewport: a line filling the
		// width exactly is a line whose last character sits on the margin, and any rounding against it turns into a
		// wrap the renderer did not intend.

		return std::max ( 0, static_cast<int> ( width / advance ) - 1 );
	}

	PrintStyle PrintController::print_style () const
	{
		PrintStyle style;

		style.bodyFamily  = QGuiApplication::font ().family ();

		// The SAME measured family the Text and Code views use on screen, not a family asked for by name: preformatted
		// content aligns by counting characters, and style/fixed_font exists because the platform's own answer to
		// "give me a fixed font" is not reliably one (lessons-learned Q15).

		style.fixedFamily = monospace_font ().family ();

		style.bodyPointSize  = config::printing::BODY_POINT_SIZE;
		style.fixedPointSize = config::printing::FIXED_POINT_SIZE;

		return style;
	}

	bool PrintController::render ( QPrinter& target, const PrintContent& content )
	{
		QPainter painter;

		if ( !painter.begin ( &target ) )
		{
			return false;
		}

		// The painter's origin is already the top-left of the printable area, so only the SIZE of that rectangle is
		// wanted here -- its position is expressed in page coordinates and would double the margins if applied.

		const QSizeF pageSize = target.pageLayout ().paintRectPixels ( target.resolution () ).size ();

		QFont furnitureFont = QGuiApplication::font ();

		furnitureFont.setPointSize ( config::printing::FURNITURE_POINT_SIZE );

		const qreal bandHeight = page_furniture_band_height ( furnitureFont, &target );

		const QRectF bodyRectangle
		(
			0.0,
			bandHeight,
			pageSize.width (),
			pageSize.height () - ( 2.0 * bandHeight )
		);

		// A page whose margins leave no room for a line of body text cannot be paginated at all -- the loop below would
		// divide by a non-positive height and never terminate.

		if ( ( bodyRectangle.width () <= 0.0 ) || ( bodyRectangle.height () <= 0.0 ) )
		{
			painter.end ();

			return false;
		}

		QTextDocument textDocument;

		// The layout has to measure against the PRINTER, or every point size resolves against the screen's dots per
		// inch and the text comes out at roughly an eighth of its intended physical size on a 600 dpi device.

		textDocument.documentLayout ()->setPaintDevice ( &target );
		textDocument.setDocumentMargin ( 0.0 );

		QFont bodyFont = QGuiApplication::font ();

		bodyFont.setPointSize ( config::printing::BODY_POINT_SIZE );

		textDocument.setDefaultFont ( bodyFont );
		textDocument.setHtml ( print_html ( content, print_style () ) );
		textDocument.setPageSize ( bodyRectangle.size () );

		const int pageCount = std::max ( 1, textDocument.pageCount () );

		for ( int page = 0; page < pageCount; ++page )
		{
			if ( ( page > 0 ) && !target.newPage () )
			{
				painter.end ();

				return false;
			}

			paint_furniture ( painter, furnitureFont, pageSize, page, pageCount );

			// Draw the whole flow with the origin walked up by one page's worth of height, clipped to the slice that
			// belongs on this sheet. This is the pattern QTextDocument::print uses internally; what it buys us is the
			// band above and below, which that call paints over.

			painter.save ();
			painter.translate ( bodyRectangle.left (), bodyRectangle.top () - ( page * bodyRectangle.height () ) );

			const QRectF pageBody
			(
				0.0,
				page * bodyRectangle.height (),
				bodyRectangle.width (),
				bodyRectangle.height ()
			);

			painter.setClipRect ( pageBody );

			QAbstractTextDocumentLayout::PaintContext context;

			context.clip = pageBody;

			textDocument.documentLayout ()->draw ( &painter, context );

			painter.restore ();
		}

		if ( !painter.end () )
		{
			return false;
		}

		pagesPrinted = pageCount;

		return true;
	}

	void PrintController::paint_furniture
	(
		QPainter&     painter,
		const QFont&  furnitureFont,
		const QSizeF& pageSize,
		int           pageIndex,
		int           pageCount
	) const
	{
		PageFurniture furniture;

		furniture.title     = header_text ();
		furniture.pageIndex = pageIndex;
		furniture.pageCount = pageCount;
		furniture.rules     = print_page_rules ( settings );

		paint_page_furniture ( painter, furnitureFont, pageSize, furniture );
	}

	QString PrintController::header_text () const
	{
		// Named the way the title bar, the status bar and the FILE-08 prompt name it, from the one function that
		// states that rule -- so the four cannot drift apart.

		return ( document != nullptr ) ? document_display_name ( *document ) : tr ( "Untitled" );
	}

	void PrintController::report_refusal ( const QString& message )
	{
		if ( dialogs != nullptr )
		{
			dialogs->show_error ( tr ( "Print" ), message );
		}

		report_status ( message );
	}

	void PrintController::report_status ( const QString& message )
	{
		if ( status != nullptr )
		{
			status->show_message ( message );
		}
	}
}
