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

#include "dialogs/MessageBox.hpp"
#include "dialogs/text_prompt.hpp"
#include "dialogs/XmlImportDialog.hpp"
#include "services/IconLibrary.hpp"

#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QObject>
#include <QPageSetupDialog>
#include <QPrintDialog>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	DialogService::DialogService ( QWidget* parent, IconLibrary* icons )
		: parentWidget ( parent )
		, icons        ( icons )
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

	QColor DialogService::choose_colour ( const QString& title, const QColor& initial )
	{
		// Qt's standard colour dialog, left as the platform gives it -- like the file pickers, it is the toolkit's
		// (STYLE-15 leaves the platform pickers untouched). Cancel answers an invalid colour.

		return QColorDialog::getColor ( initial, parentWidget, title );
	}

	//=================================================================================================================
	// Prompts
	//=================================================================================================================

	SaveChangesAnswer DialogService::ask_save_changes ( const QString& documentName )
	{
		// FILE-08. Save is the default button and Cancel the escape route, so both the Enter key and the Esc key do the
		// safe thing: neither loses the edit.

		// A Warning (STYLE-18 (5)): Don't Save discards the edit. The Discard button reads "Don't Save" through
		// MessageBox's caption table, which is FILE-08's word; Qt's own caption for it on Windows was "Discard".

		MessageBox box
		(
			MessageKind::Warning,
			QObject::tr ( "VJE" ),
			QObject::tr ( "Save changes to \"%1\"?" ).arg ( documentName ),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
			QMessageBox::Save,
			QMessageBox::Cancel,
			parentWidget
		);

		box.setInformativeText ( QObject::tr ( "Your changes will be lost if you don't save them." ) );

		switch ( box.ask () )
		{
			case QMessageBox::Save:    return SaveChangesAnswer::Save;
			case QMessageBox::Discard: return SaveChangesAnswer::DontSave;
			default:                   return SaveChangesAnswer::Cancel;
		}
	}

	void DialogService::show_error ( const QString& title, const QString& message )
	{
		show_message_box ( parentWidget, MessageKind::Error, title, message );
	}

	void DialogService::show_warning ( const QString& title, const QString& message )
	{
		show_message_box ( parentWidget, MessageKind::Warning, title, message );
	}

	void DialogService::show_information ( const QString& title, const QString& message )
	{
		show_message_box ( parentWidget, MessageKind::Information, title, message );
	}

	bool DialogService::confirm ( const QString& title, const QString& question )
	{
		// No is the default AND the escape: a confirmation exists because the action is worth a second thought, so the
		// keyboard's reflex answer must be the one that changes nothing. A Warning rather than Qt's question mark
		// (STYLE-18 (5)): every confirmation VJE asks is one whose Yes changes something.

		const QMessageBox::StandardButton answer = ask_message_box
		(
			parentWidget,
			MessageKind::Warning,
			title,
			question,
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No,
			QMessageBox::No
		);

		return answer == QMessageBox::Yes;
	}

	std::optional<QString> DialogService::ask_text ( const QString& title, const QString& label, const QString& initialValue )
	{
		// The same prompt as the key prompts -- the dialog inset and the rule above the buttons (STYLE-15) -- at the
		// width Qt's layout gives it: this is Rename Column's, which was not given the key prompts' stated width.

		return ask_text_prompt ( parentWidget, title, label, initialValue, std::nullopt );
	}

	bool DialogService::run_xml_import_dialog ( XmlImportController& controller, const QString& fileName )
	{
		// Constructed on the stack for the same reason every other modal here is: it lives exactly as long as its own
		// exec(), and the controller it edits belongs to the caller and outlives both.

		// The title-bar glyph is document-open rather than one of its own: this dialog is the second step of File >
		// Import > XML, whose first step IS the Open picker, and the 43-glyph set has no import mark. Drawing a 44th
		// would mean drawing it at every authored size, which is not what this phase is for.

		const QIcon icon = ( icons != nullptr ) ? icons->icon ( icon_names::DOCUMENT_OPEN ) : QIcon ();

		XmlImportDialog dialog ( &controller, fileName, icon, parentWidget );

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
