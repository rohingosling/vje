//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FindBar implementation. See FindBar.hpp for why the bar decides nothing and where it sits.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/FindBar.hpp"

#include "AppConfig.hpp"
#include "controllers/FindController.hpp"
#include "services/IconLibrary.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSize>
#include <QToolButton>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FindBar::FindBar ( FindController* controller, IconLibrary* icons, QWidget* parent )
	:	QWidget    ( parent ),
		controller ( controller ),
		icons      ( icons )
	{
		setObjectName ( QStringLiteral ( "findBar" ) );

		// -- The controls, left to right in the order the user works through them.

		queryField = new QLineEdit ( this );
		queryField->setPlaceholderText ( tr ( "Find in document" ) );
		queryField->setClearButtonEnabled ( true );
		queryField->setMinimumWidth ( config::find::QUERY_FIELD_MINIMUM_WIDTH );
		queryField->installEventFilter ( this );

		// NFR-05. The bar carries no label to buddy -- it is a strip of controls, not a form -- so the field's name is
		// stated here. The placeholder is NOT that name: it disappears the moment the user types, which is exactly when
		// a screen reader is asked what this field is.

		queryField->setAccessibleName ( tr ( "Find in document" ) );

		// The CLEAR button is Qt's, built by setClearButtonEnabled above, and it arrives with no text and no accessible
		// name of its own -- so it announces nothing at all. Found by the NFR-05 regression rather than by reading the
		// code, which is the whole reason that test walks the buttons instead of naming the three it knows about.
		//
		// Written defensively on purpose: if a later Qt names the button itself, or stops creating one, findChild
		// returns nothing and this does nothing.

		if ( QToolButton* const clearButton = queryField->findChild<QToolButton*> () )
		{
			clearButton->setAccessibleName ( tr ( "Clear the search text" ) );
		}

		countLabel = new QLabel ( this );
		countLabel->setMinimumWidth ( config::find::COUNT_LABEL_MINIMUM_WIDTH );
		countLabel->setAlignment ( Qt::AlignRight | Qt::AlignVCenter );

		// A readout rather than a control: its TEXT is the answer ("4 of 17 matches"), so the name says what the text
		// is about (NFR-05).

		countLabel->setAccessibleName ( tr ( "Match count" ) );

		auto make_button = [ this ] ( const QString& text, const QString& tip ) -> QToolButton*
		{
			QToolButton* const button = new QToolButton ( this );

			button->setText ( text );
			button->setToolTip ( tip );
			button->setAutoRaise ( true );
			button->setFixedSize ( config::find::BUTTON_SIZE, config::find::BUTTON_SIZE );
			button->setIconSize ( QSize ( config::find::BUTTON_ICON_SIZE, config::find::BUTTON_ICON_SIZE ) );

			// The buttons are a shortcut for what the keyboard already does; they must never become the place the
			// keyboard lands after a query, or Tab out of the field would leave the user pressing Space to find.

			button->setFocusPolicy ( Qt::NoFocus );

			return button;
		};

		// The text is the accessible name and the fallback: QToolButton draws its text when an icon-only button has no
		// icon, which is exactly the no-library case the headless suite runs in.

		previousButton = make_button ( tr ( "Previous" ), tr ( "Previous match (Shift+F3)" ) );
		nextButton     = make_button ( tr ( "Next" ),     tr ( "Next match (F3)" ) );
		closeButton    = make_button ( tr ( "Close" ),    tr ( "Close the find bar (Esc)" ) );

		matchCaseBox = new QCheckBox ( tr ( "Match &Case" ), this );

		// -- Layout. The query field is the only thing that grows; everything else keeps its size so the controls do not
		//    move as the pane is resized or the count changes.

		QHBoxLayout* const barLayout = new QHBoxLayout ( this );

		barLayout->setContentsMargins
		(
			config::find::BAR_MARGIN, config::find::BAR_MARGIN,
			config::find::BAR_MARGIN, config::find::BAR_MARGIN
		);

		barLayout->setSpacing ( config::find::BAR_SPACING );

		barLayout->addWidget ( queryField, 1 );
		barLayout->addWidget ( previousButton );
		barLayout->addWidget ( nextButton );
		barLayout->addWidget ( countLabel );
		barLayout->addWidget ( matchCaseBox );
		barLayout->addWidget ( closeButton );

		// -- Wiring.

		connect ( queryField,   &QLineEdit::textChanged, this, &FindBar::handle_query_edited );
		connect ( matchCaseBox, &QCheckBox::toggled,     this, &FindBar::handle_query_edited );

		// this-> is required, not decoration: inside this constructor the bare name is the PARAMETER, which the lambda
		// has no capture for.

		connect ( previousButton, &QToolButton::clicked, this, [ this ] () { this->controller->find_previous (); } );
		connect ( nextButton,     &QToolButton::clicked, this, [ this ] () { this->controller->find_next (); } );
		connect ( closeButton,    &QToolButton::clicked, this, &FindBar::dismiss );

		connect ( controller, &FindController::results_changed, this, &FindBar::handle_results_changed );

		if ( icons != nullptr )
		{
			connect ( icons, &IconLibrary::icons_changed, this, &FindBar::handle_icons_changed );
		}

		apply_icons ();

		handle_results_changed ();

		hide ();
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void FindBar::open ()
	{
		// Only capture the origin on the way IN. Ctrl+F pressed while the bar is already open would otherwise record the
		// query field itself, making the bar its own way back and stranding the keyboard on a hidden widget.

		if ( isHidden () )
		{
			focusOrigin = QApplication::focusWidget ();

			show ();
		}

		queryField->setFocus ( Qt::ShortcutFocusReason );
		queryField->selectAll ();

		// The bar may have been dismissed and re-opened over a document that has since changed; the label is whatever
		// the controller says now, not what it said when the bar was last hidden.

		handle_results_changed ();
	}

	void FindBar::dismiss ()
	{
		if ( isHidden () )
		{
			return;
		}

		hide ();

		emit dismissed ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QWidget* FindBar::focus_origin () const
	{
		return focusOrigin.data ();
	}

	QLineEdit* FindBar::query_field () const
	{
		return queryField;
	}

	QCheckBox* FindBar::match_case_box () const
	{
		return matchCaseBox;
	}

	QLabel* FindBar::count_label () const
	{
		return countLabel;
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void FindBar::keyPressEvent ( QKeyEvent* event )
	{
		if ( event->key () == Qt::Key_Escape )
		{
			dismiss ();

			event->accept ();

			return;
		}

		QWidget::keyPressEvent ( event );
	}

	bool FindBar::eventFilter ( QObject* watched, QEvent* event )
	{
		if ( ( watched == queryField ) && ( event->type () == QEvent::KeyPress ) )
		{
			QKeyEvent* const keyEvent = static_cast<QKeyEvent*> ( event );

			switch ( keyEvent->key () )
			{
				case Qt::Key_Return:
				case Qt::Key_Enter:
				{
					// FIND-02's Enter, with Shift for the other direction. QLineEdit::returnPressed() reports that
					// Return arrived and not what came with it, which is why this is a filter.

					if ( keyEvent->modifiers ().testFlag ( Qt::ShiftModifier ) )
					{
						controller->find_previous ();
					}
					else
					{
						controller->find_next ();
					}

					return true;
				}

				case Qt::Key_Escape:
				{
					dismiss ();

					return true;
				}

				default:
				{
					break;
				}
			}
		}

		return QWidget::eventFilter ( watched, event );
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void FindBar::handle_query_edited ()
	{
		submit_query ();
	}

	void FindBar::handle_results_changed ()
	{
		countLabel->setText ( controller->report () );

		// Nothing to step through is nothing to press. The buttons say so rather than being live and inert -- the same
		// disabled-not-hidden rule the command surface follows (section 2.3).

		const bool hasMatches = ( controller->match_count () > 0 );

		previousButton->setEnabled ( hasMatches );
		nextButton    ->setEnabled ( hasMatches );
	}

	void FindBar::handle_icons_changed ()
	{
		apply_icons ();
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void FindBar::apply_icons ()
	{
		if ( icons == nullptr )
		{
			return;
		}

		// The move-up / move-down glyphs, reused: an arrow pointing back and forward through a list is the same idea
		// here as on the toolbar, and inventing a second pair would put two shapes on one concept.

		// The buttons stay Qt::ToolButtonIconOnly (the QToolButton default) throughout: Qt draws the TEXT of an icon-only
		// button whose icon is null, so the same setting covers both the shipped case and the no-library one.

		previousButton->setIcon ( icons->icon ( icon_names::NODE_MOVE_UP ) );
		nextButton    ->setIcon ( icons->icon ( icon_names::NODE_MOVE_DOWN ) );
		closeButton   ->setIcon ( icons->icon ( icon_names::DOCUMENT_CLOSE ) );

	}

	void FindBar::submit_query ()
	{
		controller->set_query ( queryField->text (), matchCaseBox->isChecked () );
	}
}
