//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   CodeEditor / LineNumberArea implementation. See the header for the caret / scroll split, which is the reason the
//   two reveal operations are separate.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/CodeEditor.hpp"

#include "AppConfig.hpp"
#include "style/fixed_font.hpp"
#include "views/code_indentation.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>

#include <algorithm>

namespace vje
{
	//=================================================================================================================
	// LineNumberArea
	//=================================================================================================================

	LineNumberArea::LineNumberArea ( CodeEditor* editor )
		: QWidget ( editor )
		, editor  ( editor )
	{
	}

	QSize LineNumberArea::sizeHint () const
	{
		return QSize ( editor->gutter_width (), 0 );
	}

	void LineNumberArea::paintEvent ( QPaintEvent* event )
	{
		// Forwarded rather than painted here: converting a text block to a y coordinate needs the editor's own
		// blockBoundingGeometry / contentOffset, which are protected. The gutter is a surface, not a component.

		editor->paint_line_numbers ( event );
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	CodeEditor::CodeEditor ( QWidget* parent )
		: QPlainTextEdit ( parent )
	{
		setObjectName ( QStringLiteral ( "codeEditor" ) );
		setLineWrapMode ( QPlainTextEdit::NoWrap );
		setFrameShape ( QFrame::NoFrame );

		// The snap-back this view exists to avoid. With centerOnScroll on, the viewport re-centres on the caret
		// whenever the document is touched, so reading one part of a file while the caret sits in another is impossible.

		setCenterOnScroll ( false );

		// Indentation only reads as nesting if every character is the same width -- and asking Qt for "the fixed font"
		// does not reliably give one (style/fixed_font.hpp).

		setFont ( monospace_font () );

		gutter = new LineNumberArea ( this );

		connect ( this, &QPlainTextEdit::blockCountChanged,      this, &CodeEditor::update_gutter_width );
		connect ( this, &QPlainTextEdit::updateRequest,          this, &CodeEditor::update_gutter_area );
		connect ( this, &QPlainTextEdit::cursorPositionChanged,  this, &CodeEditor::highlight_current_line );

		update_gutter_width ();
		highlight_current_line ();
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void CodeEditor::set_format_profile ( const FormatProfile& profile )
	{
		formatProfile = profile;

		// A tab CHARACTER in the text must occupy exactly one indent level, or a tab-indented document renders at a
		// width the saved file does not have -- and EDITOR-07's whole claim is that the two agree.

		const qreal spaceWidth = QFontMetricsF ( font () ).horizontalAdvance ( QLatin1Char ( ' ' ) );

		setTabStopDistance ( spaceWidth * ( ( profile.indentSize >= 1 ) ? profile.indentSize : 1 ) );
	}

	void CodeEditor::set_token_palette ( const CodeTokenPalette& newTokens )
	{
		tokens = newTokens;

		highlight_current_line ();

		if ( gutter != nullptr )
		{
			gutter->update ();
		}
	}

	void CodeEditor::set_text_preserving_view ( const QString& text )
	{
		const int caretBlock  = textCursor ().blockNumber ();
		const int caretColumn = textCursor ().positionInBlock ();
		const int scrollValue = verticalScrollBar ()->value ();

		setPlainText ( text );

		// Clamped both ways: the new text may be shorter than the old caret position, which is the ordinary case after
		// an edit elsewhere removed a subtree.

		QTextCursor restored ( document ()->findBlockByNumber ( std::min ( caretBlock, blockCount () - 1 ) ) );

		restored.movePosition
		(
			QTextCursor::Right,
			QTextCursor::MoveAnchor,
			std::min ( caretColumn, restored.block ().length () - 1 )
		);

		// setTextCursor reveals the caret, which is right when the caret MOVED and wrong here -- nothing moved, the
		// text under it was replaced. Restoring the scroll offset afterwards is what keeps the reader where they were.

		setTextCursor ( restored );

		verticalScrollBar ()->setValue ( scrollValue );
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	void CodeEditor::scroll_to_line ( int line )
	{
		const QTextBlock block = document ()->findBlockByNumber ( line - 1 );

		if ( !block.isValid () )
		{
			return;
		}

		// Deliberately NOT setTextCursor / ensureCursorVisible: this must not move the caret (EDITOR-04). The scroll
		// bar counts blocks directly, so the target line can be placed without the cursor being involved at all.
		//
		// The line is placed a third of the way down the viewport rather than at its top, so the node's context above
		// it is visible -- an object member revealed flush against the top edge hides the object it belongs to.

		const int visibleLines = std::max ( 1, viewport ()->height () / std::max ( 1, static_cast<int> ( blockBoundingRect ( block ).height () ) ) );
		const int target       = std::max ( 0, block.blockNumber () - ( visibleLines / 3 ) );

		verticalScrollBar ()->setValue ( std::min ( target, verticalScrollBar ()->maximum () ) );
	}

	void CodeEditor::move_caret_to_line ( int line )
	{
		const QTextBlock block = document ()->findBlockByNumber ( line - 1 );

		if ( !block.isValid () )
		{
			return;
		}

		QTextCursor cursor ( block );

		// The first non-blank character, not column 0: the caret lands where the content is, past the indentation,
		// which is where a user asked to be taken to a node would put it themselves.

		const QString text = block.text ();

		int column = 0;

		while ( ( column < text.length () ) && text.at ( column ).isSpace () )
		{
			++column;
		}

		cursor.movePosition ( QTextCursor::Right, QTextCursor::MoveAnchor, column );

		setTextCursor ( cursor );
		ensureCursorVisible ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	int CodeEditor::caret_line () const
	{
		return textCursor ().blockNumber () + 1;
	}

	int CodeEditor::gutter_width () const
	{
		int digits = config::code::GUTTER_MINIMUM_DIGITS;

		for ( int lines = blockCount (); lines >= 10; lines /= 10 )
		{
			++digits;
		}

		// '9' rather than the widest digit generally: this is a fixed-width font by construction, so every digit is the
		// same width and the choice is arbitrary -- but stating it as a digit keeps the measurement honest if the font
		// is ever allowed to be proportional.

		return ( 2 * config::code::GUTTER_HORIZONTAL_PADDING )
		     + ( digits * fontMetrics ().horizontalAdvance ( QLatin1Char ( '9' ) ) );
	}

	//=================================================================================================================
	// Painting
	//=================================================================================================================

	void CodeEditor::paint_line_numbers ( QPaintEvent* event )
	{
		QPainter painter ( gutter );

		if ( tokens.gutterBackground.isValid () )
		{
			painter.fillRect ( event->rect (), tokens.gutterBackground );
		}

		QTextBlock block = firstVisibleBlock ();

		int top    = static_cast<int> ( blockBoundingGeometry ( block ).translated ( contentOffset () ).top () );
		int bottom = top + static_cast<int> ( blockBoundingRect ( block ).height () );

		const int currentBlock = textCursor ().blockNumber ();

		while ( block.isValid () && ( top <= event->rect ().bottom () ) )
		{
			if ( block.isVisible () && ( bottom >= event->rect ().top () ) )
			{
				const bool isCurrent = ( block.blockNumber () == currentBlock );

				const QColor digitColour = isCurrent ? tokens.gutterCurrentText : tokens.gutterText;

				if ( digitColour.isValid () )
				{
					painter.setPen ( digitColour );
				}

				painter.drawText
				(
					0,
					top,
					gutter->width () - config::code::GUTTER_HORIZONTAL_PADDING,
					fontMetrics ().height (),
					Qt::AlignRight | Qt::AlignVCenter,
					QString::number ( block.blockNumber () + 1 )
				);
			}

			block  = block.next ();
			top    = bottom;
			bottom = top + static_cast<int> ( blockBoundingRect ( block ).height () );
		}
	}

	//=================================================================================================================
	// QWidget / QPlainTextEdit
	//=================================================================================================================

	void CodeEditor::resizeEvent ( QResizeEvent* event )
	{
		QPlainTextEdit::resizeEvent ( event );

		const QRect editorArea = contentsRect ();

		gutter->setGeometry ( QRect ( editorArea.left (), editorArea.top (), gutter_width (), editorArea.height () ) );
	}

	void CodeEditor::keyPressEvent ( QKeyEvent* event )
	{
		// EDITOR-07: Tab and Shift+Tab manage indentation rather than moving keyboard focus. The view answers
		// claims_tab_key() so PaneCycler leaves the key alone while this widget has it (NAV-04).

		if ( ( event->key () == Qt::Key_Tab ) && ( event->modifiers () == Qt::NoModifier ) )
		{
			indent_selection ();

			event->accept ();

			return;
		}

		// Shift+Tab arrives as Key_Backtab on every platform Qt supports, with the Shift modifier already consumed.
		// Testing for Key_Tab with ShiftModifier -- the obvious spelling -- matches nothing and silently does nothing.

		if ( ( event->key () == Qt::Key_Backtab ) || ( ( event->key () == Qt::Key_Tab ) && ( event->modifiers () & Qt::ShiftModifier ) ) )
		{
			outdent_selection ();

			event->accept ();

			return;
		}

		QPlainTextEdit::keyPressEvent ( event );
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void CodeEditor::update_gutter_width ()
	{
		setViewportMargins ( gutter_width (), 0, 0, 0 );
	}

	void CodeEditor::update_gutter_area ( const QRect& rect, int verticalScroll )
	{
		if ( verticalScroll != 0 )
		{
			gutter->scroll ( 0, verticalScroll );
		}
		else
		{
			gutter->update ( 0, rect.y (), gutter->width (), rect.height () );
		}

		if ( rect.contains ( viewport ()->rect () ) )
		{
			update_gutter_width ();
		}
	}

	void CodeEditor::highlight_current_line ()
	{
		QList<QTextEdit::ExtraSelection> selections;

		if ( tokens.currentLineBackground.isValid () )
		{
			QTextEdit::ExtraSelection currentLine;

			currentLine.format.setBackground ( tokens.currentLineBackground );

			// FullWidthSelection is what makes it a BAR across the viewport rather than a highlight sized to the line's
			// text, which is what EDITOR-07 asks for and is also the only version that is findable at a glance.

			currentLine.format.setProperty ( QTextFormat::FullWidthSelection, true );

			currentLine.cursor = textCursor ();
			currentLine.cursor.clearSelection ();

			selections.append ( currentLine );
		}

		setExtraSelections ( selections );

		if ( gutter != nullptr )
		{
			gutter->update ();
		}
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	bool CodeEditor::has_multi_line_selection () const
	{
		const QTextCursor cursor = textCursor ();

		if ( !cursor.hasSelection () )
		{
			return false;
		}

		return document ()->findBlock ( cursor.selectionStart () ).blockNumber ()
		    != document ()->findBlock ( cursor.selectionEnd   () ).blockNumber ();
	}

	void CodeEditor::indent_selection ()
	{
		QTextCursor cursor = textCursor ();

		// EDITOR-07's no-selection case: one indent level AT THE CARET. A selection inside a single line is the same
		// case -- the user is replacing the selected text, exactly as typing any other character would.

		if ( !has_multi_line_selection () )
		{
			cursor.insertText ( indent_unit ( formatProfile ) );

			setTextCursor ( cursor );

			return;
		}

		QTextBlock firstBlock = document ()->findBlock ( cursor.selectionStart () );
		QTextBlock lastBlock  = document ()->findBlock ( cursor.selectionEnd () );

		// A selection ending exactly at the start of a line has not TOUCHED that line -- it is where the drag stopped.
		// Indenting it anyway is the classic off-by-one that makes block indent feel one line too greedy.

		if ( ( lastBlock.position () == cursor.selectionEnd () ) && ( lastBlock.blockNumber () > firstBlock.blockNumber () ) )
		{
			lastBlock = lastBlock.previous ();
		}

		// Taken as OFFSETS before the edit, not as QTextBlock handles used after it. A QTextBlock is a lightweight
		// handle into the document's block list, and inserting text rebuilds that list -- so asking a block captured
		// beforehand for its position afterwards answers with whatever now occupies its slot. The symptom is a second
		// Tab press replacing part of the selection instead of indenting it again.

		const int blockStart = firstBlock.position ();
		const int blockEnd   = lastBlock.position () + lastBlock.length () - 1;

		QTextCursor blockCursor ( document () );

		blockCursor.setPosition ( blockStart );
		blockCursor.setPosition ( blockEnd, QTextCursor::KeepAnchor );

		const QString transformed = indent_block ( blockCursor.selectedText ().replace ( QChar ( 0x2029 ), QLatin1Char ( '\n' ) ), formatProfile );

		// One edit, so one undo step: a block indent the user has to undo line by line is not a block indent.

		blockCursor.beginEditBlock ();
		blockCursor.insertText ( transformed );
		blockCursor.endEditBlock ();

		// Re-select the same lines, so a second Tab indents them again rather than replacing them with a tab.

		QTextCursor reselected ( document () );

		reselected.setPosition ( blockStart );
		reselected.setPosition ( blockCursor.position (), QTextCursor::KeepAnchor );

		setTextCursor ( reselected );
	}

	void CodeEditor::outdent_selection ()
	{
		QTextCursor cursor = textCursor ();

		if ( !has_multi_line_selection () )
		{
			// EDITOR-07's no-selection case: outdent the caret's LINE, wherever in it the caret happens to be.

			const QTextBlock block = cursor.block ();
			const int        width = outdent_width ( block.text (), formatProfile );

			if ( width == 0 )
			{
				return;
			}

			QTextCursor lineCursor ( block );

			lineCursor.setPosition ( block.position () );
			lineCursor.setPosition ( block.position () + width, QTextCursor::KeepAnchor );
			lineCursor.removeSelectedText ();

			return;
		}

		QTextBlock firstBlock = document ()->findBlock ( cursor.selectionStart () );
		QTextBlock lastBlock  = document ()->findBlock ( cursor.selectionEnd () );

		if ( ( lastBlock.position () == cursor.selectionEnd () ) && ( lastBlock.blockNumber () > firstBlock.blockNumber () ) )
		{
			lastBlock = lastBlock.previous ();
		}

		// Offsets, not block handles -- see indent_selection above.

		const int blockStart = firstBlock.position ();
		const int blockEnd   = lastBlock.position () + lastBlock.length () - 1;

		QTextCursor blockCursor ( document () );

		blockCursor.setPosition ( blockStart );
		blockCursor.setPosition ( blockEnd, QTextCursor::KeepAnchor );

		const QString transformed = outdent_block ( blockCursor.selectedText ().replace ( QChar ( 0x2029 ), QLatin1Char ( '\n' ) ), formatProfile );

		blockCursor.beginEditBlock ();
		blockCursor.insertText ( transformed );
		blockCursor.endEditBlock ();

		QTextCursor reselected ( document () );

		reselected.setPosition ( blockStart );
		reselected.setPosition ( blockCursor.position (), QTextCursor::KeepAnchor );

		setTextCursor ( reselected );
	}
}
