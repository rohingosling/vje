//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   FocusHighlight -- makes an item view's selection bar answer to KEYBOARD FOCUS, so its colour tells the user where
//   their next keystroke lands (STYLE-12). The pane holding focus shows the accent bar; every other pane shows the
//   muted one.
//
//   WHY THIS IS NOT SIMPLY A PALETTE ENTRY. Qt already carries both colours -- QPalette::Active and QPalette::Inactive
//   -- but it chooses between them by WINDOW activation, not by which widget holds focus: QStyleOption::initFrom sets
//   State_Active from QWidget::isActiveWindow, and the item-view styles read that. Left alone, the two panes dim
//   together when the application is deactivated and neither ever dims for the other. This class supplies the missing
//   half, and only that half: while the watched widget does not hold focus, it copies the Inactive selection colours
//   over the Active ones ON THAT WIDGET ALONE.
//
//   WHERE THE COLOURS LIVE. In ThemeService's palettes, as the Inactive group of each theme -- not here. One theme,
//   one place its selection colours are written, and the window-deactivated case keeps working with no help from this
//   class. All this decides is WHEN they apply.
//
//   THE DECISION IS A PURE FUNCTION (focus_adjusted_palette), and deliberately so. Real keyboard focus cannot be
//   driven under the offscreen platform the test suites run on -- a window never becomes active, so hasFocus() is
//   false however the test asks for it -- which would leave the whole rule untested if it lived inside the QObject.
//   Extracting it applies the rule directly: assert on what the code DECIDES, not on the symptom a headless run
//   cannot produce.
//
//   Deliberately NOT done in FluentStyle: that proxy style is metrics-only by contract,
//   so theming and layout stay independent. A style that reached for a colour would break the one rule that makes it
//   safe to change either alone.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QList>
#include <QObject>
#include <QPalette>

class QEvent;
class QWidget;

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// WHICH SURFACE a watcher governs. Both answer the same question -- does my pane hold the keyboard? -- and differ
	// only in what they recolour, so they are one mechanism with two role sets rather than two classes.
	//
	//   Selection -- the highlighted row or cell (STYLE-12).
	//   Surface   -- QPalette::Button, which is what a Fusion tab and every push button derive their fill from.
	//
	// NOTE, because the comment here used to claim more than it now does: the pane chrome (the Explorer band and the
	// editor's tab strip) no longer takes its fill from Button. Fusion always draws an unselected tab DARKER than the
	// selected one, which is backwards against white content, so both now paint from style/tab_surface.hpp instead --
	// and what they need from this class is the BOOLEAN (holds_focus / focus_state_changed), not a palette role. The
	// Surface role set remains correct for anything else in a pane that is drawn from Button.
	//-----------------------------------------------------------------------------------------------------------------

	enum class FocusRoles
	{
		Selection,
		Surface
	};

	QList<QPalette::ColorRole> palette_roles_for ( FocusRoles roles );

	//-----------------------------------------------------------------------------------------------------------------
	// The rule, as a pure function: the palette a widget should wear, given the one it has, the theme it should be
	// reading from, and whether its pane holds keyboard focus.
	//
	// Only the named roles are written, and only into the Active group -- the group a widget paints from while its
	// window is active. Everything else in `current` is passed through untouched, so a caller assigning the result to a
	// widget does not quietly freeze every other role against future theme changes.
	//-----------------------------------------------------------------------------------------------------------------

	QPalette focus_adjusted_palette
	(
		const QPalette& current,
		const QPalette& theme,
		bool            holdsFocus,
		FocusRoles      roles = FocusRoles::Selection
	);

	//*****************************************************************************************************************
	// Class: FocusHighlight
	//*****************************************************************************************************************

	class FocusHighlight : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// Attaches a watcher to the widget and parents it there, so the widget owns the lifetime and the caller has
		// nothing to keep. Install once, at construction; the returned pointer is for tests.
		//
		//   roles      -- which surface this watcher recolours.
		//   focusScope -- WHOSE focus decides it. Defaults to the widget itself, which is right for a view that both
		//                 takes the keyboard and shows the result. It is not right for pane chrome: the Explorer band
		//                 recolours when the TREE has focus, and the tab strip when the GRID does, so those name the
		//                 pane as their scope. Stated explicitly rather than relying on the palette propagating from a
		//                 parent, which is a mechanism this Qt build has already been caught not honouring.

		static FocusHighlight* install
		(
			QWidget*    widget,
			FocusRoles  roles      = FocusRoles::Selection,
			QWidget*    focusScope = nullptr
		);

		FocusHighlight ( QWidget* widget, FocusRoles roles = FocusRoles::Selection, QWidget* focusScope = nullptr );

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public:

		// Re-reads the theme and re-decides. Called for you on focus moves and theme changes; public because a test
		// should not have to fake an event to check the result.

		void refresh ();

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// Does the watched scope hold the keyboard? Public because this class is now the application's single answer to
		// that question, and two widgets PAINT from it rather than merely being recoloured by it: the editor's tab strip
		// and the tree's Explorer band derive their fills from style/tab_surface.hpp, which needs the boolean rather
		// than a palette role. Descendant focus counts -- see the implementation.

		bool holds_focus () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// Emitted only when the answer CHANGES, so a widget can repaint on the transition rather than on every focus
		// move in the application.

		void focus_state_changed ( bool holdsFocus );

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private:

		void handle_focus_changed ( QWidget* lost, QWidget* gained );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		bool belongs_to_us ( const QWidget* candidate ) const;

		void schedule_refresh ();

		//=============================================================================================================
		// Fields
		//=============================================================================================================

	private:

		QWidget* target = nullptr;

		// Whose focus the answer turns on. Often the target itself; for pane chrome, the pane around it.

		QWidget* scope = nullptr;

		FocusRoles governedRoles = FocusRoles::Selection;

		// Collapses the burst of events one theme application produces into a single re-read, and stops the palette
		// this class writes from being mistaken for a theme change and answered again.

		bool refreshPending = false;

		// The last answer published, so focus_state_changed() fires on the TRANSITION rather than on every refresh --
		// a theme change re-runs the whole decision and must not read as the keyboard having moved.

		bool lastFocusState = false;
	};
}
