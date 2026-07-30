//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Card -- a workspace pane's container surface (STYLE-01/02/05): an opaque fill, a one-pixel border closing it against
//   the window backdrop, and an ~8 px radius on its TOP corners. The tree pane and the editor pane each sit in one.
//
//   THE RADIUS IS PER EDGE, and neither half of that is arbitrary. The BOTTOM corners are always square, because a
//   pane's bottom edge is where an item view puts its horizontal scroll bar -- a full-width rectangular control the
//   toolkit draws -- and a fillet slices its ends off on the diagonal. The TOP corners are the USER's: SET-03's
//   "Rounded pane corners" (default on) squares them off for anyone who prefers it, since filleted and square are both
//   defensible and the preference divides. See config::card for both, and set_top_corners_rounded below for why the
//   answer is pushed in rather than read here.
//
//   IT IS PAINTED RATHER THAN STYLED IN QSS, which is a decision and not an accident. Setting a stylesheet on a widget
//   puts QStyleSheetStyle into the paint path for that widget's whole SUBTREE -- and this subtree holds ShadedTabBar,
//   PaneHeader, the two grids and the code editor, every one of which paints its own surface from a rule the theme
//   work has already settled (STYLE-11/13/14). That is the same coupling FluentStyle avoids for menus (architecture
//   9.1), and here it would put a second opinion underneath chrome that is already correct. Painting the card costs one
//   paintEvent and leaves every locked surface exactly as it is.
//
//   THE CORNERS NEED AN OVERLAY, AND THAT IS THE WHOLE OF THE COMPLEXITY HERE. Qt paints a parent before its children,
//   so a rounded card whose content is rectangular -- the Explorer band across the top of the tree card, the tab strip
//   across the top of the editor card -- shows those square corners painted straight over the arc. Three ways out, and
//   only one is right:
//
//     - Inset the content by the radius. Rejected: STYLE-13 asks for a FULL-WIDTH band, and insetting it would move
//       every pane's content 8 px in from the frame for the sake of two corners.
//     - QWidget::setMask with a rounded QRegion. Rejected by measurement: a region is a set of whole pixels, so the
//       corners come out aliased -- visibly stepped against the antialiased border drawn beside them.
//     - Paint the corners and the border ABOVE the content, in a transparent child raised over its siblings. Taken.
//       The wedges between the card's square rect and its rounded path -- two of them as the card is drawn today, none
//       at all with the fillet switched off -- are filled with the backdrop colour, which the card knows exactly (it is
//       the palette's Window role, the same colour the workspace margin around the card is painted in), so the result
//       is indistinguishable from the card having been drawn rounded in the first place.
//
//   The overlay is transparent to the mouse, so nothing about hit-testing changes: a click near a corner still reaches
//   whatever is under it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QWidget>

class QPaintEvent;
class QResizeEvent;
class QVBoxLayout;

namespace vje
{
	//*****************************************************************************************************************
	// Class: Card
	//*****************************************************************************************************************

	class Card : public QWidget
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit Card ( QWidget* parent = nullptr );

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// Stack a widget into the card, below whatever is already there. The editor card carries two (the find bar
		// above the editor pane), which is why this is a call per widget rather than a single content setter.

		void add_content ( QWidget* content );

		// SET-03's "Rounded pane corners" (STYLE-02): filleted top corners, or square ones. PUSHED IN rather than read
		// from a settings store here -- this is a general container widget and knows nothing about the application's
		// settings, the same reason TransferListEditor knows nothing about toolbars. It is also asked on every repaint,
		// which is not a question to answer with a store lookup (NFR-03).
		//
		// The BOTTOM corners are not covered by this and are always square: that is a rendering constraint rather than
		// a preference (see config::card).

		void set_top_corners_rounded ( bool rounded );

		bool top_corners_rounded () const;

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		void paintEvent  ( QPaintEvent*  event ) override;
		void resizeEvent ( QResizeEvent* event ) override;

		//=============================================================================================================
		// Fields
		//=============================================================================================================

	private:

		QVBoxLayout* contentLayout = nullptr;

		// Starts at the documented default so a card built before anything pushes a value into it already looks right
		// (config::card::ROUNDED_TOP_CORNERS_DEFAULT).

		bool topCornersRounded = true;

		// The corner-and-border overlay (see the header). Deliberately NOT in the layout: it covers the card rather
		// than taking a place in it.

		QWidget* cornerOverlay = nullptr;
	};
}
