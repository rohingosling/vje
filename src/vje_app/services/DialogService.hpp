//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   DialogService -- the Qt Widgets realization of IDialogService: QFileDialog for the pickers and QMessageBox for the
//   prompts, parented on the main window so every modal is centred on it and blocks the right window.
//
//   It holds no state and makes no decisions. Everything about WHICH dialog to show, and what to do with the answer,
//   belongs to the caller (FileController) -- this class is the thin edge that keeps QFileDialog out of it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "services/IDialogService.hpp"

class QWidget;

namespace vje
{
	//*****************************************************************************************************************
	// Class: DialogService
	//*****************************************************************************************************************

	class DialogService : public IDialogService
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// parent may be null (a parentless modal still works; it simply centres on the screen instead of the window).

		explicit DialogService ( QWidget* parent );

		//=============================================================================================================
		// IDialogService
		//=============================================================================================================

	public:

		QString choose_file_to_open ( const QString& title, const QString& filters, const QString& startPath ) override;
		QString choose_file_to_save ( const QString& title, const QString& filters, const QString& startPath ) override;
		QString choose_folder       ( const QString& title, const QString& startPath ) override;

		SaveChangesAnswer ask_save_changes ( const QString& documentName ) override;

		void show_error       ( const QString& title, const QString& message ) override;
		void show_information ( const QString& title, const QString& message ) override;

		bool confirm ( const QString& title, const QString& question ) override;

		bool run_xml_import_dialog ( XmlImportController& controller, const QString& fileName ) override;

		bool run_page_setup_dialog ( QPrinter& printer ) override;
		bool run_print_dialog      ( QPrinter& printer ) override;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QWidget* parentWidget;                                     // Non-owning: the window every modal is parented on.
	};
}
