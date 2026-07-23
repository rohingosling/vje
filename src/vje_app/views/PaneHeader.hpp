//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   PaneHeader -- the titled band across the top of a workspace pane (STYLE-13): a full-width strip carrying the
//   pane's name, closed by a one-pixel rule that separates it from the content below.
//
//   IT WEARS A TAB'S SURFACE WITHOUT BEING A TAB. The fill and vertical shading come from the style's own selected-tab
//   painting, so the band sits in the same visual register as the editor pane's tab strip across the splitter and will
//   follow STYLE-11's tab treatment when Phase 14 lands. The SHAPE is a plain full-width bar: no rounded ends, no tab
//   glyph, and a left-aligned title rather than a centred label -- it names a pane rather than selecting one.
//
//   IT IS STILL CLICKABLE, though, and reports the click rather than acting on it (see clicked()). Clicking a pane's
//   chrome is expected to give that pane the keyboard; the band was the one part of either pane that swallowed a click
//   silently, which reads as the application having missed it.
//
//   IT PAINTS ITSELF, and that is the point rather than an implementation detail. A QLabel dressed up with
//   autoFillBackground or a stylesheet would have to be told about every theme change, because a colour written onto a
//   widget is a colour Qt stops propagating to it. Reading the palette inside paintEvent means a theme switch is
//   already handled: the next repaint simply asks again.
//
//   Only the tree pane wears one. The editor pane's tab strip already occupies that band and says the same thing, so a
//   header above it would be a second row saying it twice.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>
#include <QStyleOptionTab>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QSize;

namespace vje
{
	//*****************************************************************************************************************
	// Class: PaneHeader
	//*****************************************************************************************************************

	class PaneHeader : public QWidget
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit PaneHeader ( const QString& title, QWidget* parent = nullptr );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		const QString& title () const;

		QSize sizeHint () const override;

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		void set_title ( const QString& title );

		// STYLE-14's question, pushed in rather than worked out here: does this band's pane hold the keyboard? Driven by
		// the pane's FocusHighlight, which is the application's single answer to it. The band takes the SELECTED tab's
		// surface in either state, so it stays in the same register as the editor pane's active tab across the splitter
		// (style/tab_surface.hpp).

		void set_pane_focused ( bool focused );

		bool pane_focused () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The band was clicked. Deliberately a SIGNAL rather than a focus move made here: this class is a titled band
		// and knows nothing about what its pane contains, so it reports the gesture and the pane decides what deserves
		// the keyboard. For the tree pane that is the tree itself.
		//
		// Left button only -- a right-click is left free for a future context menu on the band.

		void clicked ();

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		void paintEvent      ( QPaintEvent* event )      override;
		void mousePressEvent ( QMouseEvent* event )      override;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// The tab this header draws itself as -- shared by the painter and the size hint so the two cannot disagree.

		QStyleOptionTab tab_option () const;

		//=============================================================================================================
		// Fields
		//=============================================================================================================

	private:

		QString titleText;

		// Whether this band's pane holds the keyboard (STYLE-14). Starts false, which is the correct first paint: a
		// freshly built window has given the keyboard to nothing yet.

		bool paneFocused = false;
	};
}
