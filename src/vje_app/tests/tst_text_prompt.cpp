//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   tst_text_prompt -- the one-line text prompt the Add commands and Rename Key use: it opens at the stated width, and
//   answers the text or nothing.
//
//   THE WIDTH IS READ FROM THE PROMPT WHILE IT IS OPEN, from inside its own modal loop, because that is the only moment
//   it exists at its shown size. A guard reads Qt's own prompt the same way first, so the case cannot pass because the
//   natural width happened to be the stated one.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "AppConfig.hpp"
#include "dialogs/text_prompt.hpp"
#include "style/dialog_surface.hpp"

#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
#include <QTimer>

using namespace vje;

namespace
{
	// Queues a look at the prompt about to open: records its width, then answers it.

	void answer_next_prompt ( int* width, const QString& typed, bool accept )
	{
		QTimer::singleShot ( 0, [ width, typed, accept ] ()
		{
			QInputDialog* const prompt = qobject_cast<QInputDialog*> ( QApplication::activeModalWidget () );

			if ( prompt == nullptr )
			{
				return;
			}

			*width = prompt->width ();

			prompt->setTextValue ( typed );

			if ( accept )
			{
				prompt->accept ();
			}
			else
			{
				prompt->reject ();
			}
		} );
	}
}

//*********************************************************************************************************************
// Class: TestTextPrompt
//*********************************************************************************************************************

class TestTextPrompt : public QObject
{
	Q_OBJECT

private slots:

	void the_prompt_opens_at_the_stated_width ()
	{
		// Guard: Qt's own prompt opens at another width (measured 200).

		int naturalWidth = 0;

		answer_next_prompt ( &naturalWidth, QString (), false );

		bool accepted = false;

		QInputDialog::getText ( nullptr, QStringLiteral ( "Add Child" ), QStringLiteral ( "Key:" ), QLineEdit::Normal, QStringLiteral ( "newKey" ), &accepted );

		QVERIFY2 ( ( naturalWidth > 0 ) && ( naturalWidth != config::text_prompt::WIDTH ), qPrintable ( QStringLiteral ( "natural %1" ).arg ( naturalWidth ) ) );

		int width = 0;

		answer_next_prompt ( &width, QString (), false );

		ask_text_prompt ( nullptr, QStringLiteral ( "Add Child" ), QStringLiteral ( "Key:" ), QStringLiteral ( "newKey" ) );

		QCOMPARE ( width, config::text_prompt::WIDTH );
	}

	void a_rule_divides_the_buttons_from_the_field_as_in_every_dialog ()
	{
		// Read from inside the prompt's own modal loop, the one moment it exists at its shown size. Rows inked edge to
		// edge in the rule's colour, as tst_dialog_frame counts the framed dialogs' rule.

		QList<int> rows;
		int        fieldBottom = -1;
		int        buttonsTop  = -1;
		int        inset       = -1;

		QTimer::singleShot ( 0, [ & ] ()
		{
			QInputDialog* const prompt = qobject_cast<QInputDialog*> ( QApplication::activeModalWidget () );

			if ( prompt == nullptr )
			{
				return;
			}

			const QImage image = prompt->grab ().toImage ();
			const QRgb   rule  = dialog_rule ( prompt->palette () ).rgb ();
			const qreal  scale = image.devicePixelRatio ();

			for ( int y = 0; y < prompt->height (); ++y )
			{
				bool inked = true;

				for ( int x = 0; ( x < prompt->width () ) && inked; ++x )
				{
					inked = ( image.pixel ( qRound ( x * scale ), qRound ( y * scale ) ) & 0xFFFFFFu ) == ( rule & 0xFFFFFFu );
				}

				if ( inked )
				{
					rows.append ( y );
				}
			}

			const QLineEdit*        const field   = prompt->findChild<QLineEdit*> ();
			const QDialogButtonBox* const buttons = prompt->findChild<QDialogButtonBox*> ();

			fieldBottom = field->geometry ().bottom () + 1;
			buttonsTop  = buttons->buttons ().first ()->mapTo ( prompt, QPoint ( 0, 0 ) ).y ();
			inset       = field->x ();

			prompt->reject ();
		} );

		ask_text_prompt ( nullptr, QStringLiteral ( "Add Child" ), QStringLiteral ( "Key:" ), QStringLiteral ( "newKey" ) );

		QCOMPARE ( rows.size (), config::dialog::RULE_THICKNESS );

		QCOMPARE ( rows.first () - fieldBottom,                                   config::dialog::RULE_PADDING_ABOVE );
		QCOMPARE ( buttonsTop - ( rows.first () + config::dialog::RULE_THICKNESS ), config::dialog::RULE_PADDING_BELOW );
		QCOMPARE ( inset,                                                          config::dialog::CONTENT_MARGIN );
	}

	void the_prompt_answers_the_text_or_nothing ()
	{
		int width = 0;

		answer_next_prompt ( &width, QStringLiteral ( "renamed" ), true );

		QCOMPARE ( ask_text_prompt ( nullptr, QStringLiteral ( "Rename Key" ), QStringLiteral ( "Key:" ), QStringLiteral ( "old" ) ), std::optional<QString> ( QStringLiteral ( "renamed" ) ) );

		answer_next_prompt ( &width, QStringLiteral ( "ignored" ), false );

		QCOMPARE ( ask_text_prompt ( nullptr, QStringLiteral ( "Rename Key" ), QStringLiteral ( "Key:" ), QStringLiteral ( "old" ) ), std::nullopt );
	}
};

QTEST_MAIN ( TestTextPrompt )

#include "tst_text_prompt.moc"
