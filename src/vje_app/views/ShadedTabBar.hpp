//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   ShadedTabBar / ShadedTabWidget -- the editor pane's tab strip, painting its own tab surfaces from
//   style/tab_surface.hpp rather than letting Fusion derive them (STYLE-14).
//
//   WHY IT PAINTS. Fusion draws the UNSELECTED tab darker than the selected one, always, from a single QPalette::Button
//   value. Against dark content that is right; against white content it is backwards, leaving the unselected tabs
//   heavier than the selected one and the active view the hardest tab to find. One Button colour feeds both fills
//   through a fixed relationship, so no palette value can invert it -- the strip has to be painted. See
//   tab_surface.hpp for the rule that replaces it.
//
//   ONLY THE SURFACE IS OURS. The label -- text, icon, elision, the disabled and hovered states -- still goes through
//   the style (CE_TabBarTabLabel), and every metric still comes from FluentStyle. This class supplies the fill and the
//   focus marker and nothing else, which is what keeps FluentStyle's metrics-only contract intact instead of moving colour into the proxy style.
//
//   THE FOCUS MARKER answers item two of the same change: a tab click now gives the STRIP the keyboard, so the arrow
//   keys move between tabs. That is invisible unless it is drawn, so while the bar itself holds focus the current tab
//   carries an accent marker -- the affordance that says "the arrows are driving the tabs, not the document".
//
//   ShadedTabWidget exists only because QTabWidget::setTabBar is PROTECTED: a custom bar cannot be installed from
//   outside. It does nothing else.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QTabBar>
#include <QTabWidget>

class QPaintEvent;

namespace vje
{
	//*****************************************************************************************************************
	// Class: ShadedTabBar
	//*****************************************************************************************************************

	class ShadedTabBar : public QTabBar
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit ShadedTabBar ( QWidget* parent = nullptr );

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		// STYLE-14's question, pushed in rather than worked out here: does the pane this strip belongs to hold the
		// keyboard? Driven by the pane's FocusHighlight, which is the application's single answer to it and already
		// carries the rules that make it non-obvious (a cell editor is a descendant, not the view itself).

		void set_pane_focused ( bool focused );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		bool pane_focused () const;

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		void paintEvent ( QPaintEvent* event ) override;

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		bool paneFocused = false;
	};

	//*****************************************************************************************************************
	// Class: ShadedTabWidget
	//*****************************************************************************************************************

	class ShadedTabWidget : public QTabWidget
	{
		Q_OBJECT

	public:

		explicit ShadedTabWidget ( QWidget* parent = nullptr );

		// The bar, already typed. QTabWidget::tabBar() returns a QTabBar*, and every caller here wants the derived one.

		ShadedTabBar* shaded_tab_bar () const;
	};
}
