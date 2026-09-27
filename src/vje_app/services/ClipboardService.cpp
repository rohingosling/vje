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
#include <vje_core/services/JsonParser.hpp>
#include <vje_core/services/JsonSerializer.hpp>

#include <QClipboard>
#include <QMimeData>
#include <QStringList>

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

	void ClipboardService::copy_nodes ( const QList<ClipboardNodeRef>& nodes )
	{
		if ( ( clipboard == nullptr ) || nodes.isEmpty () )
		{
			return;
		}

		QMimeData* const data = new QMimeData ();

		fill_node_list_mime ( data, nodes );

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

	void ClipboardService::copy_table_selection
	(
		TableSelectionShape                   shape,
		const std::vector<TableSelectionRef>& cells,
		const QString&                        plainText,
		bool                                  keyed
	)
	{
		if ( clipboard == nullptr )
		{
			return;
		}

		QMimeData* const data = new QMimeData ();

		fill_table_mime ( data, shape, cells, plainText, keyed );

		clipboard->setMimeData ( data );
	}

	//=================================================================================================================
	// Paste
	//=================================================================================================================

	TableSelectionValue ClipboardService::table_selection () const
	{
		return ( clipboard != nullptr ) ? table_selection_from_mime ( clipboard->mimeData () ) : TableSelectionValue ();
	}

	std::unique_ptr<JsonNode> ClipboardService::value () const
	{
		return ( clipboard != nullptr ) ? value_from_mime ( clipboard->mimeData () ) : nullptr;
	}

	std::vector<ClipboardNodeValue> ClipboardService::value_list () const
	{
		return ( clipboard != nullptr ) ? value_list_from_mime ( clipboard->mimeData () ) : std::vector<ClipboardNodeValue> ();
	}

	QString ClipboardService::source_key () const
	{
		return ( clipboard != nullptr ) ? source_key_from_mime ( clipboard->mimeData () ) : QString ();
	}

	QString ClipboardService::plain_text () const
	{
		// Read straight from the clipboard, unparsed -- see the header. This is asked on a Ctrl+V gesture rather than
		// on every enablement recompute, so it does not need has_content()'s cache.

		return ( clipboard != nullptr ) ? clipboard->text () : QString ();
	}

	QString ClipboardService::external_text () const
	{
		return ( clipboard != nullptr ) ? external_text_from_mime ( clipboard->mimeData () ) : QString ();
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

	void ClipboardService::fill_node_list_mime ( QMimeData* data, const QList<ClipboardNodeRef>& nodes )
	{
		if ( nodes.isEmpty () )
		{
			return;
		}

		// A LIST OF ONE IS A SINGLE-NODE COPY. See the header: this is what keeps every pre-EDIT-14 paste route
		// working unchanged, and it is stated once, here, rather than at each caller that might have one node.

		if ( nodes.size () == 1 )
		{
			fill_node_mime ( data, *nodes.first ().node, nodes.first ().key );

			return;
		}

		// The payload is itself JSON -- an array of { "key": ..., "value": ... } entries -- so it is written by the
		// serializer and read by the parser rather than by a bespoke codec. The subtrees nest inside it untouched, so
		// FILE-10's raw number tokens survive the round trip for the same reason they do in the single format.
		//
		// "key" is ABSENT for an array element rather than empty, because an object member CAN legitimately be keyed
		// with the empty string, and a format in which the two are the same value cannot paste that member back.

		std::unique_ptr<JsonNode> payload = JsonNode::make_array ();

		for ( const ClipboardNodeRef& entry : nodes )
		{
			std::unique_ptr<JsonNode> item = JsonNode::make_object ();

			if ( !entry.key.isNull () )
			{
				item->append_member ( QStringLiteral ( "key" ), JsonNode::make_string ( entry.key ) );
			}

			item->append_member ( QStringLiteral ( "value" ), entry.node->clone () );

			payload->append_element ( std::move ( item ) );
		}

		data->setData ( clipboard_mime::VJE_JSON_LIST, JsonSerializer::serialize ( *payload ).toUtf8 () );
		data->setText ( node_list_plain_text ( nodes ) );
	}

	QString ClipboardService::node_list_plain_text ( const QList<ClipboardNodeRef>& nodes )
	{
		QStringList lines;

		lines.reserve ( nodes.size () );

		for ( const ClipboardNodeRef& entry : nodes )
		{
			const QString json = JsonSerializer::serialize ( *entry.node );

			// A member keeps its name, an element does not have one. The key is re-encoded through a string node so
			// its quoting and escaping are the serializer's rather than a second opinion formed here.

			lines.append
			(
				entry.key.isNull ()
					? json
					: ( JsonSerializer::serialize ( *JsonNode::make_string ( entry.key ) ) + QStringLiteral ( ": " ) + json )
			);
		}

		return lines.join ( QStringLiteral ( ",\n" ) );
	}

	void ClipboardService::fill_table_mime
	(
		QMimeData*                            data,
		TableSelectionShape                   shape,
		const std::vector<TableSelectionRef>& cells,
		const QString&                        plainText,
		bool                                  keyed
	)
	{
		if ( cells.empty () )
		{
			return;
		}

		// The payload is itself JSON, written by the serializer and read by the parser, so the cell subtrees nest
		// inside it untouched and FILE-10's raw number tokens survive the round trip -- the list format's reason.
		//
		// An ABSENT cell is an entry with NO "value" member rather than one holding null, because EDITOR-18 treats the
		// two differently on paste: an absent source leaves its target alone, a null one overwrites it. The two must
		// therefore be distinguishable in the format as well as in memory.
		//
		// "key" is likewise ABSENT rather than empty when the source column had none, for the reason the node list
		// writes an element's key absent: "" is a legal member key, so a format in which the two are the same value
		// could not carry a column named with the empty string.

		std::unique_ptr<JsonNode> payload = JsonNode::make_object ();

		payload->append_member
		(
			QStringLiteral ( "shape" ),
			JsonNode::make_string
			(
				( shape == TableSelectionShape::Row ) ? QStringLiteral ( "row" ) : QStringLiteral ( "column" )
			)
		);

		std::unique_ptr<JsonNode> entries = JsonNode::make_array ();

		for ( const TableSelectionRef& cell : cells )
		{
			std::unique_ptr<JsonNode> entry = JsonNode::make_object ();

			if ( !cell.key.isNull () )
			{
				entry->append_member ( QStringLiteral ( "key" ), JsonNode::make_string ( cell.key ) );
			}

			if ( cell.value != nullptr )
			{
				entry->append_member ( QStringLiteral ( "value" ), cell.value->clone () );
			}

			entries->append_element ( std::move ( entry ) );
		}

		payload->append_member ( QStringLiteral ( "cells" ), std::move ( entries ) );

		// Written only when FALSE, so a payload without it reads as the keyed rows every earlier copy produced.

		if ( !keyed )
		{
			payload->append_member ( QStringLiteral ( "keyed" ), JsonNode::make_boolean ( false ) );
		}

		data->setData ( clipboard_mime::VJE_JSON_TABLE, JsonSerializer::serialize ( *payload ).toUtf8 () );
		data->setText ( plainText );
	}

	TableSelectionValue ClipboardService::table_selection_from_mime ( const QMimeData* data )
	{
		TableSelectionValue selection;

		// ONLY the private table format answers, never plain text -- see the header.

		if ( ( data == nullptr ) || !data->hasFormat ( clipboard_mime::VJE_JSON_TABLE ) )
		{
			return selection;
		}

		ParseResult parsed = JsonParser::parse ( QString::fromUtf8 ( data->data ( clipboard_mime::VJE_JSON_TABLE ) ) );

		if ( !parsed.ok || ( parsed.root == nullptr ) || ( parsed.root->kind () != JsonKind::Object ) )
		{
			return selection;
		}

		const JsonNode* const shape   = parsed.root->find_member ( QStringLiteral ( "shape" ) );
		JsonNode* const       entries = parsed.root->find_member ( QStringLiteral ( "cells" ) );

		if ( ( shape == nullptr ) || ( shape->kind () != JsonKind::String )
		  || ( entries == nullptr ) || ( entries->kind () != JsonKind::Array ) )
		{
			return selection;
		}

		selection.shape = ( shape->string_value () == QStringLiteral ( "column" ) )
		                ? TableSelectionShape::Column
		                : TableSelectionShape::Row;

		const JsonNode* const keyed = parsed.root->find_member ( QStringLiteral ( "keyed" ) );

		selection.keyed = !( ( keyed != nullptr ) && ( keyed->kind () == JsonKind::Boolean ) && !keyed->boolean_value () );

		for ( int index = 0; index < entries->array_size (); ++index )
		{
			const JsonNode* const entry = entries->array_element ( index );

			if ( ( entry == nullptr ) || ( entry->kind () != JsonKind::Object ) )
			{
				selection.cells.push_back ( TableSelectionCell {} );

				continue;
			}

			const JsonNode* const value = entry->find_member ( QStringLiteral ( "value" ) );
			const JsonNode* const key   = entry->find_member ( QStringLiteral ( "key" ) );

			TableSelectionCell cell;

			cell.value = ( value != nullptr ) ? value->clone () : nullptr;
			cell.key   = ( ( key != nullptr ) && ( key->kind () == JsonKind::String ) ) ? key->string_value () : QString ();

			selection.cells.push_back ( std::move ( cell ) );
		}

		return selection;
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

	QString ClipboardService::external_text_from_mime ( const QMimeData* data )
	{
		if ( data == nullptr )
		{
			return QString ();
		}

		// Any private format means the copy was VJE's own, whatever text rides alongside it -- see the header.

		for ( const QString& format : { clipboard_mime::VJE_JSON, clipboard_mime::VJE_JSON_LIST, clipboard_mime::VJE_JSON_TABLE } )
		{
			if ( data->hasFormat ( format ) )
			{
				return QString ();
			}
		}

		return data->hasText () ? data->text () : QString ();
	}

	std::vector<ClipboardNodeValue> ClipboardService::value_list_from_mime ( const QMimeData* data )
	{
		std::vector<ClipboardNodeValue> entries;

		// ONLY the private list format answers here. Plain text is deliberately not a fallback: an external editor's
		// text is one value, and reading a copied JSON array as "several nodes" would turn a paste of one array into
		// a paste of its elements -- a different edit, from a gesture that looked the same.

		if ( ( data == nullptr ) || !data->hasFormat ( clipboard_mime::VJE_JSON_LIST ) )
		{
			return entries;
		}

		ParseResult parsed = JsonParser::parse ( QString::fromUtf8 ( data->data ( clipboard_mime::VJE_JSON_LIST ) ) );

		if ( !parsed.ok || ( parsed.root == nullptr ) || ( parsed.root->kind () != JsonKind::Array ) )
		{
			return entries;
		}

		for ( int index = 0; index < parsed.root->array_size (); ++index )
		{
			JsonNode* const item = parsed.root->array_element ( index );

			if ( ( item == nullptr ) || ( item->kind () != JsonKind::Object ) )
			{
				continue;
			}

			JsonNode* const value = item->find_member ( QStringLiteral ( "value" ) );

			if ( value == nullptr )
			{
				continue;
			}

			JsonNode* const key = item->find_member ( QStringLiteral ( "key" ) );

			ClipboardNodeValue entry;

			entry.node = value->clone ();
			entry.key  = ( ( key != nullptr ) && ( key->kind () == JsonKind::String ) ) ? key->string_value () : QString ();

			entries.push_back ( std::move ( entry ) );
		}

		return entries;
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

		return data->hasFormat ( clipboard_mime::VJE_JSON )
		    || data->hasFormat ( clipboard_mime::VJE_JSON_LIST )
		    || data->hasFormat ( clipboard_mime::VJE_JSON_TABLE )
		    || ( data->hasText () && !data->text ().isEmpty () );
	}
}
