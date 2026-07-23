//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

#include <vje_core/document/JsonPointer.hpp>

#include <QString>
#include <QWidget>

class QLabel;
class QTimer;

namespace vje
{
	class CodeEditor;
	class JsonDocument;
	class JsonHighlighter;
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
		// commit / discard notices. Both are the forms the headless tests use.

		CodeView
		(
			JsonDocument*   document,
			UndoController* undo,
			SettingsStore*  settings,
			StatusService*  status,
			QWidget*        parent = nullptr
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

	private slots:

		void handle_text_changed     ();
		void handle_validation_due   ();
		void handle_document_changed ();
		void handle_setting_changed  ( const QString& key );

		//=============================================================================================================
		// QWidget
		//=============================================================================================================

	protected:

		void changeEvent ( QEvent* event ) override;

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
		void validate_now          ();   // Run the validation pass and update the message strip.
		void apply_token_palette   ();
		void apply_highlighting    ();   // Attach or detach the highlighter per SET-07.

		void reveal_pointer ( const JsonPointer& pointer, bool moveCaret );

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*   document;
		UndoController* undo;
		SettingsStore*  settings;
		StatusService*  status;

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
		PointerLineIndex lineIndex;         // Built from the TEXT, so it survives an uncommitted edit.
		bool             lineIndexStale = true;

		// Set while this view is the thing changing the document, so the node_changed that comes straight back does not
		// regenerate the text under the user's caret (never re-present a view in response to its own edit).

		bool committing = false;

		// Set while refresh_from_document() is writing the text, so the textChanged storm that causes is not mistaken
		// for the user typing -- which would mark a freshly loaded document as having an uncommitted edit.

		bool refreshing = false;
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
			JsonDocument*   document,
			UndoController* undo,
			SettingsStore*  settings,
			StatusService*  status
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

		JsonDocument*   document;
		UndoController* undo;
		SettingsStore*  settings;
		StatusService*  status;
	};
}
