//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   MessageBox implementation. See the header for why this is a subclass and why its structure is applied at show
//   time; the measurements each step relies on are cited where the step is taken.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "dialogs/MessageBox.hpp"

#include "AppConfig.hpp"
#include "dialogs/dialog_frame.hpp"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QSpacerItem>

#include <algorithm>

namespace vje
{
	//=================================================================================================================
	// Free Functions -- the rules
	//=================================================================================================================

	QMessageBox::Icon message_box_icon ( MessageKind kind )
	{
		switch ( kind )
		{
			case MessageKind::Information: return QMessageBox::Information;
			case MessageKind::Error:       return QMessageBox::Critical;
			case MessageKind::Warning:     return QMessageBox::Warning;
		}

		return QMessageBox::NoIcon;
	}

	QString standard_button_caption ( QMessageBox::StandardButton button )
	{
		// The mnemonics are Windows' own for these words, so Alt+S, Alt+N, Alt+Y answer the box the way they answer a
		// Windows one. OK and Cancel carry none: Enter and Esc already reach them.

		switch ( button )
		{
			case QMessageBox::Ok:      return QCoreApplication::translate ( "MessageBox", "OK" );
			case QMessageBox::Cancel:  return QCoreApplication::translate ( "MessageBox", "Cancel" );
			case QMessageBox::Yes:     return QCoreApplication::translate ( "MessageBox", "&Yes" );
			case QMessageBox::No:      return QCoreApplication::translate ( "MessageBox", "&No" );
			case QMessageBox::Save:    return QCoreApplication::translate ( "MessageBox", "&Save" );
			case QMessageBox::Discard: return QCoreApplication::translate ( "MessageBox", "Do&n't Save" );
			case QMessageBox::Close:   return QCoreApplication::translate ( "MessageBox", "Close" );

			default: break;
		}

		return QString ();
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	MessageBox::MessageBox
	(
		MessageKind                  kind,
		const QString&               title,
		const QString&               text,
		QMessageBox::StandardButtons buttons,
		QMessageBox::StandardButton  defaultButton,
		QMessageBox::StandardButton  escapeButton,
		QWidget*                     parent,
		HorizontalTextAlignment      horizontal,
		VerticalTextAlignment        vertical
	)
	:	QMessageBox         ( message_box_icon ( kind ), title, text, buttons, parent )
	,	horizontalAlignment ( horizontal )
	,	verticalAlignment   ( vertical )
	{
		setDefaultButton ( defaultButton );
		setEscapeButton  ( escapeButton );

		// The captions are set once, here, rather than with the rest of the structure at show time: every box VJE
		// builds has its buttons from construction, and a caller re-captioning one (the Code View's Discard) must not
		// have its caption put back when the box is shown. Measured: a style change while the box is open leaves them
		// as set (tst_message_box).

		for ( QAbstractButton* const button : this->buttons () )
		{
			const QString caption = standard_button_caption ( standardButton ( button ) );

			if ( !caption.isEmpty () )
			{
				button->setText ( caption );
			}
		}
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void MessageBox::set_text_alignment ( HorizontalTextAlignment horizontal, VerticalTextAlignment vertical )
	{
		horizontalAlignment = horizontal;
		verticalAlignment   = vertical;

		// Before the box is shown there is nothing to do: showEvent structures the grid, alignment included. On an open
		// box the grid is already structured, so only the alignment is re-applied -- apply_structure would find the grid
		// structured and do nothing -- and the layout re-run.

		if ( isVisible () && ( structuredLayout == layout () ) )
		{
			apply_alignment ();

			layout ()->invalidate ();
			layout ()->activate   ();
		}
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	QMessageBox::StandardButton MessageBox::ask ()
	{
		// The same reading of the answer as Qt's static functions, deliberately (see ask_message_box in the header): a
		// box closed with no button chosen answers Cancel, and one accepted without a button -- as tst_main_window's
		// script does -- answers NoButton, which every caller already treats as "not Yes".

		if ( exec () == -1 )
		{
			return QMessageBox::Cancel;
		}

		return standardButton ( clickedButton () );
	}

	//=================================================================================================================
	// Event Handlers
	//=================================================================================================================

	void MessageBox::showEvent ( QShowEvent* event )
	{
		// BEFORE the base class, because QMessageBox::showEvent is where the box measures its layout and fixes its own
		// size: the floor has to be in the layout by then.

		apply_structure ();

		QMessageBox::showEvent ( event );
	}

	void MessageBox::changeEvent ( QEvent* event )
	{
		QMessageBox::changeEvent ( event );

		// A STYLE CHANGE REBUILDS THE GRID UNDER AN OPEN BOX. Measured (Qt 6.10.1, 2026-09-26): QEvent::StyleChange
		// replaced the grid, and the open box shrank from 300 x 150 back to its natural 166 x 100; a palette change did
		// not. ThemeService::apply() installs a new style object every time, so an OS light/dark switch under the
		// System theme reaches this with a box open.
		//
		// Re-setting the text is how the size is recomputed: it is Qt's one public route back into the calculation for
		// a visible box, and measured as returning it to the floor.
		//
		// It is a REPAIR, and it runs on any change event while the grid is not the structured one -- including the
		// activation change every shown window receives. So it would also rescue a floor that showEvent failed to
		// apply, one visible resize late; showEvent is still where the floor belongs, and tst_message_box reads the
		// size the moment show() returns to tell the two apart (lesson D49).

		if ( isVisible () && ( structuredLayout != layout () ) )
		{
			apply_structure ();

			setText ( text () );
		}
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void MessageBox::apply_structure ()
	{
		QGridLayout* const grid = qobject_cast<QGridLayout*> ( layout () );

		if ( ( grid == nullptr ) || ( structuredLayout == grid ) )
		{
			return;
		}

		structuredLayout = grid;

		// STYLE-15's inset, so a message box's buttons line up with the framed dialogs'. Qt's own was 11 (measured).

		const int margin = config::dialog::CONTENT_MARGIN;

		grid->setContentsMargins ( margin, margin, margin, margin );

		// The rule above the buttons, as every framed dialog has (STYLE-15, extended to message boxes 2026-09-26). Qt's
		// grid leaves no gap between the text rows and the button row -- measured, the text rows end at y = 116 and the
		// button box starts at 117 -- so the whole of the padding above the rule is the rule's own.

		if ( QDialogButtonBox* const buttons = findChild<QDialogButtonBox*> () )
		{
			apply_button_rule ( *this, *buttons, 0 );
		}

		apply_alignment ();

		// THE FLOOR IS A SPACER IN QT'S OWN GRID, because setMinimumSize cannot work: QMessageBox computes its layout's
		// minimum when shown and then calls setFixedSize, overwriting any minimum set on the widget (measured). What it
		// computes is the layout, so the floor goes into the layout -- one spacer of the floor's inner size, spanning
		// every row and column. It overlaps everything and displaces nothing; it only raises the grid's minimum, so a
		// longer message still grows the box past it (measured: 166 x 100 became 300 x 150, and 500 x 100 became
		// 500 x 150).

		const int innerWidth  = std::max ( 0, config::message_box::MINIMUM_WIDTH  - ( 2 * margin ) );
		const int innerHeight = std::max ( 0, config::message_box::MINIMUM_HEIGHT - ( 2 * margin ) );

		grid->addItem
		(
			new QSpacerItem ( innerWidth, innerHeight, QSizePolicy::Minimum, QSizePolicy::Minimum ),
			0,
			0,
			grid->rowCount (),
			grid->columnCount ()
		);
	}

	void MessageBox::apply_alignment ()
	{
		QGridLayout* const grid = qobject_cast<QGridLayout*> ( layout () );

		if ( grid == nullptr )
		{
			return;
		}

		// Find the parts by their place in Qt's grid rather than by name: the icon is the label in column 0, the text is
		// every label to its right (the message, and the informative text below it), and the buttons are the button
		// box's row. So a Qt release that arranges the rows differently still aligns what it arranges.

		QLabel*        iconLabel     = nullptr;
		QList<QLabel*> textLabels;
		int            firstTextRow  = -1;
		int            lastTextRow   = -1;
		int            buttonRow     = grid->rowCount () - 1;

		for ( int i = 0; i < grid->count (); ++i )
		{
			int row        = 0;
			int column     = 0;
			int rowSpan    = 0;
			int columnSpan = 0;

			grid->getItemPosition ( i, &row, &column, &rowSpan, &columnSpan );

			QWidget* const widget = grid->itemAt ( i )->widget ();

			if ( qobject_cast<QDialogButtonBox*> ( widget ) != nullptr )
			{
				buttonRow = row;
			}
			else if ( QLabel* const label = qobject_cast<QLabel*> ( widget ); label != nullptr )
			{
				if ( column == 0 )
				{
					iconLabel = label;
				}
				else if ( !label->isHidden () )
				{
					textLabels.append ( label );

					firstTextRow = ( firstTextRow < 0 ) ? row : std::min ( firstTextRow, row );
					lastTextRow  = std::max ( lastTextRow, row );
				}
			}
		}

		// The rows that HOLD text, counted from the labels actually there. Not "every row above the buttons": on a box with
		// no informative text the row beneath the message is empty, and treating it as the second half of the block
		// centred the message on the boundary between the two, half a line too high (tst_message_box, measured).

		const int rowAboveButtons = buttonRow - 1;

		if ( ( firstTextRow < 0 ) || ( rowAboveButtons < lastTextRow ) )
		{
			return;
		}

		// WHERE THE FLOOR'S EXTRA HEIGHT GOES decides where the text sits. Left to itself the grid split it evenly
		// between the message row and the row beneath it, and a QLabel centres vertically -- measured, a one-line message
		// floated a third of the way down, and a message with informative text opened a gap between its two sentences.
		// So the text is moved as one block, by giving the extra height to the rows on the far side of it:
		//
		//   Top     -- the row above the buttons takes it all, and every label hugs the top of its row.
		//   Bottom  -- the first text row takes it all; the message hugs the BOTTOM of its row, so it sits on the
		//              informative text (or, with none, on the buttons), and the informative text hugs the top of its row.
		//   Centre  -- the first and last text rows take half each, with the message and the informative text hugging
		//              each other as for Bottom, so the pair is centred as a block. With one text row only, it takes the
		//              extra height and its label centres itself.

		for ( int row = 0; row < grid->rowCount (); ++row )
		{
			grid->setRowStretch ( row, 0 );
		}

		const bool oneTextRow = ( firstTextRow == lastTextRow );

		Qt::Alignment messageVertical = Qt::AlignTop;
		Qt::Alignment restVertical    = Qt::AlignTop;

		switch ( verticalAlignment )
		{
			case VerticalTextAlignment::Top:
			{
				grid->setRowStretch ( rowAboveButtons, 1 );

				break;
			}

			case VerticalTextAlignment::Bottom:
			{
				grid->setRowStretch ( firstTextRow, 1 );

				messageVertical = Qt::AlignBottom;

				break;
			}

			case VerticalTextAlignment::Centre:
			{
				grid->setRowStretch ( firstTextRow, 1 );
				grid->setRowStretch ( lastTextRow,  1 );

				messageVertical = oneTextRow ? Qt::AlignVCenter : Qt::AlignBottom;

				break;
			}
		}

		Qt::Alignment horizontal = Qt::AlignLeft;

		switch ( horizontalAlignment )
		{
			case HorizontalTextAlignment::Left:   horizontal = Qt::AlignLeft;    break;
			case HorizontalTextAlignment::Centre: horizontal = Qt::AlignHCenter; break;
			case HorizontalTextAlignment::Right:  horizontal = Qt::AlignRight;   break;
		}

		for ( QLabel* const label : textLabels )
		{
			int row        = 0;
			int column     = 0;
			int rowSpan    = 0;
			int columnSpan = 0;

			grid->getItemPosition ( grid->indexOf ( label ), &row, &column, &rowSpan, &columnSpan );

			label->setAlignment ( horizontal | ( ( row == firstTextRow ) ? messageVertical : restVertical ) );
		}

		// THE ICON STAYS TOP-LEFT, whatever the text does: it is the box's anchor, where the eye lands first, and it does not
		// travel with the message (revised 2026-09-26 -- it first followed the vertical choice). Stated rather than left to
		// Qt's placement, so the rule holds whatever a Qt release adds the icon with.

		if ( iconLabel != nullptr )
		{
			grid->setAlignment ( iconLabel, Qt::AlignLeft | Qt::AlignTop );
		}
	}

	//=================================================================================================================
	// Free Functions -- the two shapes every call site uses
	//=================================================================================================================

	void show_message_box
	(
		QWidget*                parent,
		MessageKind             kind,
		const QString&          title,
		const QString&          text,
		HorizontalTextAlignment horizontal,
		VerticalTextAlignment   vertical
	)
	{
		MessageBox box ( kind, title, text, QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok, parent, horizontal, vertical );

		box.exec ();
	}

	QMessageBox::StandardButton ask_message_box
	(
		QWidget*                     parent,
		MessageKind                  kind,
		const QString&               title,
		const QString&               text,
		QMessageBox::StandardButtons buttons,
		QMessageBox::StandardButton  defaultButton,
		QMessageBox::StandardButton  escapeButton,
		HorizontalTextAlignment      horizontal,
		VerticalTextAlignment        vertical
	)
	{
		MessageBox box ( kind, title, text, buttons, defaultButton, escapeButton, parent, horizontal, vertical );

		return box.ask ();
	}
}
