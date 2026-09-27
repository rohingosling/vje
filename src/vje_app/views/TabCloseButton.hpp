//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   TabCloseButton -- the x on an editor tab (VIEW-03, STYLE-11).
//
//   WHY OURS RATHER THAN THE TOOLKIT'S. QTabBar's own close button paints through PE_IndicatorTabClose, which
//   QCommonStyle draws from standardIcon ( SP_TabCloseButton ) -- a PNG baked into Qt at a literal #EA8F60. Measured
//   against a palette with all twenty roles forced to green it does not move, so it is the one element on the tab strip
//   that ignores the theme entirely: it follows neither Light/Dark nor SET-13's Interface style, and it is loudest on
//   the CURRENT tab, where it is most visible. Replacing the widget is the only one of the three available routes that
//   keeps the styling where this codebase already puts it -- an IconLibrary glyph, tinted (or not) by the family's own
//   rule -- rather than putting appearance into FluentStyle, whose contract is shape and metrics, never colour.
//
//   IT KEEPS THE TOOLKIT'S MODE RULE, DELIBERATELY. QCommonStyle picks QIcon::Disabled whenever a tab is neither
//   selected, hovered nor pressed, which is why the toolkit's x is grey on the unselected tabs and full strength on the
//   current one. That behaviour is worth keeping -- three tabs each carrying a full-strength x is three things asking
//   to be clicked -- so this class reproduces it exactly, and the only thing that changed is the artwork. What
//   "greyed" MEANS is then the icon family's own business: Fluent tints from the palette's disabled group, Classic
//   fades its artwork at config::icons::DISABLED_ARTWORK_OPACITY, and neither is restated here.
//
//   IT DOES NOT KNOW WHICH TAB IT IS ON. The owner tells it (set_on_current_tab), because the alternative -- asking the
//   parent QTabBar for its own index -- means a widget deriving its identity from its position in a list that is
//   rebuilt on every applicable-set change. EditorPane already knows which tab is current and already rebuilds the
//   strip; this is one call in each of those two places.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QAbstractButton>
#include <QIcon>
#include <QSize>

namespace vje
{
	//*****************************************************************************************************************
	// Class: TabCloseButton
	//*****************************************************************************************************************

	class TabCloseButton : public QAbstractButton
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// buttonSize is the widget's own extent and glyphSize the artwork drawn centred inside it. They differ for the
		// reason QCommonStyle's do: the button is the click target, sized to what CT_TabBarTab reserves for it, while
		// the glyph is one of the icon set's authored sizes and must not be scaled off its ladder (lesson Q28).

		TabCloseButton
		(
			const QSize& buttonSize,
			const QSize& glyphSize,
			QWidget*     parent = nullptr
		);

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// The artwork, re-supplied whenever IconLibrary re-tints (a theme change, or a change of Interface style).

		void set_icon ( const QIcon& icon );

		// Whether this button's tab is the current one. Drives the Normal / Disabled distinction -- see the header
		// note on why the button is told rather than asked.

		void set_on_current_tab ( bool onCurrentTab );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		QSize sizeHint () const override;

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		void paintEvent  ( QPaintEvent* event ) override;

		void enterEvent  ( QEnterEvent* event ) override;
		void leaveEvent  ( QEvent*      event ) override;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QIcon glyph;

		QSize buttonSize;
		QSize glyphSize;

		bool onCurrentTab = false;
	};
}
