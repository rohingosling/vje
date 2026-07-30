//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   GoToDialog implementation. See GoToDialog.hpp for why the failure is reported in place.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/GoToDialog.hpp"

#include "AppConfig.hpp"
#include "controllers/FindController.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	GoToDialog::GoToDialog ( FindController* controller, const QString& initialPointer, QWidget* parent )
	:	QDialog    ( parent ),
		controller ( controller )
	{
		setWindowTitle ( tr ( "Go To" ) );
		setModal ( true );

		// No context-help button in the title bar: there is no help topic behind it, and it is the one decoration Qt
		// adds by default that leads nowhere.

		setWindowFlags ( windowFlags () & ~Qt::WindowContextHelpButtonHint );

		QLabel* const promptLabel = new QLabel ( tr ( "&JSON Pointer:" ), this );

		pointerField = new QLineEdit ( this );
		pointerField->setText ( initialPointer );
		pointerField->selectAll ();
		pointerField->setPlaceholderText ( QStringLiteral ( "/projects/0/name" ) );

		promptLabel->setBuddy ( pointerField );

		// The in-place report (FIND-04). It carries no text until something fails, but it is in the layout from the
		// start so the dialog does not grow -- and jump under the user's cursor -- the first time it has something to
		// say.

		messageLabel = new QLabel ( this );
		messageLabel->setWordWrap ( true );

		QDialogButtonBox* const buttonBox = new QDialogButtonBox ( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );

		QVBoxLayout* const dialogLayout = new QVBoxLayout ( this );

		dialogLayout->setContentsMargins
		(
			config::go_to_dialog::CONTENT_MARGIN, config::go_to_dialog::CONTENT_MARGIN,
			config::go_to_dialog::CONTENT_MARGIN, config::go_to_dialog::CONTENT_MARGIN
		);

		dialogLayout->setSpacing ( config::go_to_dialog::ROW_SPACING );

		dialogLayout->addWidget ( promptLabel );
		dialogLayout->addWidget ( pointerField );
		dialogLayout->addWidget ( messageLabel );
		dialogLayout->addWidget ( buttonBox );

		resize ( config::go_to_dialog::DEFAULT_WIDTH, sizeHint ().height () );

		// OK and Return both submit; Cancel and Esc both reject. Return is wired explicitly rather than left to the
		// default-button mechanism, because a field that has just reported a failure must resubmit on Return rather
		// than re-triggering whatever the platform decided the default was.

		connect ( buttonBox,    &QDialogButtonBox::accepted, this, &GoToDialog::handle_submit );
		connect ( buttonBox,    &QDialogButtonBox::rejected, this, &GoToDialog::reject );
		connect ( pointerField, &QLineEdit::returnPressed,   this, &GoToDialog::handle_submit );

		// A failed submit leaves the reason on the label; typing again is the correction, so the stale reason goes with
		// the first keystroke rather than sitting under text it no longer describes.

		connect ( pointerField, &QLineEdit::textEdited, this, [ this ] () { messageLabel->clear (); } );

		pointerField->setFocus ( Qt::OtherFocusReason );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QLineEdit* GoToDialog::pointer_field () const
	{
		return pointerField;
	}

	QLabel* GoToDialog::message_label () const
	{
		return messageLabel;
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void GoToDialog::handle_submit ()
	{
		const GoToResult result = controller->go_to ( pointerField->text () );

		if ( result == GoToResult::Selected )
		{
			accept ();

			return;
		}

		// FIND-04: reported here, selection unchanged, text kept, dialog open.

		messageLabel->setText ( describe_go_to_result ( result ) );

		pointerField->setFocus ( Qt::OtherFocusReason );
		pointerField->selectAll ();
	}
}
