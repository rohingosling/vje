//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   window_title -- what the title bar reads (spec section 2.2), as a pure function.
//
//   WHY IT IS NOT JUST TWO LINES IN MainWindow. The format is a stated rule with three cases -- no document, a document,
//   a document with unsaved changes -- and there is no MainWindow harness to check any of them through. Pulled out
//   here it is an ordinary headless assertion, which is the same move printing/page_furniture, views/toolbar_plan and
//   controllers/edit_reporting each make for the same reason: the DECISION is testable even where the widget is not.
//
//   THE APPLICATION COMES FIRST because it is the constant. A taskbar button, an Alt+Tab entry and a window list all
//   truncate from the RIGHT, and the half worth keeping is the one that says which application this is.
//
//   "VJE" ALONE MEANS NOTHING IS OPEN, not nothing is saved. A new, never-saved document is still a document and takes
//   the name "Untitled" -- the caller supplies that name (controllers/FileController's document_display_name), so this
//   function never has to know what an unnamed document is called.
//
//   THE MODIFIED MARKER IS OURS RATHER THAN QT'S "[*]" placeholder, which substitutes an asterisk and cannot be told to
//   substitute anything else. MainWindow::update_title carries the rest of that note, including why setWindowModified
//   is not called alongside it and what that costs a macOS port.
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
	// The caption for a window showing `documentName`, or showing nothing when it is empty.
	//
	//   ( "VJE", "",              false ) -> "VJE"
	//   ( "VJE", "test.json",     false ) -> "VJE ▪ test.json"
	//   ( "VJE", "test.json",     true  ) -> "VJE ▪ test.json ●"
	//   ( "VJE", "Untitled",      true  ) -> "VJE ▪ Untitled ●"
	//
	// An empty document name drops the separator AND the marker together: a window with nothing open has nothing to be
	// modified, so a marker there would name a document that is not there.

	QString window_title ( const QString& applicationName, const QString& documentName, bool modified );
}
