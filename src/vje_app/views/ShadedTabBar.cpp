//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   ShadedTabBar / ShadedTabWidget implementation. See the header for why the surface is painted here and the label is
//   not.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/ShadedTabBar.hpp"

#include "AppConfig.hpp"
#include "style/tab_surface.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QRect>
#include <QStyle>
#include <QStyleOptionTab>

namespace vje
{
	//=================================================================================================================
	// ShadedTabBar
	//=================================================================================================================

	ShadedTabBar::ShadedTabBar ( QWidget* parent )
		: QTabBar ( parent )
	{
		// Clicking a tab hands the STRIP the keyboard, which QTabBar's default Qt::TabFocus policy does not do -- a
		// click would switch the view and leave the caret wherever it was. StrongFocus makes the click a focus move as
		// well, so the arrow keys then walk the tabs and Tab still leaves the pane (NAV-04).

		setFocusPolicy ( Qt::StrongFocus );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void ShadedTabBar::set_pane_focused ( bool focused )
	{
		if ( focused == paneFocused )
		{
			return;
		}

		paneFocused = focused;

		update ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	bool ShadedTabBar::pane_focused () const
	{
		return paneFocused;
	}

	//=================================================================================================================
	// Events
	//=================================================================================================================

	void ShadedTabBar::paintEvent ( QPaintEvent* event )
	{
		Q_UNUSED ( event );

		QPainter painter ( this );

		// The strip's own backdrop is the unselected surface, so the area beyond the last tab does not fall through to
		// the window colour and leave the strip looking truncated.

		painter.fillRect ( rect (), tab_surface ( palette (), false, paneFocused ) );

		for ( int index = 0; index < count (); ++index )
		{
			QStyleOptionTab option;

			initStyleOption ( &option, index );

			const bool selected = ( index == currentIndex () );

			painter.fillRect ( option.rect, tab_surface ( palette (), selected, paneFocused ) );

			// The keyboard-focus marker (see the header). Bound to the BAR's own focus, not the pane's: the pane holds
			// focus whenever the grid does too, and marking the tab then would say the arrows move between tabs when
			// they are actually driving the document.

			if ( selected && hasFocus () )
			{
				const QRect marker
				(
					option.rect.left (),
					option.rect.bottom () - config::editor::TAB_FOCUS_MARKER_HEIGHT + 1,
					option.rect.width (),
					config::editor::TAB_FOCUS_MARKER_HEIGHT
				);

				painter.fillRect ( marker, palette ().color ( QPalette::Highlight ) );
			}

			// The label stays the style's: text, icon, elision, and the disabled and hovered states are all things
			// FluentStyle and Fusion already get right, and reimplementing them here would be a second set of rules to
			// keep in step for no gain. Only the surface is ours.

			style ()->drawControl ( QStyle::CE_TabBarTabLabel, &option, &painter, this );
		}
	}

	//=================================================================================================================
	// ShadedTabWidget
	//=================================================================================================================

	ShadedTabWidget::ShadedTabWidget ( QWidget* parent )
		: QTabWidget ( parent )
	{
		// The only reason this class exists: setTabBar is protected, so a custom bar cannot be installed from outside.

		setTabBar ( new ShadedTabBar ( this ) );
	}

	ShadedTabBar* ShadedTabWidget::shaded_tab_bar () const
	{
		return static_cast<ShadedTabBar*> ( tabBar () );
	}
}
