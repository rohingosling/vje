//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   PrintController -- File > Page Setup... and File > Print... (FILE-12).
//
//   THE SPLIT IS PHASE 11's, APPLIED AGAIN. What the print commands CONTAIN is two platform modals, and Qt supplies
//   both; what printing DECIDES is everything else -- whether there is anything to print, which rendering goes on the
//   paper, how it is laid out, where the page breaks fall, what the furniture says, and what the user is told
//   afterwards. All of the second list is here, so this suite is headless bar the printer itself, and the two dialogs
//   are behind IDialogService where a fake answers them.
//
//   ONE PRINTER FOR THE SESSION. Page Setup and Print configure and then use the SAME QPrinter, which is the whole
//   reason Page Setup is a command rather than a tab of the print dialog: the paper, orientation and margins a user
//   chooses have to still be there when they press Ctrl+P. It is deliberately NOT persisted to the SettingsStore --
//   a printer selection and its paper are the platform's own state, and a settings file that outlived the printer it
//   named would be a source of failures nobody could explain.
//
//   PRINTING DOES NOT RUN THE EDITOR-09 DEPARTURE GATE, and that is a decision rather than an omission. Every FILE
//   command runs it because what they write is the DOCUMENT, which an uncommitted Code View edit has not reached yet.
//   Printing writes the VIEW's rendering -- FILE-12 says so in as many words -- and an uncommitted edit is part of that
//   rendering. Running the gate would make Ctrl+P stop and ask keep / discard, which is a strange thing for a read-only
//   command to do, and would print something other than what is on the screen.
//
//   WHAT IT REPORTS (VAL-04), following the rule Phase 12.5 settled for the exports. A REFUSAL -- nothing to print, or
//   a printer that would not take the job -- goes to BOTH the modal and the status bar, because it is the outcome the
//   user cannot see for themselves: the paper simply never appears. A SUCCESS stays in the status bar alone, naming the
//   page count. A CANCELLED dialog is neither, and says nothing: the user knows what they just did.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "printing/print_content.hpp"
#include "printing/print_html.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QFont;
class QPainter;
class QPrinter;
class QSizeF;

namespace vje
{
	class IDialogService;
	class JsonDocument;
	class SettingsStore;
	class StatusService;

	//*****************************************************************************************************************
	// Class: PrintController
	//*****************************************************************************************************************

	class PrintController : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors / Destructor
		//=============================================================================================================

	public:

		// The status service may be null, which simply silences the outcome messages, and so may the settings store, in
		// which case SET-10's defaults apply. The dialog service may not: with no way to run the two modals there is
		// no print command, and both entry points answer false.

		PrintController
		(
			JsonDocument*   document,
			SettingsStore*  settings,
			IDialogService* dialogs,
			StatusService*  status,
			QObject*        parent = nullptr
		);

		~PrintController () override;

		//=============================================================================================================
		// Wiring
		//=============================================================================================================

	public:

		// Where the content comes from -- in the application, EditorPane::print_content. Injected as a callback rather
		// than taken as an EditorPane*, for the reason FileController takes its departure gate that way: this class has
		// no business knowing that views live in tabs, and a test supplies content without building one.
		//
		// The argument is the PAGE's width in characters (see page_columns), because a rendering laid out by counting
		// characters has to be produced at the width it will be read at -- IEditorView::print_content says why.
		//
		// Left unset, there is nothing to print and print() refuses accordingly.

		void set_content_source ( std::function<PrintContent ( int )> source );

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public:

		// Configure the session printer through the platform's page setup UI. True when the user accepted.

		bool page_setup ();

		// Print the active view's rendering. True only when a job actually reached the printer -- a cancelled dialog,
		// nothing to print, and a failed job all answer false, and the last two report why.

		bool print ();

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// The session printer, so Page Setup writes into the very printer the next Print runs against -- and so a test
		// can point it at a PDF file and drive the whole pipeline end to end.

		QPrinter& printer ();

		// How many pages the last print produced; 0 before the first, and after one that was refused or cancelled. The
		// pagination is ours (see the implementation), so this is a fact about the job rather than an estimate.

		int pages_printed () const;

		// What the page header names on the left: the document, named the way the title bar and the status bar name it,
		// plus the pointer of the node being shown when the rendering is of one node rather than the whole document.
		// Public because it is the one piece of the furniture with a rule worth stating rather than a position.

		QString header_text () const;

		// How many characters of the print's fixed-width font fit across the printable width of a page on this
		// printer. This is what a view is asked to wrap to, and it is exact rather than approximate because the font
		// is fixed-width -- the same property the renderings' own column alignment already depends on. 0 when the
		// metric cannot be taken.

		int page_columns ( const QPrinter& target ) const;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		PrintContent current_content ( int availableColumns ) const;
		PrintStyle   print_style     () const;

		// Lay the content out on pages and paint them. False when the painter could not be opened on the device or the
		// job failed part way -- the two printer failures the caller turns into a refusal.

		bool render ( QPrinter& target, const PrintContent& content );

		// One page's header and footer. Drawn per page rather than flowed into the text, because "Page 3 of 7" is a
		// property of the pagination and is not knowable until the layout has run.

		void paint_furniture
		(
			QPainter&     painter,
			const QFont&  furnitureFont,
			const QSizeF& pageSize,
			int           pageIndex,
			int           pageCount
		) const;

		void report_refusal ( const QString& message );            // Both channels (see the class header).
		void report_status  ( const QString& message );            // The status bar alone.

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*   document;
		SettingsStore*  settings;
		IDialogService* dialogs;
		StatusService*  status;

		std::function<PrintContent ( int )> contentSource;

		//=============================================================================================================
		// Data Members -- owned state
		//=============================================================================================================

	private:

		// Held by pointer purely so the header does not have to include QPrinter, which drags Qt PrintSupport into
		// everything that names this class.

		std::unique_ptr<QPrinter> sessionPrinter;

		int pagesPrinted = 0;
	};
}
