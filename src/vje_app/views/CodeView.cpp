//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
#include "services/SettingsStore.hpp"
#include "services/settings_profiles.hpp"
#include "services/StatusService.hpp"
#include "style/CodeTokenPalette.hpp"
#include "views/CodeEditor.hpp"
#include "views/JsonHighlighter.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/Validator.hpp>

#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QTimer>
#include <QVBoxLayout>

namespace vje
{
	const QString CodeViewProvider::VIEW_ID = QStringLiteral ( "code" );

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	CodeView::CodeView
	(
		JsonDocument*   document,
		UndoController* undo,
		SettingsStore*  settings,
		StatusService*  status,
		QWidget*        parent
	)
		: QWidget ( parent )
		, document ( document )
		, undo     ( undo )
		, settings ( settings )
		, status   ( status )
	{
		codeEditor = new CodeEditor ( this );

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

		if ( document != nullptr )
		{
			connect ( document, &JsonDocument::node_changed, this, &CodeView::handle_document_changed );
			connect ( document, &JsonDocument::reset,        this, &CodeView::handle_document_changed );
		}

		if ( settings != nullptr )
		{
			connect ( settings, &SettingsStore::changed, this, &CodeView::handle_setting_changed );
		}

		codeEditor->installEventFilter ( this );

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
			return true;
		}

		validate_now ();

		if ( textValid )
		{
			// A valid edit AUTO-COMMITS on leaving. The user is not asked, because there is nothing to decide: the edit
			// is applicable and applying it is what they were going to say.

			commit_now ();

			return true;
		}

		// Only an INVALID edit is a question, because it is the one case where continuing means losing work.

		const QMessageBox::StandardButton answer = QMessageBox::warning
		(
			this,
			tr ( "Invalid JSON" ),
			tr ( "The Code View edit cannot be applied:\n\n%1\n\nKeep editing, or discard the changes?" )
				.arg ( validationMessage ),
			QMessageBox::Discard | QMessageBox::Cancel,
			QMessageBox::Cancel
		);

		if ( answer == QMessageBox::Discard )
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
			return true;   // Nothing to commit is a success, not a refusal -- Ctrl+S on unchanged text is not an error.
		}

		const QString text = codeEditor->toPlainText ();

		// rejectDuplicates is TRUE here and false on load, and the asymmetry is deliberate (VAL-02): a file may arrive
		// with duplicate keys and RFC 8259 permits them, but an EDIT that introduces one is rejected, because a pointer
		// names the first match and the second would be unreachable.

		ValidationResult result = Validator::validate ( text, true );

		if ( !result.ok )
		{
			textValid         = false;
			validationMessage = tr ( "Line %1, column %2: %3" )
				.arg ( result.issue.line )
				.arg ( result.issue.column )
				.arg ( result.issue.message );

			messageStrip->setText ( validationMessage );
			messageStrip->show ();

			if ( status != nullptr )
			{
				status->show_message ( tr ( "Cannot apply: %1" ).arg ( validationMessage ), config::code::MESSAGE_TIMEOUT );
			}

			return false;
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
	}

	void CodeView::handle_validation_due ()
	{
		validate_now ();
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

		refresh_from_document ();
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

		// A format-profile change re-formats the whole document, so it cannot be applied over an uncommitted edit
		// without discarding it. The edit wins; the new profile takes effect at the next refresh.

		codeEditor->set_format_profile ( document_format_profile ( settings ) );

		if ( !has_uncommitted_edit () )
		{
			refresh_from_document ();
		}
	}

	//=================================================================================================================
	// QWidget
	//=================================================================================================================

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

	void CodeView::refresh_from_document ()
	{
		const bool hasDocument = ( document != nullptr ) && document->has_root ();

		const QString text = hasDocument
		                   ? JsonFormatter::format ( *document->root (), document_format_profile ( settings ) )
		                   : QString ();

		refreshing = true;

		codeEditor->set_text_preserving_view ( text );

		refreshing = false;

		// Re-highlight EXPLICITLY after replacing the whole buffer. QSyntaxHighlighter schedules its initial pass on a
		// zero-timer, so whether a freshly set buffer arrives coloured depends on the event loop getting a turn before
		// the widget paints -- which is true often enough to look like it works and is not a guarantee. Measured: a
		// document given setPlainText and then read back immediately carries no format ranges at all.

		if ( highlighter != nullptr )
		{
			highlighter->rehighlight ();
		}

		committedText  = text;
		lineIndexStale = true;

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

		const ValidationResult result = Validator::validate ( text, true );

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

	void CodeView::reveal_pointer ( const JsonPointer& pointer, bool moveCaret )
	{
		if ( lineIndexStale )
		{
			// Rebuilt from the TEXT rather than from the document, so it stays correct over an uncommitted edit --
			// which is what EDITOR-09's "only moves the caret within it" requires (json_text_index.hpp).

			lineIndex      = build_pointer_line_index ( codeEditor->toPlainText () );
			lineIndexStale = false;
		}

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
		JsonDocument*   document,
		UndoController* undo,
		SettingsStore*  settings,
		StatusService*  status
	)
		: document ( document )
		, undo     ( undo )
		, settings ( settings )
		, status   ( status )
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
		return QStringLiteral ( "vje-view-code" );
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
		return new CodeView ( document, undo, settings, status, parent );
	}
}
