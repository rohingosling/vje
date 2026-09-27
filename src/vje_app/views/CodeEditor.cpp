//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CodeEditor / LineNumberArea implementation. See the header for the caret / scroll split, which is the reason the
//   two reveal operations are separate, and for the two identities a fold carries.
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
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextDocumentLayout>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>

#include <algorithm>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// A fold's marks on the text itself. The block carrying a region's OPENING bracket holds the region's pointer
		// and whether it is folded; the block carrying its CLOSING bracket holds the pointer too, so the pair can be
		// found again from the blocks alone after typing has moved both. One block can close several regions ("} ]")
		// and close one while opening another ("}, {"), hence the list beside the single opener.
		//
		// The document owns user data and deletes it with its block (QTextBlock::setUserData).
		//-------------------------------------------------------------------------------------------------------------

		class FoldBlockData : public QTextBlockUserData
		{
		public:

			QString     opens;
			bool        folded = false;
			QStringList closes;
		};

		FoldBlockData* fold_data ( const QTextBlock& block )
		{
			return dynamic_cast<FoldBlockData*> ( block.userData () );
		}

		FoldBlockData* fold_data_for_writing ( QTextBlock block )
		{
			FoldBlockData* data = fold_data ( block );

			if ( data == nullptr )
			{
				data = new FoldBlockData;

				block.setUserData ( data );
			}

			return data;
		}
	}

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

	void LineNumberArea::mousePressEvent ( QMouseEvent* event )
	{
		// The fold markers' hit test is the editor's for the same reason the painting is: only it can map a y coordinate
		// to a block.

		editor->handle_gutter_press ( event );
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
		connect ( this, &QPlainTextEdit::cursorPositionChanged,  this, &CodeEditor::reveal_caret );
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

	void CodeEditor::set_text_preserving_view ( const QString& text, const QList<FoldRegion>& regions )
	{
		const int caretBlock  = textCursor ().blockNumber ();
		const int caretColumn = textCursor ().positionInBlock ();
		const int topBlock    = firstVisibleBlock ().blockNumber ();

		setPlainText ( text );

		// The folds go back on BEFORE the view is restored: the scroll offset counts visible lines, so a view restored
		// first and folded afterwards would land the reader somewhere else by the height of every fold above them.

		apply_fold_regions ( regions );

		// The nearest visible line at or above a block -- where a position the new text puts inside a fold goes instead.

		const auto visible_at_or_above = [ this ] ( int blockNumber )
		{
			QTextBlock block = document ()->findBlockByNumber ( std::clamp ( blockNumber, 0, blockCount () - 1 ) );

			while ( !block.isVisible () && block.previous ().isValid () )
			{
				block = block.previous ();
			}

			return block;
		};

		// Clamped both ways: the new text may be shorter than the old caret position, which is the ordinary case after
		// an edit elsewhere removed a subtree.

		QTextCursor restored ( visible_at_or_above ( caretBlock ) );

		restored.movePosition
		(
			QTextCursor::Right,
			QTextCursor::MoveAnchor,
			std::min ( caretColumn, restored.block ().length () - 1 )
		);

		// setTextCursor reveals the caret, which is right when the caret MOVED and wrong here -- nothing moved, the
		// text under it was replaced. Restoring the scroll offset afterwards is what keeps the reader where they were.
		// It is restored as the top LINE rather than as a raw scroll value, which is the same number when nothing is
		// folded and the right one when something is.

		setTextCursor ( restored );

		verticalScrollBar ()->setValue ( visible_at_or_above ( topBlock ).firstLineNumber () );
	}

	//=================================================================================================================
	// Folding
	//=================================================================================================================

	void CodeEditor::set_folding_enabled ( bool enabled )
	{
		if ( enabled == foldingEnabled )
		{
			return;
		}

		if ( !enabled )
		{
			clear_folds ();
		}

		foldingEnabled = enabled;

		update_gutter_width ();
	}

	void CodeEditor::apply_fold_regions ( const QList<FoldRegion>& regions )
	{
		if ( !foldingEnabled )
		{
			return;
		}

		// Which identity to trust. Blocks that carry fold data have been TYPED around since the last derivation, and they
		// know better than a pointer where each fold now is. Blocks that carry none are new -- the whole text was
		// replaced (setPlainText leaves no user data) -- and the pointer is all there is.

		bool blocksCarryFolds = false;

		for ( QTextBlock block = document ()->begin (); block.isValid (); block = block.next () )
		{
			if ( fold_data ( block ) != nullptr )
			{
				blocksCarryFolds = true;

				break;
			}
		}

		QSet<QString> folded;

		for ( const FoldRegion& region : regions )
		{
			if ( !blocksCarryFolds )
			{
				if ( foldedPointers.contains ( region.pointer ) )
				{
					folded.insert ( region.pointer );
				}

				continue;
			}

			const QTextBlock openBlock = document ()->findBlockByNumber ( region.openLine - 1 );

			// A region inside another fold cannot be read off what is visible -- its lines are hidden by the outer fold
			// whatever its own state -- so its opening block's mark says. Typing cannot reach a hidden block, so that mark
			// is still on the block it was put on.

			if ( !openBlock.isVisible () )
			{
				const FoldBlockData* const data = fold_data ( openBlock );

				if ( ( data != nullptr ) && !data->opens.isEmpty () && data->folded )
				{
					folded.insert ( region.pointer );
				}

				continue;
			}

			// A region whose opening line shows is folded exactly when EVERYTHING between its brackets is hidden: a
			// nested fold keeps its own bracket lines visible, so nothing but this region's fold hides all of them.
			// Visibility is read rather than the opening block's mark, because the mark stays with the text BEFORE a
			// split (measured on Qt 6.10.1) -- a line pasted at the start of a fold's opening line takes the mark with
			// it, while the hidden lines stay exactly where they were.
			//
			// And an edit INSIDE the fold -- a line the user can see, now between its brackets -- opens it. Keeping it
			// folded would hide the line they just made, under the caret, the moment they paused typing.

			bool interiorHidden = true;

			for ( QTextBlock block = openBlock.next (); block.isValid () && ( block.blockNumber () < ( region.closeLine - 1 ) ); block = block.next () )
			{
				if ( block.isVisible () )
				{
					interiorHidden = false;

					break;
				}
			}

			if ( interiorHidden )
			{
				folded.insert ( region.pointer );
			}
		}

		install_folds ( regions, folded );
	}

	void CodeEditor::clear_folds ()
	{
		install_folds ( regions_from_blocks ( nullptr ), QSet<QString> () );

		// install_folds keeps only the pointers of regions the blocks still hold; one the blocks have lost track of --
		// below a syntax error, say -- would otherwise outlive the load that was meant to clear it.

		foldedPointers.clear ();
	}

	void CodeEditor::set_folded_pointers ( const QSet<QString>& pointers )
	{
		foldedPointers = pointers;
	}

	bool CodeEditor::toggle_fold ( int line )
	{
		if ( !foldingEnabled )
		{
			return false;
		}

		QSet<QString> folded;

		const QList<FoldRegion> regions = regions_from_blocks ( &folded );

		for ( const FoldRegion& region : regions )
		{
			if ( region.openLine != line )
			{
				continue;
			}

			if ( folded.contains ( region.pointer ) )
			{
				folded.remove ( region.pointer );
			}
			else
			{
				// A caret inside what is about to be hidden moves to the fold's opening line first -- left where it was,
				// it would sit in a hidden line, and the rule that opens a fold around the caret would undo this at once.

				const int caret = caret_line ();

				if ( ( caret > region.openLine ) && ( caret < region.closeLine ) )
				{
					QTextCursor cursor ( document ()->findBlockByNumber ( region.openLine - 1 ) );

					cursor.movePosition ( QTextCursor::EndOfBlock );

					setTextCursor ( cursor );
				}

				folded.insert ( region.pointer );
			}

			install_folds ( regions, folded );

			return true;
		}

		return false;
	}

	void CodeEditor::fold_at_caret ()
	{
		QSet<QString> folded;

		const QList<FoldRegion> regions = regions_from_blocks ( &folded );
		const int               index   = innermost_region_at_line ( regions, folded, caret_line (), false );

		if ( index >= 0 )
		{
			toggle_fold ( regions [ index ].openLine );
		}
	}

	void CodeEditor::unfold_at_caret ()
	{
		QSet<QString> folded;

		const QList<FoldRegion> regions = regions_from_blocks ( &folded );
		const int               index   = innermost_region_at_line ( regions, folded, caret_line (), true );

		if ( index >= 0 )
		{
			toggle_fold ( regions [ index ].openLine );
		}
	}

	void CodeEditor::reveal_line ( int line )
	{
		if ( !foldingEnabled || is_line_visible ( line ) || !document ()->findBlockByNumber ( line - 1 ).isValid () )
		{
			return;
		}

		QSet<QString> folded;

		const QList<FoldRegion> regions = regions_from_blocks ( &folded );

		for ( const QString& pointer : folds_hiding_line ( regions, folded, line ) )
		{
			folded.remove ( pointer );
		}

		// Installed even when no fold claims the line. A hidden line no fold accounts for is one whose fold lost its
		// closing line to an edit, and installing re-derives every line's visibility from the folds that remain -- so
		// the orphan is shown rather than left unreachable.

		install_folds ( regions, folded );
	}

	bool CodeEditor::is_line_visible ( int line ) const
	{
		const QTextBlock block = document ()->findBlockByNumber ( line - 1 );

		return block.isValid () && block.isVisible ();
	}

	QStringList CodeEditor::folded_pointers () const
	{
		return QStringList ( foldedPointers.cbegin (), foldedPointers.cend () );
	}

	QString CodeEditor::visible_text () const
	{
		// Asked of the BLOCKS, not of the fold set: the set can move ahead of them -- CodeView re-points it when another
		// view moves a folded node, before the refresh that re-applies it -- and what prints is what is on screen.

		QStringList lines;
		bool        anyHidden = false;

		for ( QTextBlock block = document ()->begin (); block.isValid (); block = block.next () )
		{
			if ( block.isVisible () )
			{
				lines.append ( block.text () );
			}
			else
			{
				anyHidden = true;
			}
		}

		// Nothing hidden is the ordinary case, and answers exactly as before folding existed.

		if ( !anyHidden )
		{
			return toPlainText ();
		}

		// The two substitutions QTextDocument::toPlainText makes, so a folded print differs from an unfolded one by the
		// hidden lines and nothing else.

		return lines.join ( QLatin1Char ( '\n' ) )
			.replace ( QChar::LineSeparator, QLatin1Char ( '\n' ) )
			.replace ( QChar::Nbsp,          QLatin1Char ( ' ' ) );
	}

	int CodeEditor::line_at_gutter_y ( int y ) const
	{
		QTextBlock block = firstVisibleBlock ();

		int top = static_cast<int> ( blockBoundingGeometry ( block ).translated ( contentOffset () ).top () );

		while ( block.isValid () && ( top <= y ) )
		{
			const int bottom = top + static_cast<int> ( blockBoundingRect ( block ).height () );

			if ( block.isVisible () && ( y < bottom ) )
			{
				return block.blockNumber () + 1;
			}

			block = block.next ();
			top   = bottom;
		}

		return 0;
	}

	QList<FoldRegion> CodeEditor::regions_from_blocks ( QSet<QString>* outFolded ) const
	{
		QHash<QString, int>  openLines;
		QHash<QString, bool> openFolded;
		QList<FoldRegion>    regions;

		for ( QTextBlock block = document ()->begin (); block.isValid (); block = block.next () )
		{
			const FoldBlockData* const data = fold_data ( block );

			if ( data == nullptr )
			{
				continue;
			}

			const int line = block.blockNumber () + 1;

			for ( const QString& pointer : data->closes )
			{
				const auto open = openLines.constFind ( pointer );

				// A pair typing has pulled together, leaving nothing between them, is no longer a region.

				if ( ( open != openLines.constEnd () ) && ( ( line - open.value () ) >= 2 ) )
				{
					regions.append ( FoldRegion { pointer, open.value (), line } );

					if ( ( outFolded != nullptr ) && openFolded.value ( pointer ) )
					{
						outFolded->insert ( pointer );
					}
				}
			}

			if ( !data->opens.isEmpty () )
			{
				openLines.insert  ( data->opens, line );
				openFolded.insert ( data->opens, data->folded );
			}
		}

		// Found in closing order; the pure functions want opening order, which puts an outer region before the regions
		// inside it.

		std::sort
		(
			regions.begin (),
			regions.end (),
			[] ( const FoldRegion& left, const FoldRegion& right ) { return left.openLine < right.openLine; }
		);

		return regions;
	}

	void CodeEditor::install_folds ( const QList<FoldRegion>& regions, const QSet<QString>& folded )
	{
		// The marks first, rebuilt from nothing: whatever the blocks carried is either restated here or was stale.

		for ( QTextBlock block = document ()->begin (); block.isValid (); block = block.next () )
		{
			if ( fold_data ( block ) != nullptr )
			{
				block.setUserData ( nullptr );
			}
		}

		for ( const FoldRegion& region : regions )
		{
			const QTextBlock openBlock  = document ()->findBlockByNumber ( region.openLine  - 1 );
			const QTextBlock closeBlock = document ()->findBlockByNumber ( region.closeLine - 1 );

			if ( !openBlock.isValid () || !closeBlock.isValid () )
			{
				continue;
			}

			FoldBlockData* const openData = fold_data_for_writing ( openBlock );

			openData->opens  = region.pointer;
			openData->folded = folded.contains ( region.pointer );

			fold_data_for_writing ( closeBlock )->closes.append ( region.pointer );
		}

		// Then visibility, from the pure rule. Only the blocks whose state actually changes are touched, and the layout
		// is invalidated over just their range.

		const QList<bool> visible = visible_lines ( blockCount (), regions, folded );

		int changedFrom = -1;
		int changedTo   = -1;

		for ( QTextBlock block = document ()->begin (); block.isValid (); block = block.next () )
		{
			const bool wanted = visible.value ( block.blockNumber (), true );

			if ( block.isVisible () == wanted )
			{
				continue;
			}

			block.setVisible ( wanted );

			if ( changedFrom < 0 )
			{
				changedFrom = block.position ();
			}

			changedTo = block.position () + block.length ();
		}

		QSet<QString> installed;

		for ( const FoldRegion& region : regions )
		{
			if ( folded.contains ( region.pointer ) )
			{
				installed.insert ( region.pointer );
			}
		}

		const bool setChanged = ( installed != foldedPointers );

		foldedPointers = installed;

		if ( setChanged )
		{
			emit folds_changed ();
		}

		// setVisible alone changes nothing the layout knows about: measured on Qt 6.10.1, a hidden block keeps its line
		// count and the scroll range is unchanged until the range is marked dirty -- after which a hidden block counts
		// zero lines, and the scroll bar counts visible lines only.

		if ( changedFrom >= 0 )
		{
			document ()->markContentsDirty ( changedFrom, changedTo - changedFrom );

			viewport ()->update ();
		}

		gutter->update ();
	}

	int CodeEditor::fold_column_width () const
	{
		return foldingEnabled ? fontMetrics ().height () : 0;
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

		reveal_line ( line );

		// Deliberately NOT setTextCursor / ensureCursorVisible: this must not move the caret (EDITOR-04). The scroll
		// bar counts LINES -- visible ones only, once anything is folded -- so the target is placed by its line number
		// rather than its block number, which are the same until a fold above it makes them differ.
		//
		// The line is placed a third of the way down the viewport rather than at its top, so the node's context above
		// it is visible -- an object member revealed flush against the top edge hides the object it belongs to.

		const int visibleLines = std::max ( 1, viewport ()->height () / std::max ( 1, static_cast<int> ( blockBoundingRect ( block ).height () ) ) );
		const int target       = std::max ( 0, block.firstLineNumber () - ( visibleLines / 3 ) );

		verticalScrollBar ()->setValue ( std::min ( target, verticalScrollBar ()->maximum () ) );
	}

	void CodeEditor::move_caret_to_line ( int line )
	{
		const QTextBlock block = document ()->findBlockByNumber ( line - 1 );

		if ( !block.isValid () )
		{
			return;
		}

		// No reveal_line here, unlike scroll_to_line: the caret is about to LAND on the line, and a caret that lands in a
		// fold opens it (reveal_caret) -- so a second statement of the rule would be one no test could tell apart from
		// the first.

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
		     + ( digits * fontMetrics ().horizontalAdvance ( QLatin1Char ( '9' ) ) )
		     + fold_column_width ();
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
		const int foldColumn   = fold_column_width ();

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
					gutter->width () - config::code::GUTTER_HORIZONTAL_PADDING - foldColumn,
					fontMetrics ().height (),
					Qt::AlignRight | Qt::AlignVCenter,
					QString::number ( block.blockNumber () + 1 )
				);

				// The fold marker, in the digits' own colour (EDITOR-23): a chevron pointing down over an open region and
				// right over a folded one -- the tree's expander convention, so the gesture reads as the one it is.

				const FoldBlockData* const data = fold_data ( block );

				if ( ( foldColumn > 0 ) && ( data != nullptr ) && !data->opens.isEmpty () )
				{
					const qreal   extent = foldColumn * config::code::FOLD_CHEVRON_EXTENT;
					const QPointF centre ( gutter->width () - ( foldColumn / 2.0 ), top + ( fontMetrics ().height () / 2.0 ) );

					QPainterPath chevron;

					if ( data->folded )
					{
						chevron.moveTo ( centre + QPointF ( -extent / 4.0, -extent / 2.0 ) );
						chevron.lineTo ( centre + QPointF (  extent / 4.0,  0.0          ) );
						chevron.lineTo ( centre + QPointF ( -extent / 4.0,  extent / 2.0 ) );
					}
					else
					{
						chevron.moveTo ( centre + QPointF ( -extent / 2.0, -extent / 4.0 ) );
						chevron.lineTo ( centre + QPointF (  0.0,           extent / 4.0 ) );
						chevron.lineTo ( centre + QPointF (  extent / 2.0, -extent / 4.0 ) );
					}

					painter.save ();
					painter.setRenderHint ( QPainter::Antialiasing, true );
					painter.setPen ( QPen ( painter.pen ().color (), config::code::FOLD_CHEVRON_STROKE, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
					painter.drawPath ( chevron );
					painter.restore ();
				}
			}

			block  = block.next ();
			top    = bottom;
			bottom = top + static_cast<int> ( blockBoundingRect ( block ).height () );
		}
	}

	void CodeEditor::handle_gutter_press ( QMouseEvent* event )
	{
		// The whole row answers, not just the chevron: the gutter does nothing else with a click, and a target one line
		// tall and a few pixels wide is harder to hit than it needs to be.

		if ( !foldingEnabled || ( event->button () != Qt::LeftButton ) )
		{
			return;
		}

		const int line = line_at_gutter_y ( static_cast<int> ( event->position ().y () ) );

		if ( ( line > 0 ) && toggle_fold ( line ) )
		{
			event->accept ();
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
		// A read-only editor -- the Import XML preview -- takes none of the keys below. They write through a
		// QTextCursor, and a cursor's writes are not stopped by the widget being read-only.

		if ( isReadOnly () )
		{
			QPlainTextEdit::keyPressEvent ( event );

			return;
		}

		// EDITOR-07: Enter keeps the indentation (code_indentation). The keypad's Enter is the same key; Shift+Enter keeps
		// Qt's meaning, a line break within the block.

		const Qt::KeyboardModifiers modifiers = event->modifiers () & ~Qt::KeypadModifier;

		if ( ( ( event->key () == Qt::Key_Return ) || ( event->key () == Qt::Key_Enter ) ) && ( modifiers == Qt::NoModifier ) )
		{
			insert_new_line ();

			event->accept ();

			return;
		}

		// EDITOR-23: Ctrl+Shift+[ folds the region around the caret and Ctrl+Shift+] unfolds it. With Shift held the
		// bracket keys may arrive as the BRACE keys, depending on the layout, so both spellings are the same command.

		if ( foldingEnabled && modifiers.testFlag ( Qt::ControlModifier ) && !modifiers.testFlag ( Qt::AltModifier ) )
		{
			const bool shifted = modifiers.testFlag ( Qt::ShiftModifier );

			if ( ( event->key () == Qt::Key_BraceLeft ) || ( shifted && ( event->key () == Qt::Key_BracketLeft ) ) )
			{
				fold_at_caret ();

				event->accept ();

				return;
			}

			if ( ( event->key () == Qt::Key_BraceRight ) || ( shifted && ( event->key () == Qt::Key_BracketRight ) ) )
			{
				unfold_at_caret ();

				event->accept ();

				return;
			}
		}

		// EDITOR-07: Backspace in a line's indentation goes back to where the brackets say (Phase 15k.1). Shift+Backspace
		// is Backspace to Qt as well, so it is here too; Ctrl+Backspace keeps its meaning of deleting a word.

		if ( ( event->key () == Qt::Key_Backspace ) && ( ( modifiers & ~Qt::ShiftModifier ) == Qt::NoModifier ) && smart_backspace () )
		{
			event->accept ();

			return;
		}

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

	void CodeEditor::mouseDoubleClickEvent ( QMouseEvent* event )
	{
		// The base class FIRST. A double click selects the word under it, which is what a text editor does and what the
		// user still wants; it is also what puts the caret where they clicked, so the line below is read afterwards
		// rather than computed from the event position.

		QPlainTextEdit::mouseDoubleClickEvent ( event );

		emit line_double_clicked ( caret_line () );
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void CodeEditor::update_gutter_width ()
	{
		setViewportMargins ( gutter_width (), 0, 0, 0 );

		// The gutter's own geometry follows too. It used to change only on a resize, which was enough while its width
		// moved with the digit count alone; the fold column appears without one.

		const QRect editorArea = contentsRect ();

		gutter->setGeometry ( QRect ( editorArea.left (), editorArea.top (), gutter_width (), editorArea.height () ) );
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

	void CodeEditor::reveal_caret ()
	{
		if ( foldingEnabled && !textCursor ().block ().isVisible () )
		{
			reveal_line ( caret_line () );
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

	void CodeEditor::insert_new_line ()
	{
		QTextCursor cursor = textCursor ();

		// One edit block, so the selection it replaces, the line break and the indent undo as one step -- the Enter
		// the user pressed, rather than three things they did not do separately.

		cursor.beginEditBlock ();

		if ( cursor.hasSelection () )
		{
			cursor.removeSelectedText ();
		}

		const QTextBlock  block     = cursor.block ();
		const int         lineStart = block.position ();
		const NewLineEdit edit      = indent_for_new_line ( block.text (), cursor.positionInBlock (), formatProfile );

		cursor.setPosition ( lineStart + edit.replaceFrom );
		cursor.setPosition ( lineStart + edit.replaceTo, QTextCursor::KeepAnchor );
		cursor.insertText  ( edit.insertion );

		cursor.endEditBlock ();

		cursor.setPosition ( lineStart + edit.replaceFrom + edit.caretOffset );

		setTextCursor ( cursor );

		ensureCursorVisible ();
	}

	bool CodeEditor::smart_backspace ()
	{
		QTextCursor cursor = textCursor ();

		if ( cursor.hasSelection () )
		{
			return false;
		}

		// The lines above, nearest first, walked by block rather than looked up by number -- the scan goes only as far
		// as the enclosing bracket, but a top-level line's enclosing bracket is the document's first.

		const QTextBlock block = cursor.block ();

		QTextBlock above = block.previous ();

		const LineSource nextLineUp = [ &above ] () -> std::optional<QString>
		{
			if ( !above.isValid () )
			{
				return std::nullopt;
			}

			const QString text = above.text ();

			above = above.previous ();

			return text;
		};

		const int width = backspace_width ( block.text (), cursor.positionInBlock (), nextLineUp, formatProfile );

		if ( width == 0 )
		{
			return false;
		}

		// One edit block, so however much whitespace it takes, the press undoes as the one step it was.

		cursor.beginEditBlock ();

		cursor.setPosition        ( cursor.position () - width, QTextCursor::KeepAnchor );
		cursor.removeSelectedText ();

		cursor.endEditBlock ();

		setTextCursor ( cursor );

		ensureCursorVisible ();

		return true;
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
