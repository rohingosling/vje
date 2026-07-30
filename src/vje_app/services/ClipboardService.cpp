//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ClipboardService implementation. See the header for the dual-format contract.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "services/ClipboardService.hpp"

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/CellPasteConverter.hpp>
#include <vje_core/services/JsonSerializer.hpp>

#include <QClipboard>
#include <QMimeData>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	ClipboardService::ClipboardService ( QClipboard* clipboard, QObject* parent )
		: QObject    ( parent )
		, clipboard  ( clipboard )
	{
		if ( clipboard != nullptr )
		{
			connect ( clipboard, &QClipboard::dataChanged, this, &ClipboardService::handle_clipboard_changed );

			// Seed the has_content cache with whatever the clipboard already holds, so the first enablement pass is
			// right without waiting for a change.

			contentPresent = mime_has_content ( clipboard->mimeData () );
		}
	}

	//=================================================================================================================
	// Copy
	//=================================================================================================================

	void ClipboardService::copy_node ( const JsonNode& node, const QString& sourceKey )
	{
		if ( clipboard == nullptr )
		{
			return;
		}

		QMimeData* const data = new QMimeData ();   // QClipboard::setMimeData takes ownership.

		fill_node_mime ( data, node, sourceKey );

		clipboard->setMimeData ( data );
	}

	void ClipboardService::copy_cell ( const JsonNode& node )
	{
		if ( clipboard == nullptr )
		{
			return;
		}

		QMimeData* const data = new QMimeData ();

		fill_cell_mime ( data, node );

		clipboard->setMimeData ( data );
	}

	void ClipboardService::set_plain_text ( const QString& text )
	{
		if ( clipboard == nullptr )
		{
			return;
		}

		// setText REPLACES the clipboard's contents outright, which is exactly what is wanted: any private
		// application/x-vje-json left by an earlier node copy must go, or a following paste would insert that node
		// while the user believed they had just copied a pointer.

		clipboard->setText ( text );
	}

	//=================================================================================================================
	// Paste
	//=================================================================================================================

	std::unique_ptr<JsonNode> ClipboardService::value () const
	{
		return ( clipboard != nullptr ) ? value_from_mime ( clipboard->mimeData () ) : nullptr;
	}

	QString ClipboardService::source_key () const
	{
		return ( clipboard != nullptr ) ? source_key_from_mime ( clipboard->mimeData () ) : QString ();
	}

	bool ClipboardService::has_content () const
	{
		// The cache, not the clipboard -- see the header. handle_clipboard_changed is the one place that reads the OS.

		return contentPresent;
	}

	//=================================================================================================================
	// Handlers
	//=================================================================================================================

	void ClipboardService::handle_clipboard_changed ()
	{
		contentPresent = ( clipboard != nullptr ) && mime_has_content ( clipboard->mimeData () );

		emit content_changed ();
	}

	//=================================================================================================================
	// Pure encode / decode
	//=================================================================================================================

	QString ClipboardService::cell_plain_text ( const JsonNode& node )
	{
		// Matches tree copy: a string's content unquoted, a number's raw token, true / false, and a container as its
		// JSON text -- the form that reads sensibly when pasted into an external editor.

		switch ( node.kind () )
		{
			case JsonKind::String:  return node.string_value ();
			case JsonKind::Number:  return node.number_token ();
			case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
			case JsonKind::Null:    return QStringLiteral ( "null" );

			case JsonKind::Array:
			case JsonKind::Object:  return JsonSerializer::serialize ( node );
		}

		return QString ();
	}

	void ClipboardService::fill_node_mime ( QMimeData* data, const JsonNode& node, const QString& sourceKey )
	{
		// The exact JSON in the private format (round-trippable, tokens preserved) and, for external targets, the same
		// JSON as plain text.

		const QString json = JsonSerializer::serialize ( node );

		data->setData ( clipboard_mime::VJE_JSON, json.toUtf8 () );
		data->setText ( json );

		if ( !sourceKey.isEmpty () )
		{
			data->setData ( clipboard_mime::VJE_JSON_KEY, sourceKey.toUtf8 () );
		}
	}

	void ClipboardService::fill_cell_mime ( QMimeData* data, const JsonNode& node )
	{
		// The private format is the value's exact JSON so an in-app paste keeps its type; the plain text is the
		// tree-copy form for external targets.

		data->setData ( clipboard_mime::VJE_JSON, JsonSerializer::serialize ( node ).toUtf8 () );
		data->setText ( cell_plain_text ( node ) );
	}

	std::unique_ptr<JsonNode> ClipboardService::value_from_mime ( const QMimeData* data )
	{
		if ( data == nullptr )
		{
			return nullptr;
		}

		// The private format is authoritative: it is exact JSON we wrote, so it parses to its own type. Fall back to the
		// clipboard's plain text (JSON if it parses, else a string) exactly as an external paste resolves.

		if ( data->hasFormat ( clipboard_mime::VJE_JSON ) )
		{
			return CellPasteConverter::resolve_clipboard_text ( QString::fromUtf8 ( data->data ( clipboard_mime::VJE_JSON ) ) );
		}

		if ( data->hasText () && !data->text ().isEmpty () )
		{
			return CellPasteConverter::resolve_clipboard_text ( data->text () );
		}

		return nullptr;
	}

	QString ClipboardService::source_key_from_mime ( const QMimeData* data )
	{
		if ( ( data == nullptr ) || !data->hasFormat ( clipboard_mime::VJE_JSON_KEY ) )
		{
			return QString ();
		}

		return QString::fromUtf8 ( data->data ( clipboard_mime::VJE_JSON_KEY ) );
	}

	bool ClipboardService::mime_has_content ( const QMimeData* data )
	{
		if ( data == nullptr )
		{
			return false;
		}

		return data->hasFormat ( clipboard_mime::VJE_JSON ) || ( data->hasText () && !data->text ().isEmpty () );
	}
}
