//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tooltip_text -- the ONE formatter every tooltip's text passes through (STYLE-17), so a tooltip's measure, its length
//   and its treatment of markup are stated once rather than decided again at each call site.
//
//   WHY IT IS NEEDED AT ALL. Qt shows a plain-text tooltip on a single line however long it is (QTipLabel word-wraps
//   only text it takes for rich text), so a long string value in a tree tooltip or a cell runs to the edge of the
//   screen. And it takes text for rich text by SNIFFING it (Qt::mightBeRichText): a JSON string holding "<b>x</b>" is
//   rendered bold, which shows the user a value their document does not contain.
//
//   So the formatter wraps to config::tooltip::LINE_LENGTH -- at a space where there is one, inside the run where there
//   is none -- keeps the line breaks it was given, stops after config::tooltip::MAXIMUM_LINES with an ellipsis, and
//   hands text that would be sniffed as markup back as escaped, preformatted rich text, so it is shown exactly as
//   written. Pure, and headlessly tested.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

namespace vje
{
	// The tooltip for `text`: wrapped, capped, and safe from markup sniffing. Empty text stays empty, which is what
	// "no tooltip" is to Qt.

	QString tooltip_text ( const QString& text );
}
