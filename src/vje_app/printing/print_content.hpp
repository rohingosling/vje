//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   PrintContent -- what an editor view hands the printer (FILE-12), and the one vocabulary the three views speak.
//
//   WHY THERE ARE ONLY TWO SHAPES. FILE-12 names three renderings -- the Form View's form / table layout, the Text
//   View's plain text, the Code View's pretty-printed JSON -- but on paper those are two things, not three:
//
//     - PREFORMATTED. A block of text whose COLUMN ALIGNMENT CARRIES MEANING. TextViewRenderer draws its table rules
//       and aligns its separators by counting characters (EDITOR-06) and JSON's nesting is read off its indentation
//       (EDITOR-07), so both must be set in a fixed-width font or they stop meaning what they showed. That is a
//       correctness constraint carried over from TextView's own header, not a style preference.
//     - TABLE. Headers and rows -- the Form View's two faces. The object form has no header row (it shows none on
//       screen), the array table's headers are its column keys.
//
//   A third view added later picks whichever of the two it is; a fourth SHAPE would be a new enumerator here and a new
//   branch in print_html, and nowhere else.
//
//   WHAT IS PRINTED IS WHAT IS SHOWN. The Form View fills a Table by reading its own live models through
//   Qt::DisplayRole -- the very role the screen paints from -- so SET-03's string notation, the {...} / [...] container
//   placeholders and EDITOR-03's ragged key-union columns reach the paper without any of them being restated here. The
//   one deliberate omission is EDITOR-12's provisional row, which is a view-only affordance for growing an array and
//   not part of the document being printed.
//
//   BUT A VIEW IS ASKED FOR ITS RENDERING AT THE PAGE'S WIDTH, NOT THE PANE'S. Preformatted content is laid out by
//   COUNTING CHARACTERS, so a rendering wrapped for the pane and then re-broken by a narrower page loses exactly what
//   the wrapping was for: the Text View's continuation lines land under the value column when the renderer wraps them
//   and at column 0 when the page does. So a view is handed the page's width in characters and wraps its own content
//   to it -- which is also why the page itself never needs to wrap, and why the two renderings that must NOT be
//   wrapped can say so (Overflow::Clip) rather than being silently re-broken.
//
//   THE VIEW DESCRIBES, THE CONTROLLER TITLES. A view knows which rendering it is and which node it is showing;
//   only the application knows the document's name. So a view fills viewName and subject, and PrintController composes
//   the page header from those plus document_display_name() -- which is what keeps the document named the same way on
//   paper as in the title bar, the status bar and the FILE-08 prompt.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// One view's rendering, ready to be laid out on paper.
	//-----------------------------------------------------------------------------------------------------------------

	struct PrintContent
	{
		enum class Kind
		{
			Empty,          // Nothing to print: no document, or no view able to render one.
			Preformatted,   // text, set in a fixed-width font (see the header).
			Table           // headers + rows.
		};

		//-------------------------------------------------------------------------------------------------------------
		// What the page should do with a Kind::Preformatted line too wide for it.
		//
		// Wrap is the default and the safe answer: nothing is lost, at the cost of a continuation line landing at
		// column 0. It should rarely trigger, because a view wraps its own content to the width it was given.
		//
		// Clip is for the renderings where wrapping is not untidy but WRONG -- a CSV or TSV record broken across two
		// lines is a corrupt file, a Markdown table stops being a table, and a box-drawing table loses its borders.
		// Those are stated by the view that produced them rather than inferred from the text, so clipping is always a
		// decision somebody made about a specific rendering and never an accident of measurement.
		//-------------------------------------------------------------------------------------------------------------

		enum class Overflow
		{
			Wrap,
			Clip
		};

		Kind     kind     = Kind::Empty;
		Overflow overflow = Overflow::Wrap;

		// Which rendering this is ("Form View" / "Text View" / "Code View"). Not printed -- the page header carries the
		// document's name alone -- but kept because it names the content for a status message and for a test.

		QString viewName;

		// What is being shown, as the user would name it: the JSON Pointer of the presented node, or empty for a
		// rendering of the whole document (the Code View's).

		QString subject;

		// Kind::Preformatted. Newlines are the line breaks; nothing else about it is interpreted.

		QString text;

		// Kind::Table. An EMPTY headers list means the table has no header row -- which is the object form, whose
		// key / value columns are unlabelled on screen. Rows may be shorter than the header list; a short row's
		// missing trailing cells print empty, which is exactly what a ragged element shows in the grid.

		QStringList        headers;
		QList<QStringList> rows;

		bool is_empty () const
		{
			return kind == Kind::Empty;
		}
	};
}
