//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FindBar -- the find bar docked above the editor pane (FIND-01..03): a query field, Match Case, previous / next, the
//   match count, and a close button. Hidden until Ctrl+F, dismissed by Esc.
//
//   IT DECIDES NOTHING. Every question Find actually poses -- what matches, where the cursor is in the match list, what
//   wrapping does, what an edit to the document does to a list of pointers -- belongs to FindController and is tested
//   without a display. This class turns gestures into controller calls and renders FindController::report(). If a
//   behaviour here needs a comment about search semantics, it is in the wrong file.
//
//   WHY IT IS INSIDE THE EDITOR CARD RATHER THAN A QDockWidget or a toolbar. FIND-01 puts it above the editor pane, and
//   the pane is one half of a splitter: a dock or a window-wide strip would span the tree as well, which would say the
//   bar belongs to the window rather than to what is being edited. Sitting in the card above the tab strip also keeps it
//   OUT of the two panes PaneCycler knows about, so Tab inside the bar is ordinary control-to-control navigation and
//   never jumps to a pane mid-query (NAV-04) -- and Tab from the workspace never lands on the bar, which is NAV-05's
//   rule for chrome.
//
//   ESCAPE DISMISSES BUT DOES NOT CLEAR. The query survives a dismissal, so F3 keeps repeating the last search with the
//   bar out of the way -- and re-opening shows what was searched for, selected, so typing replaces it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace vje
{
	class FindController;
	class IconLibrary;

	//*****************************************************************************************************************
	// Class: FindBar
	//*****************************************************************************************************************

	class FindBar : public QWidget
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The icon library may be null, in which case the three buttons show their text alone -- the form the headless
		// tests use.

		FindBar ( FindController* controller, IconLibrary* icons, QWidget* parent = nullptr );

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public slots:

		// Edit > Find... (Ctrl+F). Shows the bar, takes the keyboard, and SELECTS the existing query so the next
		// keystroke replaces it. Invoked while already open it re-focuses and re-selects without disturbing the
		// remembered focus origin -- Ctrl+F twice must not make the bar its own way back.

		void open ();

		// Esc, or the close button. Hides the bar and hands the keyboard back; the query is kept (see the header note).

		void dismiss ();

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		// The widget that held the keyboard when the bar was opened, or null. MainWindow decides what to do with it on
		// dismissal -- the bar knows where the keyboard CAME from, and the window knows where it should go when that
		// widget is no longer a sensible destination.

		QWidget* focus_origin () const;

		// The widgets, for the offscreen suite. The bar's behaviour is what it does to the controller and to its own
		// label, and both are only observable through these.

		QLineEdit* query_field  () const;
		QCheckBox* match_case_box () const;
		QLabel*    count_label  () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		void dismissed ();   // The bar has hidden itself; the keyboard needs somewhere to go.

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		// Esc reaches here from the check box and the buttons, which do not consume it. The query field does not let it
		// through on every platform, which is why there is an event filter as well.

		void keyPressEvent ( QKeyEvent* event ) override;

		// Watches the query field for Return / Shift+Return (next / previous, FIND-02) and Esc. A QLineEdit's own
		// returnPressed() cannot answer this: it reports that Return arrived and not which modifiers came with it.

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_query_edited  ();   // Live search as the user types (FIND-01).
		void handle_results_changed ();   // Re-read the controller's report into the label.
		void handle_icons_changed ();

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void apply_icons ();

		// Push the query field and the check box into the controller as ONE query. They are one setting in two widgets:
		// toggling Match Case with text already typed has to re-run the same search, not start a new one.

		void submit_query ();

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		FindController* controller;
		IconLibrary*    icons;

		//=============================================================================================================
		// Data Members -- widgets (parent-owned).
		//=============================================================================================================

	private:

		QLineEdit*   queryField     = nullptr;
		QCheckBox*   matchCaseBox   = nullptr;
		QToolButton* previousButton = nullptr;
		QToolButton* nextButton     = nullptr;
		QLabel*      countLabel     = nullptr;
		QToolButton* closeButton    = nullptr;

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		// Where the keyboard was when the bar opened. A QPointer because the widget can be destroyed while the bar is
		// open -- a Code View tab rebuilt under it is exactly that (EDITOR-10).

		QPointer<QWidget> focusOrigin;
	};
}
