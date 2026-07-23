//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   PaneCycler -- Tab and Shift+Tab move the keyboard between the workspace panes (NAV-04), forward and backward round
//   a ring of registered panes. With two panes both directions are the same toggle; the ring is the shape it will keep
//   when there is a third.
//
//   WHY NOT Qt's FOCUS CHAIN. setTabOrder describes a chain over every focusable widget, which here would thread the
//   tab bar, the scroll bars, and whatever a future view brings into the middle of a movement the user thinks of as
//   "the other pane". Naming the panes and moving between them says what is meant, and stays true as those widgets
//   come and go.
//
//   WHAT IT DELIBERATELY DOES NOT INTERCEPT, which is most of the work:
//
//     - Anything outside the registered panes. A dialog, a menu, the toolbar -- Tab there is ordinary widget
//       navigation and none of this class's business.
//     - Tab carrying a modifier other than Shift. Ctrl+Tab cycles the editor's view tabs (VIEW-01) and must pass.
//     - An OPEN CELL EDITOR. While a grid is editing, Tab is the editor's commit-and-advance (EDITOR-03) -- the
//       keyboard belongs to the cell, not the workspace, and a Tab that jumped panes mid-value would be a trap.
//     - A view that claims the key (IEditorView::claims_tab_key). Reserved for the Code View, where Tab indents
//       (EDITOR-07).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QList>
#include <QObject>

#include <functional>

class QEvent;
class QWidget;

namespace vje
{
	//*****************************************************************************************************************
	// Class: PaneCycler
	//*****************************************************************************************************************

	class PaneCycler : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit PaneCycler ( QObject* parent = nullptr );

		//=============================================================================================================
		// Registry
		//=============================================================================================================

	public:

		// Add a pane to the ring, in cycle order.
		//
		//   root       -- the pane's widget. Focus anywhere inside it counts as being in this pane.
		//   giveFocus  -- hands the keyboard to whatever this pane considers its landing spot. A pane knows that and
		//                 this class does not: the tree focuses its view, the editor pane focuses its VISIBLE view.
		//   claimsTab  -- optional; answering true takes the pane out of the cycle for as long as it is true. Left
		//                 empty, the pane always cycles.

		void add_pane
		(
			QWidget*                     root,
			std::function<void ()>       giveFocus,
			std::function<bool ()>       claimsTab = {}
		);

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public:

		// Move the keyboard one pane forward (or backward). Public so the behaviour can be driven directly by a test
		// -- the offscreen platform will not deliver a real Tab to a real focus widget.

		void cycle ( bool forward );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// Which registered pane contains the widget, or -1 for none. Also -1 when the ring is empty.

		int pane_index_of ( const QWidget* widget ) const;

		// Is this widget an open cell editor -- i.e. does the keyboard belong to a value being typed rather than to the
		// grid around it? Public and static because it is the one judgement in this class that a headless test can
		// actually make: the offscreen platform grants no focus, so the event path itself cannot be driven.

		static bool is_open_cell_editor ( const QWidget* widget );

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Fields
		//=============================================================================================================

	private:

		struct Pane
		{
			QWidget*               root      = nullptr;
			std::function<void ()> giveFocus;
			std::function<bool ()> claimsTab;
		};

		QList<Pane> panes;
	};
}
