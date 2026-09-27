//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CodeView implementation -- the validation / commit lifecycle and the two reveal channels. See the header for the
//   commit model, which is the whole of EDITOR-09.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/CodeView.hpp"

#include "AppConfig.hpp"
#include "dialogs/MessageBox.hpp"
#include "services/IconLibrary.hpp"
#include "printing/print_wrapping.hpp"
#include "services/SelectionService.hpp"
#include "services/settings_profiles.hpp"
#include "services/StatusService.hpp"
#include "style/CodeTokenPalette.hpp"
#include "views/CodeEditor.hpp"
#include "views/JsonHighlighter.hpp"
#include "views/code_folding.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/Validator.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <QAbstractButton>
#include <QEvent>
#include <QShowEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QTimer>
#include <QVBoxLayout>

#include <optional>

namespace vje
{
	// See FormView.cpp: the id is AppConfig's, because the default open set names it too (VIEW-03).

	const QString CodeViewProvider::VIEW_ID = QString::fromUtf8 ( config::editor::view_ids::CODE );

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	CodeView::CodeView
	(
		JsonDocument*     document,
		UndoController*   undo,
		SettingsStore*    settings,
		StatusService*    status,
		SelectionService* selection,
		QWidget*          parent
	)
		: QWidget    ( parent )
		, document   ( document )
		, undo       ( undo )
		, settings   ( settings )
		, status     ( status )
		, selection  ( selection )
	{
		codeEditor = new CodeEditor ( this );

		// NFR-05, and named HERE rather than inside CodeEditor because the class is used twice for two different
		// things: this editable JSON buffer, and the Import XML dialog's read-only preview. A name baked into the
		// class would be wrong in one of the two places.

		codeEditor->setAccessibleName ( tr ( "JSON code editor" ) );

		// The validation message strip (EDITOR-07: "marked at the error with its line/column beneath the editor"). It
		// is hidden while valid rather than showing a "no errors" line -- a permanent strip trains the eye to ignore
		// the one place an error is ever going to appear.

		messageStrip = new QLabel ( this );

		messageStrip->setObjectName ( QStringLiteral ( "codeViewMessage" ) );
		messageStrip->setWordWrap ( true );
		messageStrip->setTextInteractionFlags ( Qt::TextSelectableByMouse );
		messageStrip->hide ();

		QVBoxLayout* const viewLayout = new QVBoxLayout ( this );

		viewLayout->setContentsMargins ( 0, 0, 0, 0 );
		viewLayout->setSpacing ( 0 );
		viewLayout->addWidget ( codeEditor );
		viewLayout->addWidget ( messageStrip );

		// One validation pass per pause rather than per keystroke -- every pass re-parses the whole document, and the
		// message is only useful once the user has stopped typing anyway (config::code::VALIDATION_DEBOUNCE).

		validationTimer = new QTimer ( this );

		validationTimer->setSingleShot ( true );
		validationTimer->setInterval ( config::code::VALIDATION_DEBOUNCE );

		connect ( validationTimer, &QTimer::timeout,             this, &CodeView::handle_validation_due );
		connect ( codeEditor,      &CodeEditor::textChanged,     this, &CodeView::handle_text_changed );

		// The reverse of the reveal channel: a double click in the text names a node to the rest of the application
		// (EDITOR-07). The editor reports a LINE; which node that is, is decided here.

		connect ( codeEditor, &CodeEditor::line_double_clicked, this, &CodeView::handle_line_double_clicked );

		// Which NODES are folded is noted whenever the set of folds changes while this view's text is the document's --
		// the only time a pointer read off the text names a node the document has (see follow_folds).

		connect ( codeEditor, &CodeEditor::folds_changed, this, &CodeView::note_folded_nodes );

		if ( document != nullptr )
		{
			connect ( document, &JsonDocument::node_changed, this, &CodeView::handle_node_changed );
			connect ( document, &JsonDocument::reset,        this, &CodeView::handle_document_reset );
		}

		if ( settings != nullptr )
		{
			connect ( settings, &SettingsStore::changed, this, &CodeView::handle_setting_changed );
		}

		codeEditor->installEventFilter ( this );

		// Folding is this view's, not the editor class's: the Import XML preview reuses the widget read-only and has no
		// use for a marker column (EDITOR-23).

		codeEditor->set_folding_enabled ( true );

		codeEditor->set_format_profile ( document_format_profile ( settings ) );

		apply_token_palette ();
		apply_highlighting ();
		refresh_from_document ();
	}

	//=================================================================================================================
	// IEditorView
	//=================================================================================================================

	QWidget* CodeView::widget ()
	{
		return this;
	}

	void CodeView::present ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		Q_UNUSED ( origin );

		selectedPointer = pointer;

		// This view's OWN double-click, arriving back through the selection service. The caret is already exactly where
		// the user put it, so revealing the line again would scroll the very line they just clicked on.

		if ( publishingSelection )
		{
			return;
		}

		// SCROLL ONLY. A selection change -- however it arose -- reveals the element and stops there (EDITOR-04): no
		// caret move, no current-line move, no focus. The caret changes hands in activate_editing(), and nowhere else.
		//
		// Note what is NOT here: no re-render. This view always shows the whole document, so a selection change has
		// nothing to re-render, which is exactly what makes tree navigation during an uncommitted edit non-destructive
		// (EDITOR-09).

		reveal_pointer ( pointer, false );
	}

	void CodeView::take_focus ()
	{
		codeEditor->setFocus ( Qt::TabFocusReason );
	}

	void CodeView::tree_node_clicked ()
	{
		// SET-07's "Edit on", defaulting to Double click as SET-05's does. Under Single click the tree click both
		// reveals and hands over the caret.

		if ( hands_over_caret_on_click ( settings, settings_keys::CODE_EDIT_ON ) )
		{
			activate_editing ();
		}
	}

	void CodeView::activate_editing ()
	{
		// The activation gesture: the caret and the current-line highlight move to the selected element, and the
		// keyboard comes with them.

		reveal_pointer ( selectedPointer, true );

		codeEditor->setFocus ( Qt::MouseFocusReason );
	}

	bool CodeView::view_deactivating ()
	{
		// EDITOR-09's gate. Nothing to defend is the common case and must be cheap and silent.

		if ( !has_uncommitted_edit () )
		{
			// An edit typed back to the committed text ended without a commit or a discard, so a format change it held
			// back is paid here rather than left for whatever regenerates the text next.

			if ( formatOwed )
			{
				reformat ();
			}

			return true;
		}

		validate_now ();

		if ( textValid )
		{
			// A valid edit AUTO-COMMITS on leaving. The user is not asked, because there is nothing to decide: the edit
			// is applicable and applying it is what they were going to say.
			//
			// The RESULT is honoured rather than discarded, which it had to become the moment a well-formed text could
			// still be refused (an introduced duplicate, SET-03a). Ignoring it would let the user leave the tab
			// believing the edit had landed -- the text is still in the buffer, so nothing is lost, but "it committed"
			// would have been a lie. A refusal falls through to the question below, which now says why.

			if ( commit_now () )
			{
				return true;
			}
		}

		// Only an INVALID edit is a question, because it is the one case where continuing means losing work.

		// Cancel is the default and the escape (STYLE-18 (4)): it keeps the edit, so neither Enter nor Esc loses work.
		// Discard is re-captioned here because MessageBox's table reads it as FILE-08's "Don't Save", and nothing is
		// being saved -- the question is whether to throw the edit away.

		MessageBox box
		(
			MessageKind::Warning,
			tr ( "Invalid JSON" ),
			tr ( "The Code View edit cannot be applied:\n\n%1\n\nKeep editing, or discard the changes?" )
				.arg ( validationMessage ),
			QMessageBox::Discard | QMessageBox::Cancel,
			QMessageBox::Cancel,
			QMessageBox::Cancel,
			this
		);

		box.button ( QMessageBox::Discard )->setText ( tr ( "&Discard" ) );

		if ( box.ask () == QMessageBox::Discard )
		{
			discard_edit ();

			return true;
		}

		return false;   // Keep editing: the departure is aborted.
	}

	bool CodeView::claims_tab_key () const
	{
		return true;
	}

	bool CodeView::has_unsaved_view_edit () const
	{
		return has_uncommitted_edit ();
	}

	PrintContent CodeView::print_content ( int availableColumns ) const
	{
		PrintContent content;

		// Phase 13 prints the view's RENDERING, uncommitted edit included -- so this pays any deferred refresh first
		// and then reads the widget, rather than formatting the document behind the user's edit.

		ensure_current ();

		// What is SHOWN, which is FILE-12's rule and not a stylistic choice: a folded region prints folded (EDITOR-23),
		// so the editor is asked for its visible lines rather than for the text behind them.

		const QString text = codeEditor->visible_text ();

		if ( text.isEmpty () )
		{
			return content;
		}

		// The subject is left empty: this view always shows the WHOLE document (EDITOR-07), so naming a node here
		// would say something untrue about what is on the page.

		content.kind     = PrintContent::Kind::Preformatted;
		content.viewName = tr ( "Code View" );
		content.text     = wrap_preformatted ( text, availableColumns );

		return content;
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	bool CodeView::commit_now ()
	{
		if ( ( document == nullptr ) || ( undo == nullptr ) )
		{
			return true;
		}

		if ( !has_uncommitted_edit () )
		{
			// Nothing to commit is a success, not a refusal -- Ctrl+S on unchanged text is not an error. It is still the
			// end of an edit, if one was typed back to the committed text, so a format change it held back is paid.

			if ( formatOwed )
			{
				reformat ();
			}

			return true;
		}

		const QString text = codeEditor->toPlainText ();

		ValidationResult result = Validator::validate ( text );

		if ( !result.ok )
		{
			refuse_commit ( result.issue.line, result.issue.column, result.issue.message );

			return false;
		}

		// VAL-02 / SET-03a: a duplicate the EDIT INTRODUCES is refused; one the document already carried is not.
		//
		// THIS USED TO REFUSE ANY DUPLICATE IN THE TEXT, and that is the defect reported on 2026-08-20. A file may
		// arrive with duplicate keys -- RFC 8259 permits them and FILE-04 preserves them -- so on such a document
		// EVERY Code View commit was refused, from the moment it loaded, naming an object the user had never touched
		// and could not see from where they were typing. The only way out was to undo the edit or to delete somebody
		// else's data. The comment that stood here stated the rule correctly ("an EDIT that introduces one") while the
		// call beneath it did something stricter, which is lesson D19's shape a second time.
		//
		// The comparison is Validator's census rather than a set of pointers, because an array element's pointer token
		// IS its position: the reported edit was deleting array elements, which renames every duplicate below them.

		if ( !duplicate_keys_allowed ( settings ) && ( document->root () != nullptr ) )
		{
			const std::optional<QString> introduced = Validator::introduced_duplicate ( *document->root (), *result.root );

			if ( introduced.has_value () )
			{
				// The position comes from the parse's own duplicate list, so the message points at an occurrence of
				// the key the user actually introduced rather than at the first duplicate anywhere in the file.

				int line   = 0;
				int column = 0;

				for ( const DuplicateKey& duplicate : result.duplicateKeys )
				{
					if ( duplicate.key == introduced.value () )
					{
						line   = duplicate.line;
						column = duplicate.column;

						break;
					}
				}

				refuse_commit
				(
					line,
					column,
					tr ( "This edit adds a duplicate key \"%1\". A JSON Pointer names only the first member with a "
					     "given key, so the second would be unreachable. Settings > General > Allow edits to create "
					     "duplicate keys permits it." ).arg ( introduced.value () )
				);

				return false;
			}
		}

		// One replace-subtree at the ROOT, so the whole edit is one undo step (EDITOR-07). The command emits
		// node_changed(SubtreeReplaced) rather than reset(), which is what lets the tree preserve its expansion
		// (NAV-03) instead of collapsing to a freshly loaded document.

		committing = true;

		const EditOutcome outcome = undo->replace_subtree ( JsonPointer (), std::move ( result.root ), tr ( "Edit JSON" ) );

		committing = false;

		// The document now holds what the editor holds, so the editor's text becomes the baseline WITHOUT being
		// regenerated -- regenerating it would reformat the user's own whitespace out from under their caret the
		// instant they pressed Ctrl+S.

		committedText  = text;
		lineIndexStale = true;

		// The commit built a NEW tree, so every node noted as folded is gone -- and this is the moment the text and the
		// document agree again, so the folds' nodes can be noted afresh from their pointers.

		note_folded_nodes ();

		// The one commit that DOES regenerate the text: a format change was held back by this edit, and the user asked
		// for it. Paid after the folds are noted, so the regenerated text finds them again by pointer.

		if ( formatOwed )
		{
			reformat ();
		}

		// The gap between the buffer and the document has just closed, so the title's marker has to answer to the
		// DOCUMENT again rather than to this view (VIEW-04). Raised here rather than left to the next keystroke,
		// because a commit is exactly when there may not be one.

		announce_unsaved_edit_state ();

		if ( status != nullptr )
		{
			status->show_message
			(
				( outcome == EditOutcome::Applied ) ? tr ( "Applied Code View changes." )
				                                    : tr ( "No changes to apply." ),
				config::code::MESSAGE_TIMEOUT
			);
		}

		return true;
	}

	void CodeView::refuse_commit ( int line, int column, const QString& reason )
	{
		// One refusal, two callers (malformed text, and an introduced duplicate), so the strip, the flag and the
		// status line cannot come to disagree about what a refused commit looks like.
		//
		// textValid goes FALSE here rather than in validate_now, which is the whole of why this is a separate concept
		// from the typing-time check below: a text can be perfectly well-formed JSON and still be refused.

		textValid         = false;
		validationMessage = ( line > 0 ) ? tr ( "Line %1, column %2: %3" ).arg ( line ).arg ( column ).arg ( reason )
		                                 : reason;

		messageStrip->setText ( validationMessage );
		messageStrip->show ();

		if ( status != nullptr )
		{
			status->show_message ( tr ( "Cannot apply: %1" ).arg ( validationMessage ), config::code::MESSAGE_TIMEOUT );
		}
	}

	void CodeView::discard_edit ()
	{
		if ( !has_uncommitted_edit () )
		{
			return;
		}

		refresh_from_document ();

		if ( status != nullptr )
		{
			status->show_message ( tr ( "Discarded Code View changes." ), config::code::MESSAGE_TIMEOUT );
		}
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	CodeEditor* CodeView::editor () const
	{
		// Handing out the editor is handing out the text, so any deferred refresh is paid first. Nothing in the
		// application calls this -- it exists for the tests, which construct this view without ever showing it, and
		// which would otherwise read text the staleness rule had deliberately left behind.

		ensure_current ();

		return codeEditor;
	}

	bool CodeView::has_uncommitted_edit () const
	{
		return codeEditor->toPlainText () != committedText;
	}

	bool CodeView::is_text_valid () const
	{
		return textValid;
	}

	QString CodeView::validation_message () const
	{
		return validationMessage;
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void CodeView::handle_text_changed ()
	{
		if ( refreshing )
		{
			return;
		}

		lineIndexStale = true;

		validationTimer->start ();

		announce_unsaved_edit_state ();
	}

	void CodeView::announce_unsaved_edit_state ()
	{
		emit unsaved_view_edit_changed ();
	}

	void CodeView::handle_validation_due ()
	{
		validate_now ();

		// The same pause re-derives the folds. Their MARKERS and hidden lines have travelled with the text as it was
		// typed; what moves only with a re-index is which regions exist -- a container the user has just closed, or one
		// they have just broken apart.

		update_fold_regions ();
	}

	void CodeView::handle_node_changed ( const JsonPointer& pointer, DocumentChange change )
	{
		// Something below this pointer changed in another view -- an element inserted or removed, a member renamed or
		// moved, or a group of edits an undo or a multi-node gesture replayed, which the document reports as ONE change
		// naming their common ancestor. The folds are held by pointer, and pointers below it may have moved: follow them
		// first, so the refresh looks up the right ones. A scalar's value edit has nothing folded beneath it.

		if ( ( change != DocumentChange::ValueChanged ) && !committing )
		{
			follow_folds ( pointer );
		}

		handle_document_changed ();
	}

	void CodeView::handle_document_reset ()
	{
		// A load, a New, a Close: a different document, whose nodes share nothing with the old one's but, by accident,
		// some of their pointers. Every load opens fully expanded (EDITOR-23).

		codeEditor->clear_folds ();

		foldedNodes.clear ();

		handle_document_changed ();
	}

	void CodeView::handle_document_changed ()
	{
		if ( committing )
		{
			return;   // This view's own edit coming back. See the class header.
		}

		// An uncommitted edit OUTRANKS a change made elsewhere, because the alternative is destroying work the user can
		// see in front of them without asking. In practice this is nearly unreachable: leaving this view to reach
		// another one runs the EDITOR-09 gate first, which commits or discards. It is defended anyway -- "nearly
		// unreachable" is where the data-loss bugs live.

		if ( has_uncommitted_edit () )
		{
			return;
		}

		// Hidden: owe the refresh rather than pay for it. See the note on documentStale.

		if ( !isVisible () )
		{
			documentStale = true;

			return;
		}

		refresh_from_document ();
	}

	void CodeView::handle_line_double_clicked ( int line )
	{
		// EDITOR-07, the reverse direction: the tree follows the code. What this does NOT do is take the keyboard
		// anywhere -- the user is typing here, and a gesture that moved focus to the tree would be unusable.

		if ( ( selection == nullptr ) || ( document == nullptr ) )
		{
			return;
		}

		ensure_line_index ();

		bool found = false;

		const JsonPointer pointer = pointer_at_line ( lineIndex, line, &found );

		if ( !found )
		{
			return;
		}

		// The index is built from the TEXT, which during an uncommitted edit may describe nodes the document does not
		// have yet. Publishing one of those would name a selection nothing can resolve, so the gesture simply does
		// nothing there -- the same answer reveal_pointer gives when the map has no line for a pointer.

		if ( document->resolve ( pointer ) == nullptr )
		{
			return;
		}

		// Reveal intent: the node may be inside a collapsed branch, and the whole point of the gesture is to find it in
		// the tree (SelectionOrigin::CodeCaret). The flag is what stops the echo scrolling the line just clicked.

		publishingSelection = true;

		selection->set_selection ( pointer, SelectionOrigin::CodeCaret );

		publishingSelection = false;
	}

	void CodeView::handle_setting_changed ( const QString& key )
	{
		if ( !key.startsWith ( QLatin1String ( "codeView." ) ) )
		{
			return;
		}

		if ( key == settings_keys::CODE_SYNTAX_HIGHLIGHTING )
		{
			apply_highlighting ();

			return;
		}

		// The four format settings ARE the document format profile (SET-07), so changing one in the Settings dialog
		// re-formats this view the moment OK commits -- indent character and size, brace style, and separator alignment
		// alike. That is the same profile File > Save writes through, which is what keeps FILE-03's byte-for-byte claim
		// true across a settings change rather than only until one.

		codeEditor->set_format_profile ( document_format_profile ( settings ) );

		if ( !has_uncommitted_edit () )
		{
			reformat ();

			return;
		}

		// A re-format regenerates the whole buffer, so it cannot be applied over an uncommitted edit without discarding
		// it. The edit wins -- and says so, because a settings change that visibly does nothing is indistinguishable
		// from one that failed (VAL-04). It is OWED, not dropped: the message below is a promise, and the commit or
		// discard that ends the edit keeps it.

		formatOwed = true;

		if ( status != nullptr )
		{
			status->show_message
			(
				tr ( "Format settings apply once the current edit is committed or discarded." ),
				config::code::MESSAGE_TIMEOUT
			);
		}
	}

	//=================================================================================================================
	// QWidget
	//=================================================================================================================

	void CodeView::showEvent ( QShowEvent* event )
	{
		QWidget::showEvent ( event );

		ensure_current ();
	}

	void CodeView::changeEvent ( QEvent* event )
	{
		QWidget::changeEvent ( event );

		const QEvent::Type type = event->type ();

		const bool isThemeEvent = ( type == QEvent::StyleChange )              ||
		                          ( type == QEvent::ApplicationPaletteChange ) ||
		                          ( type == QEvent::PaletteChange );

		if ( !isThemeEvent )
		{
			return;
		}

		// Deferred, because ThemeService installs the style BEFORE the palette: the StyleChange that brings the news
		// arrives while the widget still holds the OLD colours, so reading them now would recolour to the theme being
		// left.

		QMetaObject::invokeMethod ( this, [ this ] () { apply_token_palette (); }, Qt::QueuedConnection );
	}

	bool CodeView::eventFilter ( QObject* watched, QEvent* event )
	{
		if ( ( watched != codeEditor ) || ( event->type () != QEvent::KeyPress ) )
		{
			return QWidget::eventFilter ( watched, event );
		}

		const QKeyEvent* const keyEvent = static_cast<QKeyEvent*> ( event );

		// Esc -- EDITOR-09's explicit discard.

		if ( ( keyEvent->key () == Qt::Key_Escape ) && ( keyEvent->modifiers () == Qt::NoModifier ) )
		{
			discard_edit ();

			return true;
		}

		// Ctrl+S -- validate, then commit (EDITOR-07). Handled here rather than through MainWindow's Save action
		// because that action is a Phase 10 placeholder; when it goes live it calls commit_now() first and writes the
		// file second, which is the order that keeps an invalid edit from ever reaching the disk.

		if ( ( keyEvent->key () == Qt::Key_S ) && ( keyEvent->modifiers () == Qt::ControlModifier ) )
		{
			commit_now ();

			return true;
		}

		return QWidget::eventFilter ( watched, event );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void CodeView::ensure_current () const
	{
		// An uncommitted edit outranks a deferred refresh for the reason handle_document_changed() states: the text on
		// screen is the user's, and regenerating it would destroy work. The debt is kept, not discharged -- a commit
		// or discard refreshes anyway.

		if ( !documentStale || has_uncommitted_edit () )
		{
			return;
		}

		const_cast<CodeView*> ( this )->refresh_from_document ();
	}

	void CodeView::reformat ()
	{
		// The caret's node, read off the text before it is regenerated. With no uncommitted edit that text is the
		// document's, so the pointer names a node the new text also has.

		bool found = false;

		const JsonPointer caretNode = pointer_at_line
		(
			build_pointer_span_index ( codeEditor->toPlainText () ),
			codeEditor->caret_line (),
			&found
		);

		refresh_from_document ();

		// The refresh put the caret back on the same line NUMBER, which a new brace style or indent has given to some
		// other line. A caret on no node -- a blank line past the end -- stays where the refresh put it.

		if ( found )
		{
			const int line = line_for_pointer ( lineIndex, caretNode );

			if ( line > 0 )
			{
				codeEditor->move_caret_to_line ( line );
			}
		}
	}

	void CodeView::refresh_from_document ()
	{
		documentStale = false;
		formatOwed    = false;

		const bool hasDocument = ( document != nullptr ) && document->has_root ();

		const QString text = hasDocument
		                   ? JsonFormatter::format ( *document->root (), document_format_profile ( settings ) )
		                   : QString ();

		// Indexed BEFORE the text goes in rather than after, so the folds can go back on before the view is restored
		// (CodeEditor::set_text_preserving_view). It is the same text, so it is the same index -- and it is this view's
		// index from here on, which saves building it a second time on the first reveal.

		lineIndex      = build_pointer_span_index ( text );
		lineIndexStale = false;

		refreshing = true;

		codeEditor->set_text_preserving_view ( text, fold_regions ( lineIndex ) );

		refreshing = false;

		// Re-highlight EXPLICITLY after replacing the whole buffer. QSyntaxHighlighter schedules its initial pass on a
		// zero-timer, so whether a freshly set buffer arrives coloured depends on the event loop getting a turn before
		// the widget paints -- which is true often enough to look like it works and is not a guarantee. Measured: a
		// document given setPlainText and then read back immediately carries no format ranges at all.

		if ( highlighter != nullptr )
		{
			highlighter->rehighlight ();
		}

		committedText = text;

		// The text IS the document again, so the folds just re-applied by pointer name nodes it has.

		note_folded_nodes ();

		validate_now ();
	}

	void CodeView::validate_now ()
	{
		validationTimer->stop ();

		const QString text = codeEditor->toPlainText ();

		// An EMPTY buffer is not an error to report. It is what an empty document renders as, and it is also what the
		// user momentarily has after selecting all and typing -- flagging it would put an error on screen for a
		// document nobody has finished describing yet.

		if ( text.trimmed ().isEmpty () )
		{
			textValid         = true;
			validationMessage.clear ();

			messageStrip->hide ();

			return;
		}

		// SYNTAX ONLY, deliberately. The strip answers "can this be parsed", and the duplicate policy is a question
		// about the COMMIT -- it needs the document's before-state, which nothing here has, and asking it per
		// keystroke would flag a file's own pre-existing duplicates the whole time the view was open, which is half
		// of the defect this pass fixes. A well-formed text that the commit will nevertheless refuse is reported by
		// refuse_commit at the moment it is refused.

		const ValidationResult result = Validator::validate ( text );

		textValid = result.ok;

		if ( result.ok )
		{
			validationMessage.clear ();

			messageStrip->hide ();

			return;
		}

		validationMessage = tr ( "Line %1, column %2: %3" )
			.arg ( result.issue.line )
			.arg ( result.issue.column )
			.arg ( result.issue.message );

		messageStrip->setText ( validationMessage );
		messageStrip->show ();
	}

	void CodeView::apply_token_palette ()
	{
		const CodeTokenPalette tokens = CodeTokenPalette::for_palette ( codeEditor->palette () );

		codeEditor->set_token_palette ( tokens );

		if ( highlighter != nullptr )
		{
			highlighter->set_token_palette ( tokens );
		}
	}

	void CodeView::apply_highlighting ()
	{
		const bool wanted = ( settings == nullptr )
		                  || settings->value_bool ( settings_keys::CODE_SYNTAX_HIGHLIGHTING, true );

		if ( wanted == ( highlighter != nullptr ) )
		{
			return;
		}

		if ( wanted )
		{
			highlighter = new JsonHighlighter ( codeEditor->document () );

			highlighter->set_token_palette ( CodeTokenPalette::for_palette ( codeEditor->palette () ) );
		}
		else
		{
			// Deleting it is what removes the colouring: QSyntaxHighlighter re-highlights the document with no formats
			// on destruction, so there is no separate "clear" step to forget.

			delete highlighter;

			highlighter = nullptr;
		}
	}

	void CodeView::ensure_line_index ()
	{
		if ( !lineIndexStale )
		{
			return;
		}

		// Rebuilt from the TEXT rather than from the document, so it stays correct over an uncommitted edit -- which is
		// what EDITOR-09's "only moves the caret within it" requires (json_text_index.hpp).

		lineIndex      = build_pointer_span_index ( codeEditor->toPlainText () );
		lineIndexStale = false;
	}

	void CodeView::note_folded_nodes ()
	{
		// Only while the text is the document's. During an uncommitted edit a pointer read off the text may name a node
		// the document does not have, or a different one -- so the last notes taken while the two agreed are kept.

		if ( ( document == nullptr ) || has_uncommitted_edit () )
		{
			return;
		}

		foldedNodes.clear ();

		for ( const QString& pointer : codeEditor->folded_pointers () )
		{
			const JsonNode* node = document->resolve ( JsonPointer::parse ( pointer ) );

			if ( node == nullptr )
			{
				continue;
			}

			// The whole chain, root first: follow_folds asks whether the node at a changed pointer is still the one that
			// was there, and that pointer may be any of this node's ancestors.

			QList<const JsonNode*> chain;

			for ( ; node != nullptr; node = node->parent () )
			{
				chain.prepend ( node );
			}

			foldedNodes.insert ( pointer, chain );
		}
	}

	void CodeView::follow_folds ( const JsonPointer& container )
	{
		if ( foldedNodes.isEmpty () || ( document == nullptr ) )
		{
			return;
		}

		// The folds below the container are the only ones whose pointers can have moved.

		const QString prefix = container.to_string () + QLatin1Char ( '/' );
		const int     depth  = container.token_count ();

		QSet<const JsonNode*> wanted;
		const JsonNode*       notedContainer = nullptr;

		for ( auto entry = foldedNodes.constBegin (); entry != foldedNodes.constEnd (); ++entry )
		{
			if ( entry.key ().startsWith ( prefix ) )
			{
				wanted.insert ( entry.value ().last () );

				notedContainer = entry.value ().value ( depth, nullptr );
			}
		}

		const JsonNode* const containerNode = document->resolve ( container );

		if ( wanted.isEmpty () || ( containerNode == nullptr ) || !containerNode->is_container () )
		{
			return;
		}

		// Is the container the SAME node it was? A child-list change, an undo and a multi-node gesture leave it in
		// place and change what is under it; a type change or an array transform REPLACES it, and every node beneath
		// is new. Only in the first case does a node's identity say where its fold went. In the second there is nothing
		// to follow, and the pointers stand -- the replacement keeps the shape more often than not, and a pointer that
		// no longer names a region is dropped when the refresh looks for it.

		if ( containerNode != notedContainer )
		{
			return;
		}

		// Found again by ADDRESS among the container's live descendants, never by dereferencing a noted node: one the
		// edit removed may no longer exist, and asking it for its parent would read freed memory. A node that is not
		// found was removed, and its fold goes with it. This is the tree's own rule for its expansion across the same
		// edit -- JsonTreeModel re-syncs a container's children by node identity rather than by position (TREE-07).

		QHash<const JsonNode*, QString> found;
		QList<const JsonNode*>          pending { containerNode };

		while ( !pending.isEmpty () )
		{
			const JsonNode* const node     = pending.takeLast ();
			const bool            isObject = ( node->kind () == JsonKind::Object );
			const int             children = isObject ? node->member_count () : node->array_size ();

			for ( int index = 0; index < children; ++index )
			{
				const JsonNode* const child = isObject ? node->member_value ( index ) : node->array_element ( index );

				if ( wanted.contains ( child ) )
				{
					found.insert ( child, JsonPointer::from_node ( child ).to_string () );
				}

				if ( child->is_container () )
				{
					pending.append ( child );
				}
			}
		}

		const QStringList current = codeEditor->folded_pointers ();

		QSet<QString>                          pointers ( current.cbegin (), current.cend () );
		QHash<QString, QList<const JsonNode*>> followed;

		for ( auto entry = foldedNodes.constBegin (); entry != foldedNodes.constEnd (); ++entry )
		{
			if ( !entry.key ().startsWith ( prefix ) )
			{
				followed.insert ( entry.key (), entry.value () );

				continue;
			}

			pointers.remove ( entry.key () );

			const auto moved = found.constFind ( entry.value ().last () );

			if ( moved == found.constEnd () )
			{
				continue;
			}

			// Its chain is re-noted from the live node: the ancestors between the container and it may have moved too.

			QList<const JsonNode*> chain;

			for ( const JsonNode* node = moved.key (); node != nullptr; node = node->parent () )
			{
				chain.prepend ( node );
			}

			pointers.insert ( moved.value () );
			followed.insert ( moved.value (), chain );
		}

		foldedNodes = followed;

		codeEditor->set_folded_pointers ( pointers );
	}

	void CodeView::update_fold_regions ()
	{
		// Only a COMPLETE index is allowed to say which regions exist. Below a syntax error the index is silent, and
		// folding would read that silence as every fold down there having been deleted.

		bool complete = false;

		lineIndex      = build_pointer_span_index ( codeEditor->toPlainText (), &complete );
		lineIndexStale = false;

		if ( complete )
		{
			codeEditor->apply_fold_regions ( fold_regions ( lineIndex ) );
		}
	}

	void CodeView::reveal_pointer ( const JsonPointer& pointer, bool moveCaret )
	{
		ensure_line_index ();

		const int line = line_for_pointer ( lineIndex, pointer );

		// 0 means the index does not hold it: an unparsed region below a syntax error, or a node the edited text no
		// longer contains. Doing nothing is the right answer -- scrolling somewhere arbitrary would be worse than
		// staying put.

		if ( line == 0 )
		{
			return;
		}

		if ( moveCaret )
		{
			codeEditor->move_caret_to_line ( line );
		}
		else
		{
			codeEditor->scroll_to_line ( line );
		}
	}

	//=================================================================================================================
	// CodeViewProvider
	//=================================================================================================================

	CodeViewProvider::CodeViewProvider
	(
		JsonDocument*     document,
		UndoController*   undo,
		SettingsStore*    settings,
		StatusService*    status,
		SelectionService* selection
	)
		: document  ( document )
		, undo      ( undo )
		, settings  ( settings )
		, status    ( status )
		, selection ( selection )
	{
	}

	QString CodeViewProvider::view_id () const
	{
		return VIEW_ID;
	}

	QString CodeViewProvider::display_name () const
	{
		return QObject::tr ( "Code" );
	}

	QString CodeViewProvider::icon_name () const
	{
		return icon_names::VIEW_CODE;
	}

	int CodeViewProvider::display_order () const
	{
		return DISPLAY_ORDER;
	}

	bool CodeViewProvider::can_present ( const JsonNode* node ) const
	{
		// The Code View shows the WHOLE document whatever is selected, so the selection only decides where it scrolls
		// to. Only an empty document leaves it nothing to show.

		return node != nullptr;
	}

	IEditorView* CodeViewProvider::create_view ( QWidget* parent ) const
	{
		return new CodeView ( document, undo, settings, status, selection, parent );
	}
}
