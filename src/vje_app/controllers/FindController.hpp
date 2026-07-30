//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FindController -- the whole of Find and Go To (FIND-01..04) as POLICY, with no widget in it.
//
//   WHY IT IS NOT IN THE FIND BAR. What the find bar contains is a text field, a check box, and three buttons; what
//   Find actually IS is a small state machine over a list of pointers -- where the cursor sits in that list, what
//   wrapping does at each end, what a re-query does to the position the user had reached, and what an edit to the
//   document does to a list of pointers taken before it. Every one of those is worth a test and none of them needs a
//   display, so they live here and the bar renders what it is told.
//
//   THE MATCH LIST IS A SNAPSHOT, AND THE DOCUMENT CAN MOVE UNDER IT. SearchService returns JsonPointers, and an
//   element pointer's last token is a POSITION -- so a delete two rows above the current match silently renames it.
//   The controller therefore watches the document and marks its list STALE on any change, re-running the search on the
//   next question asked of it. The re-run is lazy for a reason: with the bar dismissed nobody asks, so an editing
//   session that is not a find session costs a flag per edit rather than a walk (NFR-03). The controller holds no
//   matches at all while the query is empty, which is what makes a dismissed bar free.
//
//   WHAT A RE-RUN DOES TO THE POSITION. The current match is kept BY POINTER when it is still a match; otherwise the
//   index is clamped into the new list, so "next" continues from roughly where the user was rather than jumping to the
//   top. A re-run never re-publishes the selection: the user is editing, and a find that dragged the selection around
//   under their hands would be unusable.
//
//   NAVIGATION PUBLISHES A SELECTION AND NOTHING ELSE (NAV-01). A match is delivered by writing the SelectionService
//   with SelectionOrigin::Find -- which reveals (EDITOR-04), so a match inside a collapsed branch opens it -- and the
//   tree, the editor pane, and the status bar all follow from there. Go To is the same channel with SelectionOrigin::
//   GoTo. Search never modifies the document (FIND-01).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonPointer.hpp>

#include <QObject>
#include <QString>

#include <vector>

namespace vje
{
	class ClipboardService;
	class JsonDocument;
	class SelectionService;
	class StatusService;

	//-----------------------------------------------------------------------------------------------------------------
	// What Edit > Go To did with the text it was given (FIND-04). The two failures are told apart because they mean
	// different things to the user: one says the text is not a pointer, the other says the document has no such node.
	//-----------------------------------------------------------------------------------------------------------------

	enum class GoToResult
	{
		Selected,           // Resolved; the node is now the selection.
		MalformedPointer,   // Not RFC 6901 text.
		Unresolvable        // Well-formed, but no node in this document answers to it.
	};

	// The message a failed Go To reports IN PLACE (FIND-04) -- stated here rather than in the dialog so the dialog and
	// its tests name the same wording. Selected has no message and returns an empty string.

	QString describe_go_to_result ( GoToResult result );

	//*****************************************************************************************************************
	// Class: FindController
	//*****************************************************************************************************************

	class FindController : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// status may be null, which suppresses the status-bar half of FIND-02's reporting and nothing else; clipboard
		// may be null, which makes copy_selection_pointer() a no-op that still answers truthfully.

		FindController
		(
			JsonDocument*     document,
			SelectionService* selection,
			StatusService*    status,
			ClipboardService* clipboard = nullptr,
			QObject*          parent    = nullptr
		);

		//=============================================================================================================
		// Commands -- the query (FIND-01)
		//=============================================================================================================

	public:

		// Set the needle and the Match Case option, re-run the search, and move to a match.
		//
		// The position rule: the user KEEPS their place when the node they are on is still a match (which is what makes
		// typing one more character feel like narrowing rather than restarting), and otherwise lands on the first match.
		// An empty needle stands the controller down entirely -- no matches, no report, no document watching, and the
		// selection is left exactly where it is.
		//
		// Answers whether the query has any matches.

		bool set_query ( const QString& text, bool matchCase );

		//=============================================================================================================
		// Commands -- navigation (FIND-02)
		//=============================================================================================================

	public:

		// Move to the next / previous match, wrapping at the document ends, and publish it as the selection. Both answer
		// false without touching the selection when there is nothing to move to (FIND-03).

		bool find_next     ();
		bool find_previous ();

		//=============================================================================================================
		// Commands -- Go To (FIND-04)
		//=============================================================================================================

	public:

		// Resolve RFC 6901 text against the document and select what it names. The empty string is the ROOT pointer and
		// is accepted as such -- that is what RFC 6901 says it means, and it is the only way to name the root. Nothing is
		// selected and nothing is reported unless the result is Selected.

		GoToResult go_to ( const QString& pointerText );

		//=============================================================================================================
		// Commands -- Copy JSON Pointer (FIND-05)
		//=============================================================================================================

	public:

		// Put the SELECTED node's JSON Pointer on the clipboard as plain text -- exactly the text go_to() accepts, so a
		// pointer copied here pastes straight back into the Go To dialog after any amount of work elsewhere. It lives
		// beside Go To because it is Go To's inverse, and one file holding both is what keeps the two spellings from
		// drifting apart.
		//
		// Plain text ONLY: setting the private node format would make the next Ctrl+V paste the node (ClipboardService::
		// set_plain_text). Answers false when nothing is selected, having touched nothing.
		//
		// The ROOT's pointer is the empty string (RFC 6901), so a SUCCESSFUL copy can legitimately leave the clipboard
		// empty. That is reported distinctly rather than passed off as an ordinary copy -- it round-trips correctly (an
		// empty Go To field means the root) but it does not look like it worked.

		bool copy_selection_pointer ();

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		const QString& query      () const;
		bool           match_case () const;

		// How many nodes match (FIND-02's count is a NODE count -- a node whose key and value both match is one match).

		int match_count () const;

		// The current match's 1-based position, or 0 when no match is current. Only ever 0 with an empty match list,
		// since every path that produces matches also lands on one.

		int current_position () const;

		// The current match's pointer. Meaningful only while current_position() is non-zero.

		const JsonPointer& current_match () const;

		// What the find bar and the status bar both show (FIND-02 / FIND-03): "4 of 17 matches", "No matches", or an
		// empty string while the query is empty -- a bar just opened has nothing to report yet.

		QString report () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		// The match set or the current position may have changed; anything showing report() re-reads it. Emitted on a
		// query change, on a navigation, and on a document change -- in the last case WITHOUT the search having re-run,
		// because whether it needs to is decided by whether anyone asks (see the header note on staleness).

		void results_changed ();

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	private slots:

		void handle_document_changed ();   // Any node change: the pointer list may no longer name what it named.
		void handle_document_reset   ();   // A new root: the previous document's matches mean nothing at all.

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		// Re-run the search if the list is stale, keeping the current match by pointer where it survives. const, and the
		// state it writes is mutable, because it is a CACHE refresh: every accessor must be able to trigger it, and
		// making them all non-const would push the laziness out into the callers.

		void ensure_matches () const;

		// Move the current index by one step (+1 / -1) with wrap, publish the selection, and report. Both navigation
		// commands are this function with a sign, which is what keeps the wrap identical at the two ends.

		bool step ( int direction );

		void publish_current ();           // Selection (origin Find) + status message for the current match.

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		SelectionService* selection;
		StatusService*    status;
		ClipboardService* clipboard;

		//=============================================================================================================
		// Data Members -- the query and its results.
		//
		// The results are mutable because they are a lazily-refreshed cache of a pure function of (document, query) --
		// see ensure_matches().
		//=============================================================================================================

	private:

		QString queryText;
		bool    queryMatchCase = false;

		mutable std::vector<JsonPointer> matches;
		mutable int                      currentIndex   = -1;   // Index into matches; -1 when there is no current match.
		mutable bool                     matchesStale   = false;

		JsonPointer emptyPointer;                               // Returned by current_match() when there is none.
	};
}
