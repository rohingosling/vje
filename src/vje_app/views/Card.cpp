//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Card implementation. See Card.hpp for why the card is painted rather than styled, and why its corners are drawn in
//   an overlay above the content instead of by masking the widget.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/Card.hpp"

#include "AppConfig.hpp"
#include "style/card_surface.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace vje
{
	namespace
	{
		// The card's outline, inset by half a pixel so a one-pixel pen strokes ON the boundary rather than straddling
		// it. Shared by the fill and the overlay, so the two cannot describe different shapes.
		//
		// THE TOP AND BOTTOM CORNERS TAKE DIFFERENT RADII (config::card, which carries the reason: the bottom edge is
		// where an item view puts its horizontal scroll bar, and a fillet slices its ends off). That rules out
		// addRoundedRect, which takes one radius for all four, so the path is walked corner by corner -- anticlockwise
		// from the bottom left, which is where a closed subpath can return to without an extra segment.
		//
		// Each arc is inscribed in a box of its own diameter sitting in the corner it rounds, and a zero radius
		// degenerates to a point on that corner. That is what makes ALL THREE shapes one path with no branching: the
		// filleted card, the square-topped card SET-03 lets the user choose, and the uniform card you get by setting
		// the two radii equal.

		QPainterPath card_outline ( const QRect& bounds, bool topCornersRounded )
		{
			const qreal inset = config::card::BORDER_WIDTH / 2.0;

			const QRectF edge = QRectF ( bounds ).adjusted ( inset, inset, -inset, -inset );

			// SET-03: the top radius is the user's choice, and a zero radius already degenerates to a point on its
			// own corner -- so the square case needs no branch of its own, here or anywhere below.

			const qreal top    = topCornersRounded ? config::card::TOP_CORNER_RADIUS : 0.0;
			const qreal bottom = config::card::BOTTOM_CORNER_RADIUS;

			QPainterPath outline;

			// Up the left side, from just above the bottom-left corner.

			outline.moveTo ( edge.left (), edge.bottom () - bottom );
			outline.lineTo ( edge.left (), edge.top () + top );

			// Top left: from 9 o'clock round to 12.

			outline.arcTo ( QRectF ( edge.left (), edge.top (), top * 2.0, top * 2.0 ), 180.0, -90.0 );

			outline.lineTo ( edge.right () - top, edge.top () );

			// Top right: from 12 o'clock round to 3.

			outline.arcTo ( QRectF ( edge.right () - ( top * 2.0 ), edge.top (), top * 2.0, top * 2.0 ), 90.0, -90.0 );

			// Down the right side.

			outline.lineTo ( edge.right (), edge.bottom () - bottom );

			// Bottom right: 3 o'clock round to 6. A zero radius makes this a no-op on the corner itself.

			outline.arcTo
			(
				QRectF ( edge.right () - ( bottom * 2.0 ), edge.bottom () - ( bottom * 2.0 ), bottom * 2.0, bottom * 2.0 ),
				0.0, -90.0
			);

			outline.lineTo ( edge.left () + bottom, edge.bottom () );

			// Bottom left: 6 o'clock round to 9, closing on the point the path started from.

			outline.arcTo
			(
				QRectF ( edge.left (), edge.bottom () - ( bottom * 2.0 ), bottom * 2.0, bottom * 2.0 ),
				270.0, -90.0
			);

			outline.closeSubpath ();

			return outline;
		}

		//*************************************************************************************************************
		// Class: CardOverlay
		//*************************************************************************************************************
		//
		// The corner wedges and the border, painted ABOVE the card's content (Card.hpp). Local to this file: it is a
		// piece of how a Card draws itself, not a widget anything else can use.
		//
		// It reads the colours from its PARENT's palette rather than its own. The two are the same in every ordinary
		// case, but a palette written onto the card (FocusHighlight does exactly that to other widgets) reaches the
		// card and its children alike -- and the card is the thing being drawn, so the card is the thing to ask.
		//*************************************************************************************************************

		class CardOverlay : public QWidget
		{
		public:

			explicit CardOverlay ( QWidget* parent )
				: QWidget ( parent )
			{
				// Nothing about hit-testing changes: a click near a corner reaches whatever is underneath.

				setAttribute ( Qt::WA_TransparentForMouseEvents );

				// No background of its own -- everything it draws, it draws in paintEvent.

				setAttribute ( Qt::WA_NoSystemBackground );
			}

		protected:

			void paintEvent ( QPaintEvent* event ) override
			{
				Q_UNUSED ( event );

				const Card* card = qobject_cast<const Card*> ( parentWidget () );

				if ( card == nullptr )
				{
					return;
				}

				QPainter painter ( this );

				painter.setRenderHint ( QPainter::Antialiasing, true );

				const QPainterPath outline = card_outline ( rect (), card->top_corners_rounded () );

				// The four wedges between the square widget and its rounded outline, filled with the colour the
				// workspace paints AROUND the card -- so the corner reads as the card not being there, which is what a
				// rounded corner is.

				QPainterPath bounds;

				bounds.addRect ( QRectF ( rect () ) );

				painter.fillPath ( bounds.subtracted ( outline ), card->palette ().color ( QPalette::Window ) );

				// The border last, so it closes over the wedges it shares an edge with.

				painter.strokePath ( outline, QPen ( card_border ( card->palette () ), config::card::BORDER_WIDTH ) );
			}
		};
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	Card::Card ( QWidget* parent )
		: QWidget ( parent )
	{
		contentLayout = new QVBoxLayout ( this );

		// The content is inset by the border alone. Anything more would be a margin inside a card that already sits
		// inside the workspace's own margin (STYLE-03), and the panes carry their own spacing.

		const int border = config::card::BORDER_WIDTH;

		contentLayout->setContentsMargins ( border, border, border, border );
		contentLayout->setSpacing         ( 0 );

		cornerOverlay = new CardOverlay ( this );

		cornerOverlay->setGeometry ( rect () );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void Card::add_content ( QWidget* content )
	{
		contentLayout->addWidget ( content );

		// Every added child is stacked above its siblings, so the overlay has to climb back to the top after each one.
		// Raising once in the constructor would leave it under everything the card was actually given.

		cornerOverlay->raise ();
	}

	void Card::set_top_corners_rounded ( bool rounded )
	{
		if ( rounded == topCornersRounded )
		{
			return;
		}

		topCornersRounded = rounded;

		// BOTH surfaces repaint: the card draws the fill and the overlay draws the corners and the border, so a change
		// that only reached one of them would leave a filleted fill inside a square border, or the reverse.

		update ();

		cornerOverlay->update ();
	}

	bool Card::top_corners_rounded () const
	{
		return topCornersRounded;
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void Card::paintEvent ( QPaintEvent* event )
	{
		Q_UNUSED ( event );

		QPainter painter ( this );

		painter.setRenderHint ( QPainter::Antialiasing, true );

		// The surface only shows where the content does not cover it, which on a laid-out card is the border inset and
		// whatever a pane leaves spare. It is still filled rather than left to the backdrop: STYLE-01's claim is that
		// the card IS a surface, and a gap showing the window through it would be exactly the composition the
		// requirement rules out.

		painter.fillPath ( card_outline ( rect (), topCornersRounded ), card_surface ( palette () ) );
	}

	void Card::resizeEvent ( QResizeEvent* event )
	{
		QWidget::resizeEvent ( event );

		cornerOverlay->setGeometry ( rect () );
	}
}
