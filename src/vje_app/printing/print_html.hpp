//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   print_html -- the PrintContent -> HTML rendering (FILE-12). Pure, and that is the point of it: every decision about
//   how a printed page LOOKS is stated here, where it is an ordinary assertion rather than something only a printer can
//   answer. PrintController then does nothing to the appearance -- it lays the result out on pages and adds furniture.
//
//   HTML rather than a QPainter of our own, because pagination is the hard half of printing and QTextDocument already
//   does it: it breaks a flow into pages, keeps a table's rows together with their borders, and wraps text at the page
//   width. Reimplementing that to lay out three renderings by hand would be a large amount of code whose only advantage
//   is control we do not need.
//
//   PRINT IS BLACK ON WHITE, WHATEVER THE SCREEN THEME. This is not a preference: ThemeService's Dark palette draws
//   near-white text, and a page rendered in it would come out blank on white paper. So no colour reaches paper from the
//   theme -- and the Code View's syntax highlighting is deliberately not carried across either. Its token palette is
//   generated per theme, and on a monochrome printer its light-theme hues collapse to greys a few percent apart, so it
//   would cost the reader nothing and cost us a second, theme-dependent statement of what a token looks like.
//
//   A LINE TOO WIDE FOR THE PAGE WRAPS RATHER THAN BEING CUT. Preformatted content is set white-space:pre-wrap, so a
//   Spreadsheet-style table or a deeply indented array loses its alignment on the one line that overflowed instead of
//   losing its text past the right margin -- which the reader cannot see happening and cannot recover from.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "printing/print_content.hpp"

#include <QString>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The two fonts a printed page uses and the sizes they are set at. Supplied by the caller rather than read from
	// AppConfig here, so the rendering stays a pure function of its inputs -- the shipped values are PrintController's.
	//
	// fixedFamily is load-bearing: preformatted content aligns by CHARACTER COUNT (see print_content.hpp), so a
	// proportional family here breaks every table rule and every aligned separator on the page.
	//-----------------------------------------------------------------------------------------------------------------

	struct PrintStyle
	{
		QString bodyFamily;
		QString fixedFamily;

		int bodyPointSize  = 9;
		int fixedPointSize = 8;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The body of one printed rendering. A Kind::Empty content yields an empty string -- callers test that rather than
	// printing a blank page.
	//-----------------------------------------------------------------------------------------------------------------

	QString print_html ( const PrintContent& content, const PrintStyle& style );
}
