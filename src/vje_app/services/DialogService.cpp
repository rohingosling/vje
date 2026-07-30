//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   DialogService implementation. See the header.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/DialogService.hpp"

#include "dialogs/XmlImportDialog.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QObject>
#include <QPageSetupDialog>
#include <QPrintDialog>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	DialogService::DialogService ( QWidget* parent )
		: parentWidget ( parent )
	{
	}

	//=================================================================================================================
	// Pickers
	//=================================================================================================================

	QString DialogService::choose_file_to_open ( const QString& title, const QString& filters, const QString& startPath )
	{
		return QFileDialog::getOpenFileName ( parentWidget, title, startPath, filters );
	}

	QString DialogService::choose_file_to_save ( const QString& title, const QString& filters, const QString& startPath )
	{
		// The platform pickers ask about overwriting an existing file themselves, so there is no confirmation here --
		// a second one would be a second opinion the user has already given.

		return QFileDialog::getSaveFileName ( parentWidget, title, startPath, filters );
	}

	QString DialogService::choose_folder ( const QString& title, const QString& startPath )
	{
		return QFileDialog::getExistingDirectory ( parentWidget, title, startPath, QFileDialog::ShowDirsOnly );
	}

	//=================================================================================================================
	// Prompts
	//=================================================================================================================

	SaveChangesAnswer DialogService::ask_save_changes ( const QString& documentName )
	{
		// FILE-08. Save is the default button and Cancel the escape route, so both the Enter key and the Esc key do the
		// safe thing: neither loses the edit.

		QMessageBox box ( parentWidget );

		box.setIcon ( QMessageBox::Warning );
		box.setWindowTitle ( QObject::tr ( "VJE" ) );
		box.setText ( QObject::tr ( "Save changes to \"%1\"?" ).arg ( documentName ) );
		box.setInformativeText ( QObject::tr ( "Your changes will be lost if you don't save them." ) );
		box.setStandardButtons ( QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );
		box.setDefaultButton ( QMessageBox::Save );
		box.setEscapeButton ( QMessageBox::Cancel );

		switch ( box.exec () )
		{
			case QMessageBox::Save:    return SaveChangesAnswer::Save;
			case QMessageBox::Discard: return SaveChangesAnswer::DontSave;
			default:                   return SaveChangesAnswer::Cancel;
		}
	}

	void DialogService::show_error ( const QString& title, const QString& message )
	{
		QMessageBox::critical ( parentWidget, title, message );
	}

	void DialogService::show_information ( const QString& title, const QString& message )
	{
		QMessageBox::information ( parentWidget, title, message );
	}

	bool DialogService::confirm ( const QString& title, const QString& question )
	{
		// No is the default: a confirmation exists because the action is worth a second thought, so the keyboard's
		// reflex answer must be the one that changes nothing.

		const QMessageBox::StandardButton answer = QMessageBox::question
		(
			parentWidget,
			title,
			question,
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No
		);

		return answer == QMessageBox::Yes;
	}

	bool DialogService::run_xml_import_dialog ( XmlImportController& controller, const QString& fileName )
	{
		// Constructed on the stack for the same reason every other modal here is: it lives exactly as long as its own
		// exec(), and the controller it edits belongs to the caller and outlives both.

		XmlImportDialog dialog ( &controller, fileName, parentWidget );

		return dialog.exec () == QDialog::Accepted;
	}

	bool DialogService::run_page_setup_dialog ( QPrinter& printer )
	{
		// Both print modals write straight into the printer they are given, so there is nothing to read back: an
		// accepted dialog has already applied the user's paper, orientation and margins to the caller's printer.

		QPageSetupDialog dialog ( &printer, parentWidget );

		return dialog.exec () == QDialog::Accepted;
	}

	bool DialogService::run_print_dialog ( QPrinter& printer )
	{
		QPrintDialog dialog ( &printer, parentWidget );

		dialog.setWindowTitle ( QObject::tr ( "Print" ) );

		return dialog.exec () == QDialog::Accepted;
	}
}
