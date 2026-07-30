//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

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

		// NFR-05. The tab names the view, but the tab is not where the keyboard is once the view is entered.

		textEdit->setAccessibleName ( tr ( "Text view" ) );

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

		// A tab in a rendered STRING VALUE occupies exactly ONE column, which is a correctness constraint rather than a
		// style choice. The renderer aligns every separator, draws every table rule and measures every wrap width by
		// COUNTING CHARACTERS, so a character that renders wider than one column breaks all three at once: a line
		// counted at 70 columns renders at 90 and overruns the view, and the hanging indent -- padded with literal
		// spaces -- no longer lines up with the first line it is indenting under.
		//
		// It was TAB_STOP_CHARACTERS advances until the 2026-07-28 review, which is only ever visible under the Decoded
		// notation (Escaped shows the tab as "\t" and Flattened removes it), which is why the default hid it. Left at
		// the widget's own default it would be worse still: 80 px, tearing a hole through an aligned row.

		textEdit->setTabStopDistance
		(
			QFontMetricsF ( fixedFont ).horizontalAdvance ( QLatin1Char ( ' ' ) )
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

	PrintContent TextView::print_content ( int availableColumns ) const
	{
		PrintContent content;

		const JsonNode* const node = ( document != nullptr ) ? document->resolve ( currentContainer ) : nullptr;

		if ( !hasRendering || ( node == nullptr ) )
		{
			return content;
		}

		TextViewProfile profile = text_view_profile ( settings );

		// The page's width, NOT the pane's, and unconditionally -- see the header. A zero here is "the caller does not
		// know the width", which leaves the rendering unwrapped exactly as it was before this existed.

		profile.wrapColumns = std::max ( 0, availableColumns );

		content.kind     = PrintContent::Kind::Preformatted;
		content.viewName = tr ( "Text View" );
		content.subject  = currentContainer.to_string ();
		content.text     = TextViewRenderer::render ( *node, profile );

		// A rendering the renderer does not wrap must not be wrapped by the page either: the styles that decline are
		// the ones where a broken line is a corrupt record rather than an untidy one, so they are clipped and say so.

		content.overflow = TextViewRenderer::wraps_long_values ( *node, profile )
		                       ? PrintContent::Overflow::Wrap
		                       : PrintContent::Overflow::Clip;

		return content;
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
		// The SET-06 group, plus SET-03's String display -- which is stored under "general." because it is ONE setting
		// shared with the Form View, and which a prefix test on "textView." therefore silently dropped until the
		// 2026-07-28 review. The Text View then kept rendering the old notation while the Form tab showed the new one,
		// defeating the single-setting rule EDITOR-06 rests on.

		const bool isTextViewSetting = key.startsWith ( QLatin1String ( "textView." ) )
		                            || ( key == settings_keys::STRING_DISPLAY );

		if ( !hasRendering || !isTextViewSetting )
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

	int TextView::wrap_columns () const
	{
		// Off unless asked for, in which case it is the viewport's width IN CHARACTERS. That measurement is exact
		// rather than approximate because the font is fixed-width -- the same property the renderer's column alignment
		// already depends on -- so a value that fits by this count fits on screen.

		if ( !wrap_strings_in_text_view ( settings ) )
		{
			return 0;
		}

		const qreal advance = QFontMetricsF ( textEdit->font () ).horizontalAdvance ( QLatin1Char ( '0' ) );

		if ( advance <= 0.0 )
		{
			return 0;
		}

		// One column short of the viewport, so the last character of a full line is never flush against the scroll bar
		// (and never triggers a horizontal scroll bar of its own, which would then narrow the viewport again).

		const int columns = static_cast<int> ( textEdit->viewport ()->width () / advance ) - 1;

		return std::max ( 0, columns );
	}

	void TextView::resizeEvent ( QResizeEvent* event )
	{
		QWidget::resizeEvent ( event );

		// The wrap width follows the window (SET-06), so a resize is a re-render -- but only while wrapping is on and
		// only when the width in COLUMNS actually changed. A drag across a pane is dozens of pixel-level resizes and
		// at most a handful of column boundaries, so this is what keeps a large node from being re-rendered on every
		// one of them. The scroll position is preserved, since the user has not navigated anywhere.

		if ( !hasRendering )
		{
			return;
		}

		const int columns = wrap_columns ();

		if ( columns == renderedWrapColumns )
		{
			return;
		}

		renderedWrapColumns = columns;

		render ( true );
	}

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

		TextViewProfile profile = text_view_profile ( settings );

		// Rendered up to TWICE, because the width the text is wrapped against is not knowable until the text is in.
		// A rendering taller than the viewport makes QPlainTextEdit add its vertical scroll bar, which narrows the
		// viewport by about two monospace columns AFTER the wrap width was measured -- so full-width lines overrun and
		// the horizontal scroll bar the one-column margin exists to prevent appears anyway. Nothing re-renders on its
		// own: the TextView widget's own size never changed, so resizeEvent does not fire.
		//
		// One retry settles it. The bar can only ever NARROW the viewport, and a re-render at the narrower width
		// produces more lines, so the bar stays and the second measurement is stable; the loop cannot oscillate.

		constexpr int MAXIMUM_RENDER_PASSES = 2;

		for ( int pass = 0; pass < MAXIMUM_RENDER_PASSES; ++pass )
		{
			profile.wrapColumns = wrap_columns ();

			renderedWrapColumns = profile.wrapColumns;

			textEdit->setPlainText ( TextViewRenderer::render ( *node, profile ) );

			if ( wrap_columns () == renderedWrapColumns )
			{
				break;
			}
		}

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
