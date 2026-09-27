//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CodeEditor / LineNumberArea -- the Code View's text widget (EDITOR-07): a
//   QPlainTextEdit with a scroll-synchronized line-number gutter, a current-line highlight bar, and Tab / Shift+Tab
//   indentation over the document format profile.
//
//   THE CARET / SCROLL SPLIT is the load-bearing behaviour here, and it is why the two reveal operations are separate
//   methods rather than one. Version 1.0 lost a phase to this: a viewport that re-centres on the caret whenever
//   anything touches it makes an editor unusable, because the user cannot read one part of a document while the caret
//   sits in another. So:
//
//     scroll_to_line     -- reveals a line and does NOT touch the caret. This is what a tree SELECTION does
//                           (EDITOR-04), and it is why passive tree navigation cannot disturb an uncommitted edit.
//     move_caret_to_line -- moves the caret and reveals it. This is what the activation GESTURE does.
//
//   Scrolling by hand does neither, which is the third case and the one that has to stay free of both: it never moves
//   the caret and the viewport never snaps back to it. QPlainTextEdit already behaves this way; centerOnScroll is
//   turned off explicitly because leaving it on is precisely the snap-back this forbids.
//
//   THE GUTTER is the canonical Qt pattern -- a sibling widget living in the editor's left viewport margin, painted by
//   the editor and scroll-synchronized through updateRequest / blockCountChanged. LineNumberArea is deliberately a bare
//   widget that forwards its paint and does nothing else; all the knowledge stays in the editor, which is the only
//   thing that can convert a block to a y coordinate.
//
//   THE TAB KEY is claimed here (EDITOR-07), which is why the view answers claims_tab_key() and drops out of the
//   NAV-04 pane cycle while focused. Shift+Tab likewise. There is no other way out of this widget by keyboard, which is
//   a deliberate trade: an editor that loses the caret to another pane on Tab cannot be typed in. ENTER keeps the
//   indentation by the same profile (code_indentation). None of the three writes to a read-only editor.
//
//   FOLDING (EDITOR-23, Phase 15k) hides the lines between a container's brackets with QTextBlock::setVisible. The
//   editor is told WHICH regions exist -- CodeView derives them from its text index -- and owns everything after that:
//   the gutter markers and their hit test, the fold keys, block visibility, and the rule that a line asked for is a line
//   shown. Off by default, so the read-only previews that reuse this widget grow no marker column.
//
//   A fold has TWO identities, one for each way the text can change:
//
//     - While the user TYPES, a fold travels with the lines it hides. Qt moves a block's visibility with its text
//       (measured on Qt 6.10.1: hidden blocks stay hidden across an insertion above them), so a region whose opening
//       line shows and whose interior is all hidden is folded, wherever typing has moved it -- including when an array
//       element is inserted above a folded sibling, which renumbers its pointer. A region inside another fold is
//       hidden whatever its own state, so for that one the mark on its opening block says.
//     - When the WHOLE TEXT is replaced -- a refresh after an edit elsewhere, Esc, a format change -- every block is
//       new and visible (measured: setPlainText leaves nothing hidden and no user data), so the fold is found again by
//       its node's POINTER, the identity TreeViewPane carries expansion by across a rebuild (TREE-07).
//
//   Regions are re-derived only from a COMPLETE index. While the text does not parse, the index is silent below the
//   error, and an absence there means nothing -- so folds stand as the blocks hold them until the text parses again.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "style/CodeTokenPalette.hpp"
#include "views/code_folding.hpp"

#include <vje_core/services/JsonFormatter.hpp>

#include <QPlainTextEdit>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace vje
{
	class CodeEditor;

	//*****************************************************************************************************************
	// Class: LineNumberArea
	//*****************************************************************************************************************

	class LineNumberArea : public QWidget
	{
		Q_OBJECT

	public:

		explicit LineNumberArea ( CodeEditor* editor );

		QSize sizeHint () const override;

	protected:

		void paintEvent     ( QPaintEvent* event ) override;
		void mousePressEvent ( QMouseEvent* event ) override;

	private:

		CodeEditor* editor;   // Non-owning; also this widget's Qt parent.
	};

	//*****************************************************************************************************************
	// Class: CodeEditor
	//*****************************************************************************************************************

	class CodeEditor : public QPlainTextEdit
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit CodeEditor ( QWidget* parent = nullptr );

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// The profile drives what the Tab key inserts (EDITOR-07). The editor does not format anything itself -- the
		// text it is given is already JsonFormatter output -- so this is the profile's only use here.

		void set_format_profile ( const FormatProfile& profile );

		void set_token_palette ( const CodeTokenPalette& tokens );

		// Replace the whole text while keeping the caret's line/column and the scroll offset where they were, which is
		// what makes a re-render after an edit elsewhere (EDITOR-08) not throw the user back to line 1. Both are
		// clamped to the new text, and to a VISIBLE line: a caret the new text would put inside a fold lands on the
		// fold's opening line rather than opening it -- nothing the user did asked for that fold to open.
		//
		// regions are the new text's foldable regions; the folds held are re-applied to them by pointer BEFORE the view
		// is restored, since the scroll offset counts visible lines only.

		void set_text_preserving_view ( const QString& text, const QList<FoldRegion>& regions = {} );

		//=============================================================================================================
		// Folding (EDITOR-23). See the header.
		//=============================================================================================================

	public:

		void set_folding_enabled ( bool enabled );

		// The regions a COMPLETE index of the current text found. After typing, the blocks say which are folded; after a
		// whole-text replacement, a region is folded when its pointer is one the editor holds. An edit INSIDE a fold -- a
		// visible line now between its brackets -- opens it rather than hiding the line under the user.

		void apply_fold_regions ( const QList<FoldRegion>& regions );

		void clear_folds ();                        // Everything visible: a freshly loaded document (EDITOR-23).

		// Replace the pointers the folds are held by, touching no block. For CodeView, which follows a folded node that
		// an edit in another view has moved, and does so before the refresh that will look the pointers up.

		void set_folded_pointers ( const QSet<QString>& pointers );

		bool toggle_fold      ( int line );         // At a marker line (1-based). False when no region opens there.
		void fold_at_caret    ();                   // The innermost open region around the caret, and the next out.
		void unfold_at_caret  ();                   // The innermost folded region whose bracket line the caret is on.
		void reveal_line      ( int line );         // Open every fold that hides the line.

		bool        is_line_visible  ( int line ) const;
		QStringList folded_pointers  () const;      // In no particular order.

		// What is SHOWN: the visible lines, newline-joined. What printing prints (FILE-12) -- a folded region prints
		// folded, because the page is the view's rendering and the rendering has the fold in it.

		QString visible_text () const;

		// The 1-based line drawn at a y coordinate of the gutter, or 0 below the last line. The gutter's hit test, and
		// public so a test can aim a click without assuming a font's line height.

		int line_at_gutter_y ( int y ) const;

		//=============================================================================================================
		// Commands -- the caret / scroll split. See the header.
		//=============================================================================================================

	public:

		// Both open any fold hiding the line: a line asked for is a line shown, or tree navigation and Find would scroll
		// to a hidden line and appear to do nothing (EDITOR-23). scroll_to_line opens it explicitly; move_caret_to_line
		// by landing the caret there, which the caret rule answers.

		void scroll_to_line     ( int line );   // Reveal without touching the caret. 1-based; out of range is a no-op.
		void move_caret_to_line ( int line );   // Move the caret to the line's first non-blank character, and reveal.

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		int  caret_line   () const;             // 1-based.
		int  gutter_width () const;

		//=============================================================================================================
		// Painting -- called by LineNumberArea.
		//=============================================================================================================

	public:

		void paint_line_numbers  ( QPaintEvent* event );
		void handle_gutter_press ( QMouseEvent* event );

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// A double click, reported as the 1-based line the caret landed on. The editor translates the gesture into a
		// LINE and stops there -- which node that line belongs to, and what should happen to the selection, is
		// CodeView's to decide (EDITOR-07). The word under the cursor is still selected, as in any text editor: this is
		// a second meaning laid on the same gesture, not a replacement for it.

		void line_double_clicked ( int line );

		// The set of folds changed -- a toggle, a reveal, a re-derivation. CodeView listens so it can note which NODES are
		// folded while its text is the document's.

		void folds_changed ();

		//=============================================================================================================
		// QWidget / QPlainTextEdit
		//=============================================================================================================

	protected:

		void resizeEvent            ( QResizeEvent* event )   override;
		void keyPressEvent          ( QKeyEvent* event )      override;
		void mouseDoubleClickEvent  ( QMouseEvent* event )    override;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void update_gutter_width ();
		void update_gutter_area  ( const QRect& rect, int verticalScroll );
		void highlight_current_line ();

		// A caret that lands inside a fold opens it. Up and Down step over hidden lines, but Right from the end of a
		// fold's opening line lands inside the fold (measured on Qt 6.10.1), and so can an undo or a click-drag -- so the
		// rule is stated on where the caret ENDS UP, not on the keys that might put it there.

		void reveal_caret ();

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// EDITOR-07's two indentation cases. With no selection Tab inserts one level AT THE CARET and Shift+Tab
		// outdents the caret's line; with a selection both act on every touched line as a block, and re-select it so a
		// second press indents the same lines again.

		void indent_selection  ();
		void outdent_selection ();
		void insert_new_line   ();   // Enter, through indent_for_new_line.

		// Backspace in a line's indentation, through backspace_width. False where the rule leaves the key to Qt -- a
		// selection, a caret at the start of the line, or text before the caret -- so the caller passes it on.

		bool smart_backspace ();

		bool has_multi_line_selection () const;

		// The regions and fold state as the BLOCKS hold them now -- current however the text has moved since the last
		// derivation, which is why toggling asks this rather than keeping a list of line numbers that typing would stale.

		QList<FoldRegion> regions_from_blocks ( QSet<QString>* outFolded ) const;

		// Make the blocks say what regions and folded say: the markers' user data, and each block's visibility. The one
		// place a fold's state is written.

		void install_folds ( const QList<FoldRegion>& regions, const QSet<QString>& folded );

		int fold_column_width () const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		LineNumberArea*  gutter = nullptr;   // Parent-owned.
		FormatProfile    formatProfile;
		CodeTokenPalette tokens;

		bool          foldingEnabled = false;
		QSet<QString> foldedPointers;          // The folds, by node -- the identity across a whole-text replacement.
	};
}
