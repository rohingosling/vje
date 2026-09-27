//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonPathView -- the JSONPath Query view (QUERY-01..07): a query box with its results above it, each result
//   clicking through to the node it names.
//
//   THE FIRST VIEW THAT ANSWERS TO THE DOCUMENT RATHER THAN TO A NODE. A query begins at "$", so its provider's
//   can_present() ignores the node it is handed and asks whether there is a document at all -- which needed no change
//   to IEditorViewProvider, because "applicable" was always the provider's own question and nothing in the contract
//   ever said it had to be about the selection. present() is correspondingly a no-op: moving the tree selection must
//   not re-run a query, re-scroll a result list, or disturb a half-typed expression.
//
//   IT DECIDES ALMOST NOTHING, AND THAT IS DELIBERATE. Everything worth a test about JSONPath -- which constructs are
//   accepted, what each selects, what a filter comparison means, where an error sits -- is JsonPathQuery, in vje_core,
//   tested without a widget. What is left here is three widgets and a staleness flag. There is no controller of the
//   FindController kind because there is nothing for one to hold: a result list is CHOSEN FROM rather than stepped
//   through, so there is no cursor into it, no wrap at the ends, and no re-anchoring of a current match across an edit.
//
//   THE RESULTS ARE A SNAPSHOT (QUERY-07). An array element's pointer token IS its position, so an edit renumbers
//   results taken before it. Any document change marks them stale and the summary says so; the query re-runs on the
//   next question asked -- running it again, choosing a result, or the tab becoming visible -- and never on the
//   document's behalf, so an editing session that is not a query session costs a flag per edit rather than a walk
//   (NFR-03). Staleness is RESOLVED rather than announced: with no report of its own the view has nowhere to say it
//   that would not be a message from a tab the user may not be looking at, landing on top of the one the edit itself
//   just posted. A re-run never moves the selection; only choosing a result does. A document RESET is different in kind
//   rather than in degree: the previous document's pointers name nothing, so the results are dropped outright.
//
//   AN ERROR LEAVES THE PREVIOUS RESULTS STANDING (QUERY-05). Replacing seventeen rows with an empty list on a typo
//   reads as "your query now matches nothing", which is a different and wrong answer. The message goes to the STATUS
//   BAR and the offending text is SELECTED in the query box: the sentence explains and the selection locates, and it is
//   the second half that has to be in the pane.
//
//   ENTER RUNS, SHIFT+ENTER BREAKS THE LINE, which is the other way round from a text editor and is why the query box
//   is a text box at all: the bottom half of a splitter needs something that can use the space, and whitespace between
//   tokens is insignificant (QUERY-02) so a query broken across lines is still one query.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/IEditorView.hpp"

#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/JsonPathQuery.hpp>

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QMenu;
class QPlainTextEdit;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QPoint;

namespace vje
{
	class Card;
	class ClipboardService;
	class JsonDocument;
	class SelectionService;
	class SettingsStore;
	class StatusService;

	//*****************************************************************************************************************
	// Class: JsonPathView
	//*****************************************************************************************************************

	class JsonPathView : public QWidget, public IEditorView
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The settings store may be null, in which case a result's value preview uses the SET-03 default notation; the
		// status service may be null, which suppresses the status-bar half of QUERY-06's reporting and nothing else.

		JsonPathView
		(
			JsonDocument*     document,
			SelectionService* selection,
			SettingsStore*    settings  = nullptr,
			StatusService*    status    = nullptr,
			ClipboardService* clipboard = nullptr,
			QWidget*          parent    = nullptr
		);

		// Called whenever the RESULT SET changes -- run, re-run, or a document reset that drops it. MainWindow uses it
		// to re-evaluate Edit > Copy JSONPath Result's enablement (QUERY-08), which no existing trigger covers: a query
		// changes neither the selection, the document, the undo stack, the focus nor the clipboard.
		//
		// A callback rather than a signal because the view is created and destroyed by EditorPane as its tab comes and
		// goes, so there is no stable instance for the window to connect to; the PROVIDER carries this down to each
		// view it builds, which is how FormViewProvider already hands NodeContextActions across.

		void set_results_changed_callback ( std::function<void ()> callback );

		//=============================================================================================================
		// IEditorView
		//=============================================================================================================

	public:

		QWidget* widget () override;

		// A NO-OP, and the only view for which that is the whole implementation. The view shows a query's results, not
		// a node, so a selection change has nothing to say to it (QUERY-01).

		void present ( const JsonPointer& pointer, SelectionOrigin origin ) override;

		// The tab became visible: a good moment to pay for a re-run the document made necessary while it was not
		// (QUERY-07). This is the laziness paying off rather than an exception to it.

		void view_activated () override;

		// The keyboard lands in the QUERY BOX, because that is where work on this view starts. Landing in a view is
		// never an activation gesture (EDITOR-04), and there is nothing here that could be activated by arriving.

		void take_focus () override;

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public:

		// Compile and evaluate what is in the query box (QUERY-05 / QUERY-06). An error is reported in place and the
		// previous results are left alone; an empty query clears both the results and the report.

		void run_query ();

		// Set the query box's text without running it. For the offscreen suite, and for any caller that wants to put a
		// query in front of the user rather than answer it.

		void set_query_text ( const QString& text );

		// QUERY-08. Puts every result's pointer on the clipboard, one per line, through set_plain_text -- see the
		// naming rule at the head of ClipboardService.hpp for why a list of names must not carry the private format.
		// Answers false and touches nothing when there is nothing to copy, so a refused command never destroys what
		// the user had on the clipboard already.
		//
		// A stale result set is refreshed FIRST: copying is one of QUERY-07's questions, so what lands on the clipboard
		// is what a re-run would produce rather than what the pane happened to be showing.

		bool copy_results ();

		// QUERY-09's two row commands, each acting on the pointer under the cursor rather than on a selection.

		bool go_to_result        ( const JsonPointer& pointer );
		bool copy_result_pointer ( const JsonPointer& pointer );

		// Fill a menu for the row under the cursor, or for none when `item` is null (QUERY-09).
		//
		// Split from the handler that shows it because QMenu::exec BLOCKS and cannot be driven offscreen, so the
		// menu's shape -- which commands, in which order, and enabled or not -- is assertable only while building it
		// is separable from showing it. TreeViewPane's context menu is split for exactly this reason.

		void populate_results_menu ( QMenu& menu, QTreeWidgetItem* item );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		QString query_text () const;

		// The results the list is currently showing. Not necessarily the answer for the document as it stands now --
		// see results_are_stale().

		const std::vector<JsonPointer>& results () const;

		// Has the document moved under the results since they were taken (QUERY-07)?

		bool results_are_stale () const;

		// Is there anything to copy (QUERY-08)? What Edit > Copy JSONPath Result's enablement asks.

		bool has_results () const;

		// Every result's pointer, one per line, exactly as QUERY-08 puts it on the clipboard. Named separately from
		// the command so the TEXT is assertable without a clipboard in the test.

		QString results_as_pointer_text () const;

		// The last compile error, or an empty message when the last run succeeded (or when the query was empty, which
		// is not a mistake and therefore not an error).

		const JsonPathError& last_error () const;

		// What the status bar is told: the count, the error, or an empty string meaning there is nothing to say (an
		// empty query, or a view that has not run one). Named rather than inlined so the WORDING is one statement, and
		// so a test can read it without going through the status service.

		QString report () const;

		// The widgets, for the offscreen suite. What this view DOES is what it puts in these and what it asks of the
		// selection service, and neither is observable any other way.

		QTreeWidget*    results_view () const;
		QPlainTextEdit* query_box    () const;
		QSplitter*      splitter     () const;

		// The two pane surfaces (STYLE-01/02/05). Exposed because SET-12's corner discipline is a property OF THEM, and
		// a test of "does this view answer to Interface style" has nowhere else to ask.

		Card* results_card () const;
		Card* query_card   () const;

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		// Any node change: the pointers may no longer name what they named (QUERY-07). Marks stale and re-reports; it
		// deliberately does not re-run.

		void handle_document_changed ();

		// A new root. The previous document's results name nothing at all, so they go rather than going stale.

		void handle_document_reset ();

		// A row was chosen -- a click, or Enter on the highlighted row (QUERY-06).

		void handle_result_chosen ( QTreeWidgetItem* item, int column );

		// QUERY-09. The menu's shape depends on whether a ROW sits under the cursor, not on what is selected.

		void handle_results_context_menu ( const QPoint& position );

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// Re-run the last query if the document has moved under it. Answers whether anything was re-run, which the
		// caller uses only to decide whether to re-report.

		bool refresh_if_stale ();

		void show_results ();                              // Fill the list from `resultPointers`.
		void update_report ();                             // Post report() to the status bar (QUERY-06).

		// Mark the query box's error span (QUERY-05). Selecting rather than merely reporting is what makes the message
		// actionable in a long query, and it is why JsonPathError carries a position at all.

		void mark_error_in_query_box ();

		QString value_preview ( const JsonPointer& pointer ) const;

		//=============================================================================================================
		// Events
		//=============================================================================================================

	protected:

		// Watches the query box for Enter (run) and Shift+Enter (line break). QPlainTextEdit answers only the
		// unmodified Return itself, and it answers it by inserting a paragraph -- which is the behaviour being
		// reversed here, so it has to be intercepted rather than connected to.

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		SelectionService* selection;
		SettingsStore*    settings;
		StatusService*    status;
		ClipboardService* clipboard;

		//=============================================================================================================
		// Data Members -- widgets (parent-owned).
		//=============================================================================================================

	private:

		QSplitter*      resultSplitter = nullptr;
		Card*           resultsCard    = nullptr;
		Card*           queryCard      = nullptr;
		QTreeWidget*    resultsList    = nullptr;
		QPlainTextEdit* queryEdit      = nullptr;

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		// The query the current results came from -- which is NOT necessarily what is in the box, since the user may
		// have typed on since running it. A re-run re-runs THIS, not the box's contents, because a stale refresh must
		// not silently answer a question the user has not asked yet.

		QString lastRunText;

		std::vector<JsonPointer> resultPointers;

		// Distinguishes "ran and found nothing" from "has not run at all" -- the two report differently, and only the
		// first is an answer about the document. Deliberately NOT named hasResults: that is the question has_results()
		// answers, which is "is there anything to copy" and is about the LIST rather than about having run.

		bool          hasRun = false;
		bool          stale  = false;
		JsonPathError lastCompileError;

		// Set while a row is being chosen and released on the NEXT turn of the event loop, because a double click
		// emits itemClicked twice and itemActivated once and all three are the same single choice.

		bool choosingResult = false;

		// Invoked on every change to the result set; see set_results_changed_callback.

		std::function<void ()> resultsChanged;
	};

	//*****************************************************************************************************************
	// Class: JsonPathViewProvider
	//*****************************************************************************************************************

	class JsonPathViewProvider : public IEditorViewProvider
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		JsonPathViewProvider
		(
			JsonDocument*     document,
			SelectionService* selection,
			SettingsStore*    settings  = nullptr,
			StatusService*    status    = nullptr,
			ClipboardService* clipboard = nullptr
		);

		// Handed to every view this provider builds (see JsonPathView::set_results_changed_callback).

		void set_results_changed_callback ( std::function<void ()> callback );

		//=============================================================================================================
		// IEditorViewProvider
		//=============================================================================================================

	public:

		QString view_id       () const override;
		QString display_name  () const override;
		QString icon_name     () const override;
		int     display_order () const override;

		// IGNORES ITS ARGUMENT, which is the point (QUERY-01). Every other provider answers about the selected node;
		// this one answers about the DOCUMENT, so its tab is there whenever a document is and does not come and go as
		// the selection moves.

		bool can_present ( const JsonNode* node ) const override;

		IEditorView* create_view ( QWidget* parent ) const override;

		//=============================================================================================================
		// Constants
		//=============================================================================================================

	public:

		static const QString VIEW_ID;

		static constexpr int DISPLAY_ORDER = 3;            // After Form, Text and Code (EDITOR-01).

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		SelectionService* selection;
		SettingsStore*    settings;
		StatusService*    status;
		ClipboardService* clipboard;

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		// Handed to every view this provider builds, so the window's hook survives the view being destroyed and
		// rebuilt with its tab (VIEW-01).

		std::function<void ()> resultsChanged;
	};
}
