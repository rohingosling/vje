//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CodeView -- the raw-JSON editing view (EDITOR-07 / EDITOR-09): the whole document as pretty-printed text, live
//   validation, and a streamlined commit with no Apply button.
//
//   WHAT IT SHOWS, PRECISELY. The whole document, formatted by the DOCUMENT FORMAT PROFILE (SET-07) -- the same profile
//   File > Save writes, taken from the same reader (settings_profiles.hpp), which is what makes EDITOR-07's
//   "byte-for-byte what Save writes" a structural fact rather than a claim two call sites happen to keep.
//
//   THE COMMIT MODEL, which is the whole of EDITOR-09 and is easy to get subtly wrong:
//
//     - A valid edit commits when the view is LEFT (tab switch, or a File action) or on Ctrl+S, as ONE undo step
//       replacing the document root. There is no Apply button and no per-keystroke propagation.
//     - An INVALID edit cannot commit, and cannot be silently dropped either. Leaving with one asks keep / discard;
//       keeping ABORTS the departure (view_deactivating returns false), which is why that seam exists on IEditorView
//       and why QTabWidget::currentChanged -- which fires after the switch -- could not have served.
//     - Esc is the explicit discard, restoring the committed text.
//     - TREE NAVIGATION IS NOT LEAVING. Arrowing around the tree while an edit is in progress must not commit (a
//       whole-document commit would rebuild the tree under the user) and must not discard. It only moves within the
//       text, which is why the pointer index is built from the TEXT and not from the document (json_text_index.hpp).
//
//   THE TWO REVEAL CHANNELS (EDITOR-04). A tree SELECTION scrolls the editor to the corresponding element and stops
//   there -- no caret move, no current-line move, no focus. The activation GESTURE moves the caret there and hands over
//   editing. Passive navigation therefore cannot disturb an uncommitted edit or snap the viewport away from what the
//   user is reading.
//
//   WHAT IS NOT HERE. Ctrl+S commits and reports; the FILE WRITE that EDITOR-07 chains after it belongs to the file
//   lifecycle, which does not exist yet. The commit is the half that is this view's, and it
//   is the half that has to happen first -- an invalid edit blocks the save before anything reaches the disk.
//
// TODO:
//
//   1. Phase 10: chain File > Save's write onto commit_now(), and route the File actions through confirm_leaving().
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "views/IEditorView.hpp"
#include "views/json_text_index.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonPointer.hpp>

#include <QHash>
#include <QString>
#include <QWidget>

class QLabel;
class QTimer;

namespace vje
{
	class CodeEditor;
	class JsonHighlighter;
	class JsonNode;
	class SelectionService;
	class SettingsStore;
	class StatusService;
	class UndoController;

	//*****************************************************************************************************************
	// Class: CodeView
	//*****************************************************************************************************************

	class CodeView : public QWidget, public IEditorView
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// The settings store may be null (SET-07 defaults apply); the status service may be null, which silences the
		// commit / discard notices; the selection service may be null, which silences the double-click navigation
		// below. All three are the forms the headless tests use.

		CodeView
		(
			JsonDocument*     document,
			UndoController*   undo,
			SettingsStore*    settings,
			StatusService*    status,
			SelectionService* selection,
			QWidget*          parent = nullptr
		);

		//=============================================================================================================
		// IEditorView
		//=============================================================================================================

	public:

		QWidget* widget () override;

		void present ( const JsonPointer& pointer, SelectionOrigin origin ) override;

		void take_focus        () override;
		void tree_node_clicked () override;
		void activate_editing  () override;

		bool view_deactivating () override;

		// Tab and Shift+Tab indent here rather than moving between panes (EDITOR-07), so this view leaves the NAV-04
		// cycle while it holds the keyboard.

		bool claims_tab_key () const override;

		// VIEW-04. The Code View is the one view that can hold unsaved work of its own, which is what EDITOR-09's
		// whole gate is about; this is the same question the gate asks, published for the title.

		bool has_unsaved_view_edit () const override;

		// FILE-12: the buffer as it stands, which for this view means an UNCOMMITTED edit prints as it is on screen.
		// That is deliberate and is why printing does not run the EDITOR-09 departure gate (PrintController): what this
		// view is displaying is its rendering, and a read-only command that stopped to ask keep / discard would be a
		// surprising thing for Ctrl+P to do.
		//
		// A folded region prints folded (EDITOR-23): the view's rendering has the fold in it.
		//
		// A line wider than the page is wrapped under ITS OWN indentation (printing/print_wrapping), because what a
		// JSON line means is read off that indentation -- a continuation starting at column 0 would read as a sibling
		// at the document root.

		PrintContent print_content ( int availableColumns ) const override;

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public:

		// Validate and commit the current text as one undo step. Returns false when the text is invalid (nothing is
		// committed and the reason is on screen); returns true when it committed OR when there was nothing to commit.
		// This is Ctrl+S's first half and Phase 10's entry point for File > Save.

		bool commit_now ();

		// Drop the uncommitted edit and restore the committed text (EDITOR-09's explicit discard, on Esc).

		void discard_edit ();

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		CodeEditor* editor () const;

		bool has_uncommitted_edit () const;   // The text differs from what the document holds.
		bool is_text_valid        () const;   // The last validation pass accepted the text.

		QString validation_message () const;  // Empty while valid.

		//=============================================================================================================
		// Handlers
		//=============================================================================================================

	signals:

		// VIEW-04: this view's unsaved-edit state may have changed. EditorPane forwards it; MainWindow refreshes the
		// title's marker.

		void unsaved_view_edit_changed ();

	private slots:

		void handle_text_changed     ();

	private:

		// VIEW-04. Raised where has_unsaved_view_edit () may have changed -- the keystroke that first differs from the
		// committed text, and the commit or discard that closes the gap. A SIGNAL rather than a callback because
		// CodeView is a QObject and its provider is not; EditorPane forwards it, and MainWindow refreshes the title.
		//
		// It is raised on every keystroke rather than only on a transition, which is deliberate: tracking the
		// transition means a second copy of "does the text differ", and the two would drift. update_title is four
		// string operations and QWidget::setWindowTitle is a no-op on an unchanged string.

		void announce_unsaved_edit_state ();

	private slots:
		void handle_validation_due   ();
		void handle_node_changed     ( const JsonPointer& pointer, DocumentChange change );
		void handle_document_changed ();
		void handle_document_reset   ();
		void note_folded_nodes       ();   // See foldedNodes.
		void handle_setting_changed  ( const QString& key );

		// A double click in the editor names the node that line belongs to, and publishes it as the selection -- so the
		// tree highlights it and the status bar reports it, WITHOUT the keyboard leaving this view (EDITOR-07). The
		// reverse of the reveal channel: everywhere else the tree names a node and this view scrolls to it.

		void handle_line_double_clicked ( int line );

		//=============================================================================================================
		// QWidget
		//=============================================================================================================

	protected:

		void changeEvent ( QEvent* event ) override;

		// A view that becomes visible pays whatever refresh it deferred while it was hidden -- the other half of the
		// staleness rule below.

		void showEvent ( QShowEvent* event ) override;

		// Esc and Ctrl+S are the view's, but the KEYBOARD is the editor's -- so they are intercepted on the way in
		// rather than waited for on the way out. A QPlainTextEdit accepts most key events it is given, so relying on
		// them to propagate up to this widget's keyPressEvent would work for exactly as long as Qt's internal handling
		// happened not to claim them.

		bool eventFilter ( QObject* watched, QEvent* event ) override;

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void refresh_from_document ();   // Regenerate the text and take it as the new committed baseline.

		// A format change applied (SET-07): the text regenerated in the new profile, and the caret moved to the line of
		// the node it was on, since a new brace style or indent moves every line below the first. Only with no
		// uncommitted edit -- the text it reads the caret's node from must be the document's.

		void reformat ();

		// Pay a deferred refresh, if one is owed. const because every caller is a QUESTION asked of this view rather
		// than a change to it: what the view logically shows has not moved, only the widget's copy of it is behind.
		// That is what `mutable documentStale` is for.

		void ensure_current () const;
		void validate_now          ();   // Run the SYNTAX pass and update the message strip (see the implementation).

		// A refused commit, in one place: the flag, the strip and the status line together. Two callers -- malformed
		// text, and a duplicate the edit introduced (SET-03a) -- and the second is why this is not simply part of
		// validate_now: a text can be well-formed JSON and still be refused. Pass line 0 for a reason with no position.

		void refuse_commit ( int line, int column, const QString& reason );
		void apply_token_palette   ();
		void apply_highlighting    ();   // Attach or detach the highlighter per SET-07.

		void reveal_pointer ( const JsonPointer& pointer, bool moveCaret );

		// Rebuild the text -> node index if the text has moved since it was built. Both directions of the map need it,
		// so neither owns it.

		void ensure_line_index ();

		// Rebuild the index from the text and, when it is complete, hand the editor the regions it found (EDITOR-23).

		void update_fold_regions ();

		// An edit in another view changed what lies below container: re-point every fold below it at its node's new
		// pointer, and drop the folds whose nodes are gone -- unless container itself was replaced, when there is no
		// identity left to follow and the pointers stand.

		void follow_folds ( const JsonPointer& container );

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		UndoController*   undo;
		SettingsStore*    settings;
		StatusService*    status;
		SelectionService* selection;

		//=============================================================================================================
		// Data Members -- widgets (parent-owned).
		//=============================================================================================================

	private:

		CodeEditor*      codeEditor  = nullptr;
		QLabel*          messageStrip = nullptr;
		JsonHighlighter* highlighter = nullptr;   // Null while SET-07's syntax highlighting is off.
		QTimer*          validationTimer = nullptr;

		//=============================================================================================================
		// Data Members -- state
		//=============================================================================================================

	private:

		QString committedText;              // What the document's text was when it was last generated or committed.
		QString validationMessage;          // Empty while valid.
		bool    textValid = true;

		JsonPointer      selectedPointer;   // The tree's selection, for the reveal channels.
		PointerSpanIndex lineIndex;         // Built from the TEXT, so it survives an uncommitted edit.
		bool             lineIndexStale = true;

		// Set while this view is the thing changing the document, so the node_changed that comes straight back does not
		// regenerate the text under the user's caret (never re-present a view in response to its own edit).

		bool committing = false;

		// Set while refresh_from_document() is writing the text, so the textChanged storm that causes is not mistaken
		// for the user typing -- which would mark a freshly loaded document as having an uncommitted edit.

		bool refreshing = false;

		// A HIDDEN VIEW DOES NOT REGENERATE (NFR-03). refresh_from_document() re-serializes the WHOLE document,
		// re-highlights it and re-parses it -- three passes over the file for a change to one cell, and this view
		// spends most of its life as a background tab nobody is looking at. Measured on a 4,000-element document, a
		// nine-row column paste cost 108 ms with this view alive and 3.9 ms without it; the array being pasted into
		// had nine rows in both, because the cost never depended on it.
		//
		// So the refresh is DEFERRED rather than skipped, and paid on the next question asked of the view -- becoming
		// visible, or an accessor reading the text. That is FindController's rule (Phase 11) and JsonPathView's
		// (Phase 15d), reaching its third consumer for the same reason: the answer is cheap to recompute and usually
		// nobody asks.

		mutable bool documentStale = false;

		// A FORMAT CHANGE WAITS FOR AN UNCOMMITTED EDIT (SET-07, Phase 15k.2). Regenerating the text would discard the
		// edit, so the change is owed instead -- and paid when the edit ends. A discard regenerates the text anyway; a
		// commit keeps the user's text as typed, which is right for their spacing and wrong for a profile they have
		// just asked for, so a commit pays it explicitly. Any regeneration clears it, since every one uses the current
		// profile.

		bool formatOwed = false;

		// Set while THIS view is writing the selection, so the present() that comes straight back does not scroll the
		// line the user just clicked on. A mechanism flag rather than a test of the origin: an origin says what the
		// selection MEANS, never who wrote it, and a later writer sharing it would silently break the guard (D5).

		bool publishingSelection = false;

		// The node each fold is on, noted while this view's text is the document's (EDITOR-23), with its ancestors root
		// first. A fold is held by pointer, and an array element's pointer is its POSITION -- so an element inserted
		// above a folded one in another view would otherwise move the fold onto its neighbour. The addresses are
		// compared, never dereferenced: a noted node the edit removed may no longer exist.

		QHash<QString, QList<const JsonNode*>> foldedNodes;
	};

	//*****************************************************************************************************************
	// Class: CodeViewProvider
	//*****************************************************************************************************************

	class CodeViewProvider : public IEditorViewProvider
	{
		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		CodeViewProvider
		(
			JsonDocument*     document,
			UndoController*   undo,
			SettingsStore*    settings,
			StatusService*    status,
			SelectionService* selection
		);

		//=============================================================================================================
		// IEditorViewProvider
		//=============================================================================================================

	public:

		QString view_id       () const override;
		QString display_name  () const override;
		QString icon_name     () const override;
		int     display_order () const override;

		bool can_present ( const JsonNode* node ) const override;

		IEditorView* create_view ( QWidget* parent ) const override;

		//=============================================================================================================
		// Constants
		//=============================================================================================================

	public:

		static const QString VIEW_ID;

		static constexpr int DISPLAY_ORDER = 2;   // Last in the strip (EDITOR-01).

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		UndoController*   undo;
		SettingsStore*    settings;
		StatusService*    status;
		SelectionService* selection;
	};
}
