//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   PaneHeader implementation -- the band, the rule, and the title. See the header for why it paints rather than
//   dressing up a QLabel.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/PaneHeader.hpp"

#include "AppConfig.hpp"
#include "style/tab_surface.hpp"

#include <QColor>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QSize>
#include <QSizePolicy>

namespace vje
{
	namespace
	{
		// How far the rule is pulled from the band's own colour toward the text colour. Low enough to read as a
		// divider rather than a border.

		constexpr qreal RULE_INK_FRACTION = 0.15;

		// The rule is DERIVED from the two colours it sits between rather than themed as a constant of its own. That
		// keeps it correct in both themes from one expression -- and, more to the point, keeps it from claiming a
		// palette role: QPalette::Mid would have been the obvious home, but Fusion already shades real widgets with
		// it, so writing it here to get one line right would have quietly restyled everything else that reads it.

		QColor rule_colour ( const QPalette& colours )
		{
			const QColor band = colours.color ( QPalette::Window );
			const QColor ink  = colours.color ( QPalette::WindowText );

			const qreal bandFraction = 1.0 - RULE_INK_FRACTION;

			return QColor::fromRgbF
			(
				( band.redF   () * bandFraction ) + ( ink.redF   () * RULE_INK_FRACTION ),
				( band.greenF () * bandFraction ) + ( ink.greenF () * RULE_INK_FRACTION ),
				( band.blueF  () * bandFraction ) + ( ink.blueF  () * RULE_INK_FRACTION )
			);
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	PaneHeader::PaneHeader ( const QString& title, QWidget* parent )
		: QWidget    ( parent )
		, titleText  ( title )
	{
		// As wide as the pane gives it, and exactly as tall as its content needs -- a header that grew with the window
		// would eat the tree.

		setSizePolicy ( QSizePolicy::Preferred, QSizePolicy::Fixed );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	const QString& PaneHeader::title () const
	{
		return titleText;
	}

	QSize PaneHeader::sizeHint () const
	{
		// Measured through the STYLE, with the same CT_TabBarTab call a QTabBar makes for its own tabs -- so the band
		// is the height of the editor pane's tab strip by construction rather than by a constant kept in step by eye.

		const QFontMetrics metrics ( font () );

		QStyleOptionTab option = tab_option ();

		// The frame spacing is added BEFORE the style is asked, which is the order QTabBar::tabSizeHint uses -- and
		// getting it wrong is silent: sizeFromContents alone returns a plausible-looking size that is a third too
		// short, because most of a tab's height is this padding rather than anything the style adds afterwards.

		const int horizontalFrame = style ()->pixelMetric ( QStyle::PM_TabBarTabHSpace, &option, this );
		const int verticalFrame   = style ()->pixelMetric ( QStyle::PM_TabBarTabVSpace, &option, this );

		const QSize contents
		(
			metrics.horizontalAdvance ( titleText ) + horizontalFrame,
			metrics.height () + verticalFrame
		);

		const QSize tabSize = style ()->sizeFromContents ( QStyle::CT_TabBarTab, &option, contents, this );

		return QSize ( tabSize.width (), tabSize.height () + config::pane::HEADER_RULE_HEIGHT );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void PaneHeader::set_pane_focused ( bool focused )
	{
		if ( focused == paneFocused )
		{
			return;
		}

		paneFocused = focused;

		update ();
	}

	bool PaneHeader::pane_focused () const
	{
		return paneFocused;
	}

	void PaneHeader::set_title ( const QString& title )
	{
		if ( title == titleText )
		{
			return;
		}

		titleText = title;

		updateGeometry ();
		update ();
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void PaneHeader::paintEvent ( QPaintEvent* event )
	{
		Q_UNUSED ( event );

		// A full-width BAND wearing a tab's surface -- not a tab-shaped glyph (STYLE-13). Everything is read HERE, on
		// each repaint, so a theme switch is a non-event for this widget (see the header).

		QPainter painter ( this );

		const QRect band ( 0, 0, width (), height () - config::pane::HEADER_RULE_HEIGHT );

		painter.fillRect ( rect (), palette ().color ( QPalette::Window ) );

		// The SELECTED tab's surface, stretched the width of the pane (STYLE-13). It borrows the surface, not the shape.
		//
		// Taken from tab_surface() rather than from the style, and that is a CHANGE: this used to hand CE_TabBarTabShape
		// an over-wide rect so the rounded ends fell outside the visible area, because a selected tab's fill was buried
		// in a private Fusion helper and painting it was the only way to read it. The editor's strip no longer uses
		// Fusion's shading either -- it could not, since Fusion always draws the unselected tab darker and that is
		// backwards on a light theme -- so the two now share one named rule instead of one private implementation
		// detail. Which is the better arrangement regardless: what STYLE-13 actually asks for is that the band and the
		// active tab MATCH, and that is now a claim about a function rather than a hope about a gradient.

		painter.fillRect ( band, tab_surface ( palette (), true, paneFocused ) );

		// The base line closing the band off from the tree below it.

		painter.fillRect
		(
			QRect ( 0, band.bottom () + 1, width (), config::pane::HEADER_RULE_HEIGHT ),
			rule_colour ( palette () )
		);

		// The title is drawn here rather than through CE_TabBarTabLabel because it is a pane TITLE: left-aligned and
		// inset, where a tab label would be centred on its glyph. The surface is borrowed; the typography is not.
		//
		// Elided rather than clipped: the tree pane can be dragged down to 200 px, and a title cut mid-glyph reads as a
		// rendering fault where an ellipsis reads as a narrow pane.

		const QRect textArea = band.adjusted (  config::pane::HEADER_HORIZONTAL_PADDING, 0,
		                                       -config::pane::HEADER_HORIZONTAL_PADDING, 0 );

		const QFontMetrics metrics ( font () );

		const QString shownText = metrics.elidedText ( titleText, Qt::ElideRight, textArea.width () );

		painter.setPen ( palette ().color ( QPalette::WindowText ) );
		painter.drawText ( textArea, Qt::AlignLeft | Qt::AlignVCenter, shownText );
	}

	void PaneHeader::mousePressEvent ( QMouseEvent* event )
	{
		// Clicking a pane's chrome gives that pane the keyboard -- the same expectation a click on the editor pane's tab
		// strip now meets (NAV-04). Without it the band is the one part of either pane that swallows a click silently,
		// which reads as the application having missed it.
		//
		// Note the ASYMMETRY with the tab strip, which is deliberate. A tab strip is an interactive control, so a click
		// there focuses the strip and the arrow keys then move between tabs. This band is a static title with nothing to
		// arrow through, so focusing IT would strand the keyboard somewhere the arrow keys mean nothing -- the pane
		// hands the focus to its content instead.

		if ( event->button () == Qt::LeftButton )
		{
			emit clicked ();

			event->accept ();

			return;
		}

		QWidget::mousePressEvent ( event );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	QStyleOptionTab PaneHeader::tab_option () const
	{
		QStyleOptionTab option;

		option.initFrom ( this );

		option.text  = titleText;
		option.shape = QTabBar::RoundedNorth;

		// The SELECTED tab's shading, matching the editor pane's active tab across the splitter -- an unselected tab's
		// shading would make the pane read as switched off.

		option.position         = QStyleOptionTab::OnlyOneTab;
		option.selectedPosition = QStyleOptionTab::NotAdjacent;
		option.state           |= QStyle::State_Selected;

		// Matching EditorPane's tabWidget->setDocumentMode(true). Document-mode tabs are drawn flatter, and a header
		// that missed this would be subtly the wrong shape beside the one it is meant to match.

		option.documentMode = true;

		// HasFrame is what a QTabBar sets for itself when it sits inside a QTabWidget, and it decides WHICH PALETTE
		// ROLE the fill is derived from: with it, Fusion builds the tab surface out of Button, exactly as the editor
		// pane's strip does. Without it the fill falls back to Window instead -- silently, and to a colour that still
		// looks like a plausible header while being nothing like the tab it is supposed to match (measured: #2E2E31
		// against the tab's #4D4D4D). Comparing the two rendered pixels is the only thing that catches it.

		option.features = QStyleOptionTab::HasFrame;

		return option;
	}
}
