//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonCellDelegate implementation -- editor creation per value kind, the at-rest presentation, and the in-editor
//   keyboard model. See the header for the two form/table divergences it carries.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/JsonCellDelegate.hpp"

#include "models/cell_presentation.hpp"
#include "views/JsonKeyValidator.hpp"
#include "views/JsonNumberValidator.hpp"

#include <vje_core/document/JsonNode.hpp>

#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonCellDelegate::JsonCellDelegate ( bool arrowKeysNavigate, QObject* parent )
		: QStyledItemDelegate ( parent )
		, arrowKeysNavigate ( arrowKeysNavigate )
	{
	}

	//=================================================================================================================
	// QStyledItemDelegate -- editors
	//=================================================================================================================

	QWidget* JsonCellDelegate::createEditor
	(
		QWidget*                    parent,
		const QStyleOptionViewItem& option,
		const QModelIndex&          index
	) const
	{
		Q_UNUSED ( option );

		// The model answers what KIND of content the cell holds; the delegate never needs to know which model it is.

		// A member KEY, renamed in place (EDIT-02). Plain text with one rule attached: it may not be committed onto a
		// key the object already has (VAL-02) -- expressed as a validator so the refusal reuses the same
		// keep-the-caret-in-the-cell path a rejected number takes.

		if ( index.data ( cell_roles::IS_KEY_CELL ).toBool () )
		{
			QLineEdit* const keyEditor = new QLineEdit ( parent );

			keyEditor->setFrame ( false );
			keyEditor->setValidator
			(
				new JsonKeyValidator ( index.data ( cell_roles::RIVAL_KEYS ).toStringList (), keyEditor )
			);

			return keyEditor;
		}

		const auto content = static_cast<CellContent> ( index.data ( cell_roles::CONTENT_KIND ).toInt () );

		if ( content != CellContent::Scalar )
		{
			// Containers drill in and placeholders are read-only, so neither opens an editor. Qt would not ask for one
			// (the models withhold ItemIsEditable), but answering null keeps the rule stated in one place.

			return nullptr;
		}

		const auto valueKind = static_cast<JsonKind> ( index.data ( cell_roles::VALUE_KIND ).toInt () );

		if ( valueKind == JsonKind::Boolean )
		{
			// A closed two-item combo, not a free-text field: a boolean has exactly two legal values, so offering
			// anything else to type would only create input to reject (EDITOR-02).

			QComboBox* booleanEditor = new QComboBox ( parent );

			booleanEditor->addItem ( cell_text::BOOLEAN_TRUE );
			booleanEditor->addItem ( cell_text::BOOLEAN_FALSE );

			return booleanEditor;
		}

		QLineEdit* textEditor = new QLineEdit ( parent );

		textEditor->setFrame ( false );

		if ( valueKind == JsonKind::Number )
		{
			// VAL-03. The validator is what makes an invalid commit impossible rather than merely refused -- see
			// JsonNumberValidator's header.

			textEditor->setValidator ( new JsonNumberValidator ( textEditor ) );
		}

		return textEditor;
	}

	void JsonCellDelegate::setEditorData ( QWidget* editor, const QModelIndex& index ) const
	{
		const QString value = index.data ( Qt::EditRole ).toString ();

		if ( QComboBox* const booleanEditor = qobject_cast<QComboBox*> ( editor ) )
		{
			const int itemIndex = booleanEditor->findText ( value );

			booleanEditor->setCurrentIndex ( ( itemIndex >= 0 ) ? itemIndex : 0 );

			return;
		}

		if ( QLineEdit* const textEditor = qobject_cast<QLineEdit*> ( editor ) )
		{
			textEditor->setText ( value );

			// Select the whole content, which is what makes "typing replaces the value" true (EDITOR-02 / 03): Qt
			// forwards the activating keystroke into the editor, and it lands on a full selection rather than at the
			// end of the existing text. F2 opens the same way, so a deliberate edit starts by replacing too -- standard
			// spreadsheet behaviour, and the arrow keys deselect for anyone who wanted to amend instead.

			textEditor->selectAll ();

			return;
		}

		QStyledItemDelegate::setEditorData ( editor, index );
	}

	void JsonCellDelegate::setModelData ( QWidget* editor, QAbstractItemModel* model, const QModelIndex& index ) const
	{
		if ( QComboBox* const booleanEditor = qobject_cast<QComboBox*> ( editor ) )
		{
			model->setData ( index, booleanEditor->currentText (), Qt::EditRole );

			return;
		}

		if ( QLineEdit* const textEditor = qobject_cast<QLineEdit*> ( editor ) )
		{
			// The guard that catches the paths Qt commits on its own -- notably focus loss, which does not run through
			// this delegate's key handling. An unacceptable value is simply not written; the cell keeps what it had.

			if ( !textEditor->hasAcceptableInput () )
			{
				return;
			}

			model->setData ( index, textEditor->text (), Qt::EditRole );

			return;
		}

		QStyledItemDelegate::setModelData ( editor, model, index );
	}

	//=================================================================================================================
	// QStyledItemDelegate -- presentation
	//=================================================================================================================

	void JsonCellDelegate::initStyleOption ( QStyleOptionViewItem* option, const QModelIndex& index ) const
	{
		QStyledItemDelegate::initStyleOption ( option, index );

		const auto content = static_cast<CellContent> ( index.data ( cell_roles::CONTENT_KIND ).toInt () );

		switch ( content )
		{
			case CellContent::Container:
			{
				// The drill-in colour (EDITOR-02), taken from the palette so it follows the theme rather than being
				// pinned to a literal.

				option->palette.setColor ( QPalette::Text, option->palette.color ( QPalette::Link ) );

				break;
			}

			case CellContent::Null:
			case CellContent::Missing:
			{
				option->palette.setColor ( QPalette::Text, option->palette.color ( QPalette::Disabled, QPalette::Text ) );

				// Italic as well as dimmed. The dim alone disappears the moment the cell is selected and the highlight
				// brush takes over the text colour -- which is exactly the cell the user is standing on, and exactly
				// when knowing it is a placeholder matters most.

				option->font.setItalic ( true );

				break;
			}

			case CellContent::Scalar:
			{
				break;
			}
		}
	}

	//=================================================================================================================
	// Keyboard model
	//=================================================================================================================

	bool JsonCellDelegate::eventFilter ( QObject* watched, QEvent* event )
	{
		QWidget* const editor = qobject_cast<QWidget*> ( watched );

		if ( ( editor == nullptr ) || ( event->type () != QEvent::KeyPress ) )
		{
			return QStyledItemDelegate::eventFilter ( watched, event );
		}

		const QKeyEvent* const keyEvent = static_cast<QKeyEvent*> ( event );

		GridMove move = GridMove::Down;

		if ( !movement_for_key ( editor, keyEvent->key (), keyEvent->modifiers (), move ) )
		{
			return QStyledItemDelegate::eventFilter ( watched, event );
		}

		// Swallowed either way. A refused commit (VAL-03) must NOT fall through to the view, or the highlight would
		// move off a cell whose editor is still open on invalid text.

		commit_and_move ( editor, move );

		return true;
	}

	bool JsonCellDelegate::movement_for_key
	(
		const QWidget*        editor,
		int                   key,
		Qt::KeyboardModifiers modifiers,
		GridMove&             outMove
	) const
	{
		// Anything with Ctrl or Alt is a command, not a movement, and belongs to the window.

		if ( modifiers.testFlag ( Qt::ControlModifier ) || modifiers.testFlag ( Qt::AltModifier ) )
		{
			return false;
		}

		const bool isBooleanEditor = ( qobject_cast<const QComboBox*> ( editor ) != nullptr );

		switch ( key )
		{
			case Qt::Key_Return:
			case Qt::Key_Enter:
			{
				// Commit and advance; Shift reverses it (EDITOR-02 / 03).

				outMove = modifiers.testFlag ( Qt::ShiftModifier ) ? GridMove::Up : GridMove::Down;

				return true;
			}

			case Qt::Key_Tab:
			{
				outMove = GridMove::NextCell;

				return true;
			}

			case Qt::Key_Backtab:
			{
				outMove = GridMove::PreviousCell;

				return true;
			}

			case Qt::Key_Up:
			case Qt::Key_Down:
			{
				// A combo owns these to change its value; taking them would leave a boolean unreachable by keyboard.

				if ( isBooleanEditor )
				{
					return false;
				}

				outMove = ( key == Qt::Key_Up ) ? GridMove::Up : GridMove::Down;

				return true;
			}

			case Qt::Key_Left:
			case Qt::Key_Right:
			{
				// The form/table divergence, stated by the spec in these terms: table cells are short and move like a
				// spreadsheet; form values are long, so Left / Right stay caret keys there (EDITOR-02).

				if ( !arrowKeysNavigate || isBooleanEditor )
				{
					return false;
				}

				outMove = ( key == Qt::Key_Left ) ? GridMove::Left : GridMove::Right;

				return true;
			}

			default:
			{
				return false;
			}
		}
	}

	bool JsonCellDelegate::commit_and_move ( QWidget* editor, GridMove move )
	{
		const QLineEdit* const textEditor = qobject_cast<const QLineEdit*> ( editor );

		if ( ( textEditor != nullptr ) && !textEditor->hasAcceptableInput () )
		{
			// EDITOR-03: a rejected commit keeps the caret in the errored cell. Nothing is written and nothing moves.
			//
			// Said out loud, because silence here is indistinguishable from a dead Enter key: the number case is
			// self-explanatory (the character never appeared), but a duplicate key looks perfectly typed and the
			// refusal needs a reason attached to it.

			// Which rule refused it is answerable from the validator in play, which is the only thing that knows.
			// A half-typed number explains itself on screen; a duplicate key does not, and reads as a dead Enter key.

			const bool isKeyEditor = ( qobject_cast<const JsonKeyValidator*> ( textEditor->validator () ) != nullptr );

			emit commit_refused
			(
				isKeyEditor ? tr ( "That key already exists in this object." )
				            : tr ( "That value is not a valid JSON number." )
			);

			return false;
		}

		emit commitData  ( editor );
		emit closeEditor ( editor, QAbstractItemDelegate::NoHint );

		// Announced after the commit, so the grid controller moves the current cell over a model that already holds the
		// new value -- which matters when the commit reshaped the projection.

		emit editing_moved ( move );

		return true;
	}
}
