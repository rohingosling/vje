//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   GoToDialog -- Edit > Go To... (FIND-04): a JSON Pointer in, the node it names selected.
//
//   IT REPORTS IN PLACE, which is what makes it a dialog class rather than a call to IDialogService's text prompt.
//   FIND-04 requires an invalid or unresolvable pointer to be reported without the selection changing and without the
//   user losing what they typed; an error message box over a prompt would close the prompt to show the error, so the
//   correction would have to be re-typed from memory. The message therefore lives on a label inside this dialog, the
//   dialog stays open, and the field keeps the text.
//
//   IT DECIDES NOTHING EITHER. Whether the text is a pointer, whether the document has such a node, and what to select
//   are FindController::go_to; the wording of each failure is describe_go_to_result(). This class is the field, the
//   label, and the rule that a failure does not close the dialog -- which is why the FIND-04 behaviour is pinned in
//   tst_find_controller rather than needing a modal loop no offscreen test can drive.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;

namespace vje
{
	class FindController;

	//*****************************************************************************************************************
	// Class: GoToDialog
	//*****************************************************************************************************************

	class GoToDialog : public QDialog
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// initialPointer pre-fills the field, selected -- in the application it is the current selection's pointer, so
		// the common case (go somewhere NEAR where I am) starts from a path rather than from an empty box.

		GoToDialog
		(
			FindController* controller,
			const QString&  initialPointer,
			QWidget*        parent = nullptr
		);

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		QLineEdit* pointer_field () const;
		QLabel*    message_label () const;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		// The OK button and Return in the field. Accepts and closes only on a resolved pointer; otherwise it writes the
		// reason onto the label and stays open with the text intact.

		void handle_submit ();

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		FindController* controller;   // Injected, non-owning.

		QLineEdit* pointerField = nullptr;
		QLabel*    messageLabel = nullptr;
	};
}
