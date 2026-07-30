//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   IDialogService -- the seam between a controller and the modal world: file pickers, the Save / Don't Save / Cancel
//   prompt (FILE-08), error and information boxes, and the yes/no confirmation.
//
//   WHY IT EXISTS. The file lifecycle is a sequence of DECISIONS -- is the document dirty, did the user cancel, did the
//   load fail, does the path already exist -- and every one of them is worth a test. A controller that calls
//   QFileDialog::getOpenFileName directly cannot be tested at all: the call blocks on a native dialog no test can
//   answer. Behind this interface the same controller runs headlessly against a fake that answers whatever the case
//   needs, which is what lets the FILE-08 gate be pinned in all three of its branches rather than smoke-tested by hand.
//
//   The interface is deliberately about INTENT rather than widgets: "ask whether to save changes" returns one of three
//   answers, not a QMessageBox::StandardButton, so the controller never spells out a button set and the fake never has
//   to imitate one.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

class QPrinter;

namespace vje
{
	class XmlImportController;

	//-----------------------------------------------------------------------------------------------------------------
	// The user's answer to the FILE-08 unsaved-changes prompt. Cancel means "abort whatever you were about to do" --
	// every caller must treat it as a refusal, not as a Don't Save.
	//-----------------------------------------------------------------------------------------------------------------

	enum class SaveChangesAnswer
	{
		Save,
		DontSave,
		Cancel
	};

	//*****************************************************************************************************************
	// Class: IDialogService
	//*****************************************************************************************************************

	class IDialogService
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		virtual ~IDialogService () = default;

		//=============================================================================================================
		// Pickers -- each returns an empty string when the user cancels.
		//=============================================================================================================

	public:

		// startPath is a FILE path (not a directory): the picker opens on its folder with its name filled in, which is
		// what makes Save As default to the document's current name.

		virtual QString choose_file_to_open ( const QString& title, const QString& filters, const QString& startPath ) = 0;
		virtual QString choose_file_to_save ( const QString& title, const QString& filters, const QString& startPath ) = 0;
		virtual QString choose_folder       ( const QString& title, const QString& startPath ) = 0;

		//=============================================================================================================
		// Prompts
		//=============================================================================================================

	public:

		// FILE-08. documentName is the caption the prompt names ("Untitled" for a document with no path yet).

		virtual SaveChangesAnswer ask_save_changes ( const QString& documentName ) = 0;

		virtual void show_error       ( const QString& title, const QString& message ) = 0;
		virtual void show_information ( const QString& title, const QString& message ) = 0;

		// A yes/no question; true means the user agreed. Default is deliberately the safe answer at each call site.

		virtual bool confirm ( const QString& title, const QString& question ) = 0;

		//=============================================================================================================
		// Import XML to JSON (FILE-13, spec section 2.11)
		//=============================================================================================================

	public:

		// Run the strategy dialog over a controller the caller has already built. true means Import -- and the
		// controller now carries the user's choices, which the caller reads back with options(). false means Cancel:
		// import nothing.
		//
		// THE CONTROLLER IS THE ARGUMENT, not a bag of values, and that is what keeps this one method rather than a
		// parameter list that grows with the dialog. Everything the dialog shows and every decision it makes is
		// XmlImportController's already (its header says why), so the seam has nothing left to carry but the identity
		// of the controller -- which is also what lets a fake answer this by setting a strategy and returning true,
		// with no widget and no preview anywhere in the test.
		//
		// fileName is display only: the dialog names the file the user picked, and nothing depends on it.

		virtual bool run_xml_import_dialog ( XmlImportController& controller, const QString& fileName ) = 0;

		//=============================================================================================================
		// Printing (FILE-12)
		//=============================================================================================================

	public:

		// The two standard platform print modals, each configuring the printer it is given IN PLACE and answering true
		// when the user accepted. FILE-12 asks for the platform's own UI here, so unlike the XML import dialog there is
		// nothing of ours behind these -- which is exactly why they belong on this seam rather than in the controller:
		// QPrintDialog::exec() blocks on a native dialog no test can answer, and everything worth asserting about
		// printing happens either side of it.
		//
		// The printer is a REFERENCE to the caller's own, and is deliberately not returned or copied: QPrinter has no
		// copy constructor, and the page setup a user chooses has to persist onto the very printer the next Print runs
		// against (PrintController holds one for the session).

		virtual bool run_page_setup_dialog ( QPrinter& printer ) = 0;
		virtual bool run_print_dialog      ( QPrinter& printer ) = 0;
	};
}
