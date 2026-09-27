//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   print_html implementation. See the header for why the page is black on white whatever the theme, and why an
//   over-wide preformatted line wraps rather than being cut.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "printing/print_html.hpp"

#include "AppConfig.hpp"

#include <algorithm>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// A family name safe to drop inside a single-quoted CSS font-family. Only the quote character can break out of
		// it, and no real family carries one -- but the families here come from the platform's font database rather
		// than from us, so this is checked rather than assumed.
		//-------------------------------------------------------------------------------------------------------------

		QString quoted_family ( const QString& family )
		{
			QString sanitized = family;

			sanitized.remove ( QLatin1Char ( '\'' ) );

			return QStringLiteral ( "'%1'" ).arg ( sanitized );
		}

		//-------------------------------------------------------------------------------------------------------------
		// Cell text. A DisplayRole value may carry real line breaks -- a Decoded string under SET-03 is the ordinary
		// case -- and inside a table cell HTML would run those lines together into one, so they become breaks. An empty
		// cell is given a non-breaking space so a row of nothing but absent members still draws its grid rather than
		// collapsing to a hairline.
		//-------------------------------------------------------------------------------------------------------------

		QString cell_html ( const QString& text )
		{
			if ( text.isEmpty () )
			{
				return QStringLiteral ( "&nbsp;" );
			}

			QString escaped = text.toHtmlEscaped ();

			escaped.replace ( QLatin1Char ( '\n' ), QStringLiteral ( "<br/>" ) );

			return escaped;
		}
	}

	QString print_html ( const PrintContent& content, const PrintStyle& style )
	{
		if ( content.is_empty () )
		{
			return QString ();
		}

		QString html;

		// The colour is stated on the body rather than left to the paint palette, so the same HTML prints identically
		// whoever renders it -- a test reading it back included.

		html += QStringLiteral ( "<html><body style=\"color:#000000;\">" );

		if ( content.kind == PrintContent::Kind::Preformatted )
		{
			// pre-wrap is the safety net rather than the mechanism: the view has already wrapped its content to the
			// page's own width, so this should never fire -- and where it does (a line no wrapping rule could break,
			// or a caller that supplied no width) nothing is lost, at the cost of a continuation at column 0.
			//
			// pre CLIPS, and is used only where a view has said its rendering must not be wrapped at all: a CSV or
			// TSV record broken across two lines is a corrupt file, not an untidy one (print_content.hpp).

			const QString whiteSpace = ( content.overflow == PrintContent::Overflow::Clip )
			                               ? QStringLiteral ( "pre" )
			                               : QStringLiteral ( "pre-wrap" );

			// margin:0 because the page furniture already provides the air; Qt's default <pre> margin would add a
			// blank line to the top of every page's worth of flow.

			html += QStringLiteral ( "<pre style=\"font-family:%1; font-size:%2pt; white-space:%3; margin:0;\">" )
			        .arg ( quoted_family ( style.fixedFamily ) )
			        .arg ( style.fixedPointSize )
			        .arg ( whiteSpace );

			html += content.text.toHtmlEscaped ();

			html += QStringLiteral ( "</pre>" );
		}
		else
		{
			// Both the HTML attributes and the CSS state the grid. Qt's rich text reads the attributes for the width
			// and the CSS for the style and colour, and neither alone produces a solid black rule.

			html += QStringLiteral
			(
				"<table border=\"%1\" cellspacing=\"0\" cellpadding=\"%2\" width=\"100%\" "
				"style=\"font-family:%3; font-size:%4pt; border-style:solid; border-color:#000000; "
				"border-collapse:collapse;\">"
			)
			.arg ( config::printing::TABLE_BORDER_WIDTH )
			.arg ( config::printing::TABLE_CELL_PADDING )
			.arg ( quoted_family ( style.bodyFamily ) )
			.arg ( style.bodyPointSize );

			// The header row exists only when the view shows one -- the object form's key / value columns are
			// unlabelled on screen and stay unlabelled on paper (print_content.hpp).

			if ( !content.headers.isEmpty () )
			{
				html += QStringLiteral ( "<tr>" );

				for ( const QString& header : content.headers )
				{
					html += QStringLiteral ( "<th align=\"left\">%1</th>" ).arg ( cell_html ( header ) );
				}

				html += QStringLiteral ( "</tr>" );
			}

			// The column count is the header row's where there is one, and otherwise the widest row -- so a table
			// whose rows disagree still draws a rectangle, with the short rows' trailing cells empty.

			int columnCount = content.headers.size ();

			for ( const QStringList& row : content.rows )
			{
				columnCount = std::max ( columnCount, static_cast<int> ( row.size () ) );
			}

			for ( const QStringList& row : content.rows )
			{
				html += QStringLiteral ( "<tr>" );

				for ( int column = 0; column < columnCount; ++column )
				{
					const QString value = column < row.size () ? row.at ( column ) : QString ();

					html += QStringLiteral ( "<td>%1</td>" ).arg ( cell_html ( value ) );
				}

				html += QStringLiteral ( "</tr>" );
			}

			html += QStringLiteral ( "</table>" );
		}

		html += QStringLiteral ( "</body></html>" );

		return html;
	}
}
