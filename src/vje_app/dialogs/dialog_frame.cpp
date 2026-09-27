//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   dialog_frame implementation. See the header for why this is a free function and what it deliberately does not
//   cover.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/dialog_frame.hpp"

#include "AppConfig.hpp"
#include "style/dialog_surface.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QMargins>
#include <QPainter>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// Class: DialogRule
		//
		// The hairline closing the content. A widget rather than a QFrame::HLine because a QFrame draws the base
		// style's own sunken 3D line from the style's own colours, which is neither one pixel nor a stated distance
		// from the surface -- and this project's rule is that chrome contrast is a distance through style/tone, so a
		// number is right on both themes rather than on the one it was picked against (tone.hpp).
		//
		// Reads its colour at PAINT time rather than caching it, so a theme change repaints it with no wiring: Qt
		// delivers a style change to every widget when ThemeService reapplies (lessons-learned Q9).
		//-------------------------------------------------------------------------------------------------------------

		class DialogRule : public QWidget
		{
		public:

			explicit DialogRule ( QWidget* parent )
			:	QWidget ( parent )
			{
				setFixedHeight ( config::dialog::RULE_THICKNESS );
				setSizePolicy ( QSizePolicy::Expanding, QSizePolicy::Fixed );

				// It is a divider, not a control: it must never take the keyboard or swallow a click.

				setFocusPolicy ( Qt::NoFocus );
				setAttribute ( Qt::WA_TransparentForMouseEvents );
			}

		protected:

			void paintEvent ( QPaintEvent* ) override
			{
				QPainter painter ( this );

				painter.fillRect ( rect (), dialog_rule ( palette () ) );
			}
		};

		// The name a rule placed by apply_button_rule carries, so a second call on the same dialog finds it.

		const QString BUTTON_RULE_NAME = QStringLiteral ( "vje_button_rule" );

		//-------------------------------------------------------------------------------------------------------------
		// Class: ButtonRulePlacer
		//
		// Keeps a rule that no layout manages in its place: edge to edge across the dialog, a fixed distance down the
		// button box's reserved top margin. It watches the dialog (for its width) and the button box (for where the
		// layout put it), and places the rule after every move or resize of either -- which is every time the layout
		// runs, since that is how the layout moves them.
		//-------------------------------------------------------------------------------------------------------------

		class ButtonRulePlacer : public QObject
		{
		public:

			ButtonRulePlacer ( QDialog& dialog, QDialogButtonBox& buttons, QWidget& rule, int offset )
			:	QObject ( &dialog )
			,	dialog  ( dialog )
			,	buttons ( buttons )
			,	rule    ( rule )
			,	offset  ( offset )
			{
				dialog.installEventFilter  ( this );
				buttons.installEventFilter ( this );

				place ();
			}

		protected:

			bool eventFilter ( QObject* watched, QEvent* event ) override
			{
				const QEvent::Type type = event->type ();

				if ( ( type == QEvent::Move ) || ( type == QEvent::Resize ) || ( type == QEvent::Show ) )
				{
					place ();
				}

				return QObject::eventFilter ( watched, event );
			}

		private:

			void place ()
			{
				rule.setGeometry ( 0, buttons.y () + offset, dialog.width (), config::dialog::RULE_THICKNESS );
				rule.raise ();
			}

			QDialog&          dialog;
			QDialogButtonBox& buttons;
			QWidget&          rule;
			int               offset;
		};
	}

	void apply_dialog_frame ( QDialog& dialog, QWidget* content, QDialogButtonBox* buttons, const QIcon& icon )
	{
		dialog.setWindowIcon ( icon );
		dialog.setModal ( true );

		// No context-help button in the title bar: there is no help topic behind it, and it is the one decoration Qt
		// adds by default that leads nowhere.

		dialog.setWindowFlags ( dialog.windowFlags () & ~Qt::WindowContextHelpButtonHint );

		// The outer layout carries no margins of its own, which is what lets the rule span the dialog EDGE TO EDGE
		// while the content and the buttons sit at their inset. A rule stopping short of both edges reads as an
		// underline beneath the content rather than as a division between two regions.

		QVBoxLayout* const outerLayout = new QVBoxLayout ( &dialog );

		outerLayout->setContentsMargins ( 0, 0, 0, 0 );
		outerLayout->setSpacing ( 0 );

		QVBoxLayout* const contentLayout = new QVBoxLayout ();

		contentLayout->setContentsMargins
		(
			config::dialog::CONTENT_MARGIN, config::dialog::CONTENT_MARGIN,
			config::dialog::CONTENT_MARGIN, config::dialog::RULE_PADDING_ABOVE
		);

		contentLayout->addWidget ( content );

		QHBoxLayout* const buttonLayout = new QHBoxLayout ();

		buttonLayout->setContentsMargins
		(
			config::dialog::CONTENT_MARGIN, config::dialog::RULE_PADDING_BELOW,
			config::dialog::CONTENT_MARGIN, config::dialog::CONTENT_MARGIN
		);

		buttonLayout->addWidget ( buttons );

		// The content takes the stretch; the rule and the button row are as tall as they need to be. A dialog that
		// grows therefore grows its content, which is the only part of it with anything to show.

		outerLayout->addLayout ( contentLayout, 1 );
		outerLayout->addWidget ( new DialogRule ( &dialog ) );
		outerLayout->addLayout ( buttonLayout );
	}

	void apply_button_rule ( QDialog& dialog, QDialogButtonBox& buttons, int gapAboveButtons )
	{
		// Once per dialog. MessageBox reapplies its structure whenever Qt rebuilds its grid, and the rule, the button
		// box and its margin all survive a rebuild -- only the grid is replaced.

		if ( dialog.findChild<QWidget*> ( BUTTON_RULE_NAME, Qt::FindDirectChildrenOnly ) != nullptr )
		{
			return;
		}

		// The rule's room is made INSIDE the button box, as a top contents margin: the box's own layout honours it, and
		// the dialog's layout -- Qt's, not ours -- then sizes the box to include it, so nothing about that layout has to
		// be known beyond the gap it already leaves above the box. That gap counts toward the padding above the rule.

		const int above = std::max ( 0, config::dialog::RULE_PADDING_ABOVE - gapAboveButtons );

		const QMargins margins = buttons.contentsMargins ();

		buttons.setContentsMargins
		(
			margins.left (),
			above + config::dialog::RULE_THICKNESS + config::dialog::RULE_PADDING_BELOW,
			margins.right (),
			margins.bottom ()
		);

		// The rule itself is a child of the DIALOG, not of the box, so it can run edge to edge across the dialog rather
		// than stop at the dialog layout's side margins -- the frame's rule does, and a rule short of both edges reads as
		// an underline rather than a division.

		DialogRule* const rule = new DialogRule ( &dialog );

		rule->setObjectName ( BUTTON_RULE_NAME );

		// Shown EXPLICITLY. MessageBox calls this from its showEvent, and Qt shows a dialog's children BEFORE it sends
		// the dialog its show event, so a child created there is not shown with it (measured: tst_message_box found no
		// rule). On a dialog not yet shown, this simply marks the rule to appear with it.

		rule->show ();

		new ButtonRulePlacer ( dialog, buttons, *rule, above );
	}
}
