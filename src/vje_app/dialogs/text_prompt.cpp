//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   text_prompt implementation. See the header for why the width is set by resizing.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/text_prompt.hpp"

#include "AppConfig.hpp"
#include "dialogs/dialog_frame.hpp"

#include <QDialogButtonBox>
#include <QEvent>
#include <QInputDialog>
#include <QLayout>
#include <QLineEdit>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// Class: PromptStructure
		//
		// The dialog inset and the rule above the buttons (STYLE-15), applied when the prompt is SHOWN: QInputDialog
		// builds its layout lazily, as it is first made visible, so before then there is no layout to inset and no
		// button box to put the rule over (measured: applied before exec, the prompt had neither). The show event
		// arrives after the layout exists and before the first frame.
		//-------------------------------------------------------------------------------------------------------------

		class PromptStructure : public QObject
		{
		public:

			explicit PromptStructure ( QInputDialog& dialog )
			:	QObject ( &dialog )
			,	dialog  ( dialog )
			{
				dialog.installEventFilter ( this );
			}

		protected:

			bool eventFilter ( QObject* watched, QEvent* event ) override
			{
				if ( ( event->type () == QEvent::Show ) && ( watched == &dialog ) )
				{
					apply ();
				}

				return QObject::eventFilter ( watched, event );
			}

		private:

			void apply ()
			{
				QLayout* const layout = dialog.layout ();

				if ( layout == nullptr )
				{
					return;
				}

				const int margin = config::dialog::CONTENT_MARGIN;

				layout->setContentsMargins ( margin, margin, margin, margin );

				// The layout's own spacing already separates the field from the button box, and counts toward the
				// padding above the rule.

				if ( QDialogButtonBox* const buttons = dialog.findChild<QDialogButtonBox*> () )
				{
					apply_button_rule ( dialog, *buttons, layout->spacing () );
				}

				// Now, so the first frame is already at the new inset: left to the next layout pass, the prompt was read
				// at Qt's own 11 (measured).

				layout->activate ();
			}

			QInputDialog& dialog;
		};
	}

	std::optional<QString> ask_text_prompt
	(
		QWidget*           parent,
		const QString&     title,
		const QString&     label,
		const QString&     initialValue,
		std::optional<int> width
	)
	{
		QInputDialog dialog ( parent );

		dialog.setWindowTitle  ( title );
		dialog.setLabelText    ( label );
		dialog.setTextEchoMode ( QLineEdit::Normal );
		dialog.setTextValue    ( initialValue );

		// The dialog inset every modal takes, and the rule above the buttons -- at show time (see PromptStructure).

		new PromptStructure ( dialog );

		// The height is the layout's own; only the width is stated, and only when asked for.

		if ( width.has_value () )
		{
			dialog.resize ( *width, dialog.sizeHint ().height () );
		}

		if ( dialog.exec () != QDialog::Accepted )
		{
			return std::nullopt;
		}

		return dialog.textValue ();
	}
}
