//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   TextView implementation. See the header for the presentation rule it shares with the Form View and for why the
//   fixed-width font is a correctness constraint rather than a style choice.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/TextView.hpp"

#include "AppConfig.hpp"
#include "services/SettingsStore.hpp"
#include "services/settings_profiles.hpp"
#include "style/fixed_font.hpp"
#include "views/node_presentation.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/TextViewRenderer.hpp>

#include <QFont>
#include <QFontMetricsF>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>

namespace vje
{
	const QString TextViewProvider::VIEW_ID = QStringLiteral ( "text" );

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	TextView::TextView ( JsonDocument* document, SettingsStore* settings, QWidget* parent )
		: QWidget ( parent )
		, document ( document )
		, settings ( settings )
	{
		textEdit = new QPlainTextEdit ( this );

		textEdit->setObjectName ( QStringLiteral ( "textViewEdit" ) );
		textEdit->setReadOnly ( true );
		textEdit->setLineWrapMode ( QPlainTextEdit::NoWrap );
		textEdit->setFrameShape ( QFrame::NoFrame );

		// Read-only, but the text must still be SELECTABLE -- copying the rendering into another application is the
		// entire purpose of this view (EDITOR-06). setReadOnly already leaves selection on; stating it here keeps a
		// later "tidy-up" from replacing the widget with a QLabel and quietly losing keyboard selection.

		textEdit->setTextInteractionFlags ( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard );

		// The renderer aligns separators and draws table rules by counting characters, so a proportional font would
		// leave every aligned column ragged and every border broken. See the header -- and note that asking Qt for
		// "the fixed font" is NOT enough on its own, which is what monospace_font() exists to deal with.

		const QFont fixedFont = monospace_font ();

		textEdit->setFont ( fixedFont );

		// A tab in a rendered STRING VALUE would otherwise advance by the widget's default 80 px, tearing a hole
		// through an otherwise aligned table row.

		textEdit->setTabStopDistance
		(
			config::text_view::TAB_STOP_CHARACTERS * QFontMetricsF ( fixedFont ).horizontalAdvance ( QLatin1Char ( ' ' ) )
		);

		QVBoxLayout* const viewLayout = new QVBoxLayout ( this );

		viewLayout->setContentsMargins ( 0, 0, 0, 0 );
		viewLayout->addWidget ( textEdit );

		// EDITOR-08. Both signals matter and neither subsumes the other: node_changed carries an in-place edit, reset
		// carries a load or a Code View commit at the root.

		if ( document != nullptr )
		{
			connect ( document, &JsonDocument::node_changed, this, &TextView::handle_document_changed );
			connect ( document, &JsonDocument::reset,        this, &TextView::handle_document_changed );
		}

		if ( settings != nullptr )
		{
			connect ( settings, &SettingsStore::changed, this, &TextView::handle_setting_changed );
		}
	}

	//=================================================================================================================
	// IEditorView
	//=================================================================================================================

	QWidget* TextView::widget ()
	{
		return this;
	}

	void TextView::present ( const JsonPointer& pointer, SelectionOrigin origin )
	{
		Q_UNUSED ( origin );

		// The origin is deliberately ignored. Every origin means the same thing to a read-only view: show this node.
		// There is no caret to hand over and no editor to open, so the gesture / selection split the Form View and the
		// Code View both turn on has nothing to decide here (IEditorView::present).

		const NodePresentation target = resolve_presentation ( document, pointer );

		if ( target.mode == NodePresentation::Mode::Nothing )
		{
			textEdit->clear ();

			currentContainer = JsonPointer ();
			hasRendering     = false;

			return;
		}

		// Re-rendering the container already on screen is what happens on every arrow key down an object's scalars --
		// each resolves to the same parent. Keeping the scroll position is what stops that walking the reader back to
		// the top of a long rendering on every keystroke.

		const bool sameContainer = hasRendering && ( target.container == currentContainer );

		currentContainer = target.container;
		hasRendering     = true;

		render ( sameContainer );
	}

	void TextView::take_focus ()
	{
		// The text edit, not the container: the keyboard has to land somewhere that answers the selection and copy
		// keys, which is the only thing a user does in this view (NAV-04).

		textEdit->setFocus ( Qt::TabFocusReason );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	QPlainTextEdit* TextView::text_edit () const
	{
		return textEdit;
	}

	const JsonPointer& TextView::rendered_pointer () const
	{
		return currentContainer;
	}

	QString TextView::rendered_text () const
	{
		return textEdit->toPlainText ();
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void TextView::handle_document_changed ()
	{
		if ( !hasRendering )
		{
			return;
		}

		// The rendered node may have been the thing that was removed, in which case there is nothing left to show. The
		// selection change that follows will present whatever the tree falls back to (NAV-03).

		if ( document->resolve ( currentContainer ) == nullptr )
		{
			textEdit->clear ();

			currentContainer = JsonPointer ();
			hasRendering     = false;

			return;
		}

		render ( true );
	}

	void TextView::handle_setting_changed ( const QString& key )
	{
		if ( !hasRendering || !key.startsWith ( QLatin1String ( "textView." ) ) )
		{
			return;
		}

		// A style change re-lays the whole rendering out, so the old scroll offset names a different row. Going back to
		// the top is the honest answer -- the user just asked for a different shape.

		render ( false );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	void TextView::render ( bool preserveScroll )
	{
		const JsonNode* const node = document->resolve ( currentContainer );

		if ( node == nullptr )
		{
			textEdit->clear ();

			return;
		}

		const int verticalOffset   = textEdit->verticalScrollBar   ()->value ();
		const int horizontalOffset = textEdit->horizontalScrollBar ()->value ();

		textEdit->setPlainText ( TextViewRenderer::render ( *node, text_view_profile ( settings ) ) );

		if ( preserveScroll )
		{
			// Clamped by the scroll bar itself: a re-render after a delete may be shorter than the old offset, and
			// QScrollBar::setValue already refuses a value past its maximum rather than needing to be asked.

			textEdit->verticalScrollBar   ()->setValue ( verticalOffset );
			textEdit->horizontalScrollBar ()->setValue ( horizontalOffset );
		}
	}

	//=================================================================================================================
	// TextViewProvider
	//=================================================================================================================

	TextViewProvider::TextViewProvider ( JsonDocument* document, SettingsStore* settings )
		: document ( document )
		, settings ( settings )
	{
	}

	QString TextViewProvider::view_id () const
	{
		return VIEW_ID;
	}

	QString TextViewProvider::display_name () const
	{
		return QObject::tr ( "Text" );
	}

	QString TextViewProvider::icon_name () const
	{
		return QStringLiteral ( "vje-view-text" );
	}

	int TextViewProvider::display_order () const
	{
		return DISPLAY_ORDER;
	}

	bool TextViewProvider::can_present ( const JsonNode* node ) const
	{
		// Every node kind renders, through the same presentation rule the Form View uses (EDITOR-06). Only an empty
		// document leaves nothing to show.

		return node != nullptr;
	}

	IEditorView* TextViewProvider::create_view ( QWidget* parent ) const
	{
		return new TextView ( document, settings, parent );
	}
}
