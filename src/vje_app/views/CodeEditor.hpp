//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
//   a deliberate trade: an editor that loses the caret to another pane on Tab cannot be typed in.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "style/CodeTokenPalette.hpp"

#include <vje_core/services/JsonFormatter.hpp>

#include <QPlainTextEdit>
#include <QWidget>

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

		void paintEvent ( QPaintEvent* event ) override;

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
		// clamped to the new text.

		void set_text_preserving_view ( const QString& text );

		//=============================================================================================================
		// Commands -- the caret / scroll split. See the header.
		//=============================================================================================================

	public:

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

		void paint_line_numbers ( QPaintEvent* event );

		//=============================================================================================================
		// QWidget / QPlainTextEdit
		//=============================================================================================================

	protected:

		void resizeEvent   ( QResizeEvent* event ) override;
		void keyPressEvent ( QKeyEvent* event )    override;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void update_gutter_width ();
		void update_gutter_area  ( const QRect& rect, int verticalScroll );
		void highlight_current_line ();

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// EDITOR-07's two indentation cases. With no selection Tab inserts one level AT THE CARET and Shift+Tab
		// outdents the caret's line; with a selection both act on every touched line as a block, and re-select it so a
		// second press indents the same lines again.

		void indent_selection  ();
		void outdent_selection ();

		bool has_multi_line_selection () const;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		LineNumberArea*  gutter = nullptr;   // Parent-owned.
		FormatProfile    formatProfile;
		CodeTokenPalette tokens;
	};
}
