//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include "AppConfig.hpp"

#include "models/cell_presentation.hpp"
#include "views/JsonKeyValidator.hpp"
#include "views/JsonEscapeValidator.hpp"
#include "views/JsonNumberValidator.hpp"

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/json_escapes.hpp>

#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPalette>
#include <QTextDocument>

#include <algorithm>

namespace vje
{
	namespace
	{
		// Set on a multi-line string editor at creation, so the commit path can ask the editor what notation it was
		// opened in without a second reading of the model or the setting.

		const char* const ESCAPED_NOTATION_PROPERTY = "vjeEscapedNotation";
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonCellDelegate::JsonCellDelegate ( QObject* parent )
		: QStyledItemDelegate ( parent )
	{
	}

	//=================================================================================================================
	// QStyledItemDelegate -- editors
	//=================================================================================================================

	void JsonCellDelegate::set_wrap_strings ( bool wrap )
	{
		wrapStrings = wrap;
	}

	QWidget* JsonCellDelegate::createEditor
	(
		QWidget*                    parent,
		const QStyleOptionViewItem& option,
		const QModelIndex&          index
	) const
	{
		Q_UNUSED ( option );

		QWidget* const editor = create_editor_widget ( parent, index );

		// NFR-05, applied HERE rather than in each branch below, and that placement is the point: an editor is a bare
		// QLineEdit / QComboBox / QPlainTextEdit parented into a viewport, so it inherits no name from the cell it
		// covers and announces nothing at all. There are five kinds today and naming each one would be five places to
		// forget; naming the RESULT means a sixth kind cannot arrive anonymous.
		//
		// The name comes from the model (cell_roles::CELL_LABEL) for the reason every other question here does: the
		// delegate drives two grids and must not learn which. It is the label the user can SEE -- the row's key in the
		// object form, the column's key in the array table -- so what is announced is what is on screen.

		if ( editor != nullptr )
		{
			editor->setAccessibleName ( index.data ( cell_roles::CELL_LABEL ).toString () );
		}

		return editor;
	}

	QWidget* JsonCellDelegate::create_editor_widget ( QWidget* parent, const QModelIndex& index ) const
	{
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

		// A null / missing / provisional cell takes a TYPED ENTRY (EDITOR-12): a plain text box, opened empty, whose
		// text the model interprets as a JSON literal. No validator -- any text is a legal literal (a bare word becomes
		// a string), so nothing is refused at the keystroke; the model does the interpretation on commit.

		if ( ( content == CellContent::Null ) || ( content == CellContent::Missing ) )
		{
			QLineEdit* const literalEditor = new QLineEdit ( parent );

			literalEditor->setFrame ( false );

			return literalEditor;
		}

		if ( content != CellContent::Scalar )
		{
			// A container drills in rather than editing, so it opens no editor. Qt would not ask for one (the model
			// withholds ItemIsEditable), but answering null keeps the rule stated in one place.

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

		// A STRING edits in a multi-line box for either of two reasons, and the second was missing until the 2026-07-28
		// review: because the ROW wraps (a value shown over four lines and edited in a one-line field is the same value
		// twice in two shapes, SET-05), or because the VALUE ITSELF carries a line break and a single-line box could
		// neither show nor navigate it.
		//
		// The second test is just "does the edit text contain a newline", and that is exact rather than approximate: in
		// Escaped notation a line break IS the two characters backslash-n, so the test is false there and a short value
		// keeps its QLineEdit; only Decoded puts a real newline in front of the editor (SET-03). Keying on wrapStrings
		// alone left a Decoded multi-line value uneditable the moment wrapping was switched off.
		//
		// Scoped to strings deliberately: a number keeps its QLineEdit and therefore keeps JsonNumberValidator, which is
		// what buys VAL-03's keep-the-caret-in-the-cell behaviour from Qt's own plumbing -- QPlainTextEdit has no
		// validator to give it.

		const bool valueCarriesLineBreak = index.data ( Qt::EditRole ).toString ().contains ( QChar::LineFeed );

		if ( ( valueKind == JsonKind::String ) && ( wrapStrings || valueCarriesLineBreak ) )
		{
			QPlainTextEdit* const wrappedEditor = new QPlainTextEdit ( parent );

			wrappedEditor->setFrameShape ( QFrame::NoFrame );
			wrappedEditor->setLineWrapMode ( QPlainTextEdit::WidgetWidth );
			wrappedEditor->setTabChangesFocus ( false );          // Tab is the grid's (EDITOR-03), not the editor's.

			// NEITHER scroll bar. The row is sized to hold the whole wrapped value, so a scroll bar would only ever
			// appear to say the row got its own height wrong -- and a vertical one would narrow the text area, wrap the
			// text further, and need itself all over again.

			wrappedEditor->setHorizontalScrollBarPolicy ( Qt::ScrollBarAlwaysOff );
			wrappedEditor->setVerticalScrollBarPolicy ( Qt::ScrollBarAlwaysOff );

			// QPlainTextEdit inserts its own 4 px document margin. Left in, the editor's text sits a few pixels off from
			// where the same value was painted at rest, and it wraps at a slightly narrower width than the row was
			// measured against -- which is exactly how a last line ends up hidden.

			wrappedEditor->document ()->setDocumentMargin ( 0 );

			// Carried ON the editor rather than in a member: commit_and_move has no QModelIndex to ask, and a second
			// copy of the answer is a second copy that can disagree with the model's.

			wrappedEditor->setProperty ( ESCAPED_NOTATION_PROPERTY, index.data ( cell_roles::ESCAPED_NOTATION ).toBool () );

			return wrappedEditor;
		}

		QLineEdit* textEditor = new QLineEdit ( parent );

		textEditor->setFrame ( false );

		if ( valueKind == JsonKind::Number )
		{
			// VAL-03. The validator is what makes an invalid commit impossible rather than merely refused -- see
			// JsonNumberValidator's header.

			textEditor->setValidator ( new JsonNumberValidator ( textEditor ) );
		}
		else if ( ( valueKind == JsonKind::String ) && index.data ( cell_roles::ESCAPED_NOTATION ).toBool () )
		{
			// SET-03: while the notation is escaped the text has to decode, and a malformed escape refuses the COMMIT
			// rather than the keystroke -- see JsonEscapeValidator's header for why it never answers Invalid.

			textEditor->setValidator ( new JsonEscapeValidator ( textEditor ) );
		}

		return textEditor;
	}

	QSize JsonCellDelegate::sizeHint ( const QStyleOptionViewItem& option, const QModelIndex& index ) const
	{
		const QSize measured = QStyledItemDelegate::sizeHint ( option, index );

		if ( !wrapStrings )
		{
			return measured;
		}

		// The base measurement is used for the LINE COUNT only, and the height is then rebuilt from the same formula an
		// unwrapped row is sized by -- lines x line height, plus the grid's own vertical padding.
		//
		// Taking the measured height directly was the first version and it left a wrapped block sitting visibly tighter
		// against the field below it than any two single-line fields sit against each other. The cause is that the two
		// heights come from different places: an unwrapped row is font height + config::form::ROW_VERTICAL_PADDING,
		// while the style's own measurement carries only its focus-frame margin, which is a pixel or two smaller. One
		// pixel is invisible on its own and obvious in a column of fields where every other gap is the other value.
		//
		// Divided AND multiplied by the same metric, which the first version was not: it divided by lineSpacing and
		// multiplied by height. QTextLayout does not add leading, so the measured height is lines x height -- dividing
		// that by lineSpacing (= height + leading) under-counts on any font with leading, and the row is rebuilt a line
		// short, clipping the tail of the value. Invisible on a zero-leading font, which is why it survived until the
		// 2026-07-28 review. The style's own small margin is what keeps the division from ever coming out LOW.

		const int lineHeight = std::max ( 1, option.fontMetrics.height () );

		// UNCAPPED. A row holds however many lines its value needs, at whatever width the column currently is, so the
		// whole value is always visible and no scroll bar ever has to appear inside a cell.
		//
		// A cap was tried and removed: it was there to stop one pathological value -- a base64 blob, a minified
		// document stored as a string -- producing a row taller than the viewport, on the reasoning that the scroll bar
		// would then move in units of one enormous row. The right answer to that turned out to be per-PIXEL vertical
		// scrolling (FormView::create_grid_view), which makes a tall row ordinary to scroll through, rather than hiding
		// the end of the user's value.

		const int lines = std::max ( 1, measured.height () / lineHeight );

		return QSize ( measured.width (), ( lines * lineHeight ) + config::form::ROW_VERTICAL_PADDING );
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

		if ( QPlainTextEdit* const wrappedEditor = qobject_cast<QPlainTextEdit*> ( editor ) )
		{
			wrappedEditor->setPlainText ( value );

			// The same selectAll the single-line branch does, and for the same reason: FormGridController makes typing
			// an edit trigger, Qt forwards the activating keystroke into the editor, and without a selection it lands
			// at offset 0 and PREPENDS. Missing here until the 2026-07-28 review, which meant switching Wrap strings on
			// silently changed what typing into a field did (EDITOR-02 / 03).

			wrappedEditor->selectAll ();

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

		// While wrapping, a row is as tall as its TALLEST cell, so every other cell in it has room to float. Top rather
		// than vertically centred, for both columns: a key must sit level with the first line of the value it names
		// (spec section 2.10), and a short value must not hang in the middle of a taller neighbour's paragraph.
		//
		// Only while wrapping. With one-line rows the two alignments differ by a couple of pixels of padding, and the
		// centred one is what every other grid in the application uses.

		if ( wrapStrings )
		{
			option->displayAlignment = Qt::AlignLeft | Qt::AlignTop;
		}

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

		// Ctrl+Enter is the one Ctrl combination the editor wants rather than the window (spec section 4). It is
		// handled here rather than left to QPlainTextEdit, which answers only the unmodified Enter -- so without this
		// the key does nothing at all and a multi-line value cannot be given a line break.

		const bool isLineBreak = ( ( keyEvent->key () == Qt::Key_Return ) || ( keyEvent->key () == Qt::Key_Enter ) )
		                      && keyEvent->modifiers ().testFlag ( Qt::ControlModifier );

		if ( isLineBreak )
		{
			QPlainTextEdit* const wrappedEditor = qobject_cast<QPlainTextEdit*> ( editor );

			if ( wrappedEditor != nullptr )
			{
				wrappedEditor->insertPlainText ( QString ( QLatin1Char ( '\n' ) ) );

				return true;
			}
		}

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
		// Ctrl+Up / Ctrl+Down are the ONE movement pair Ctrl carries: they commit and leave for the field above or
		// below, from EVERY editor kind. Everything else holding Ctrl or Alt is a command and belongs to the window
		// (Ctrl+S, Ctrl+Z, Ctrl+C ...), which is why the bail below it stays otherwise unconditional.
		//
		// They are free to take, which is the whole reason they were chosen: a stock QPlainTextEdit does nothing at all
		// with them -- no caret move, no scroll -- so unlike Ctrl+Left / Ctrl+Right (word-jump) nothing is given up.
		// The pair replaced Shift+Up / Shift+Down (2026-07-28), which were NOT free: they are how every text box on
		// either platform selects a line at a time, and taking them left a multi-line value with no vertical selection
		// from the keyboard at all.
		//
		// Requiring EXACTLY Ctrl is deliberate. Ctrl+Shift+Up is left to the editor rather than quietly treated as a
		// leave, so the combination means one thing and only one.

		const bool isVerticalArrow = ( key == Qt::Key_Up ) || ( key == Qt::Key_Down );

		if ( ( modifiers == Qt::ControlModifier ) && isVerticalArrow )
		{
			outMove = ( key == Qt::Key_Up ) ? GridMove::Up : GridMove::Down;

			return true;
		}

		if ( modifiers.testFlag ( Qt::ControlModifier ) || modifiers.testFlag ( Qt::AltModifier ) )
		{
			return false;
		}

		const bool isBooleanEditor   = ( qobject_cast<const QComboBox*>      ( editor ) != nullptr );
		const bool isMultiLineEditor = ( qobject_cast<const QPlainTextEdit*> ( editor ) != nullptr );

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

				// A MULTI-LINE string editor owns them too, and for the same reason: a value shown over six lines has
				// to be navigable line by line, and a grid that moved to the next field on the first Down would make
				// the second line of a paragraph unreachable by keyboard (SET-05).
				//
				// It owns the SHIFTED pair as well, which is the correction (2026-07-28): Shift+Up / Shift+Down extend
				// the selection a line at a time in QPlainTextEdit, as they do in every other text box, and this
				// briefly took them for grid movement. Ctrl+Up / Ctrl+Down is the escape now, handled above, and it is
				// the better key precisely because the editor does nothing with it.

				if ( isMultiLineEditor )
				{
					return false;
				}

				outMove = ( key == Qt::Key_Up ) ? GridMove::Up : GridMove::Down;

				return true;
			}

			case Qt::Key_Left:
			case Qt::Key_Right:
			{
				// NEVER taken, in either grid. While an editor is open the horizontal arrows belong to the TEXT: they
				// clear the selection the editor opened with and walk the caret through it, which is what every other
				// text box in the application does and what makes a mistyped character in the middle of a value
				// reachable at all.
				//
				// The array table used to take them as spreadsheet navigation -- commit and move one cell -- so the
				// only way back to a typo was to retype the whole value or Esc and start over (2026-07-28). The case is
				// kept, rather than folded into the default, because this is exactly where someone would put the
				// spreadsheet reading back.
				//
				// Vertical is a different question and keeps its own answer above: Up / Down DO commit and move,
				// because a single-line editor has nowhere for them to go.

				return false;
			}

			default:
			{
				return false;
			}
		}
	}

	bool JsonCellDelegate::commit_and_move ( QWidget* editor, GridMove move )
	{
		// The multi-line string editor carries no QValidator -- QPlainTextEdit has nowhere to put one -- so its one
		// rule is checked here instead. Same outcome as a QLineEdit's: nothing written, nothing moved, caret held.

		const QPlainTextEdit* const wrappedEditor = qobject_cast<const QPlainTextEdit*> ( editor );

		if ( ( wrappedEditor != nullptr ) && wrappedEditor->property ( ESCAPED_NOTATION_PROPERTY ).toBool () )
		{
			QString decoded;

			if ( !json_escapes::decode ( wrappedEditor->toPlainText (), decoded ) )
			{
				emit commit_refused ( JsonEscapeValidator::refusal_reason () );

				return false;
			}
		}

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

			const QValidator* const validator = textEditor->validator ();

			if ( qobject_cast<const JsonKeyValidator*> ( validator ) != nullptr )
			{
				emit commit_refused ( tr ( "That key already exists in this object." ) );
			}
			else if ( qobject_cast<const JsonEscapeValidator*> ( validator ) != nullptr )
			{
				emit commit_refused ( JsonEscapeValidator::refusal_reason () );
			}
			else
			{
				emit commit_refused ( tr ( "That value is not a valid JSON number." ) );
			}

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
