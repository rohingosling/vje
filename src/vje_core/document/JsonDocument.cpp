//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonDocument implementation. See JsonDocument.hpp for the design notes.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonDocument.hpp>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonDocument::JsonDocument ( QObject* parent )
		: QObject    ( parent )
		, dirtyState ( false )
	{
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	JsonNode* JsonDocument::root () const
	{
		return rootNode.get ();
	}

	bool JsonDocument::has_root () const
	{
		return rootNode != nullptr;
	}

	const QString& JsonDocument::file_path () const
	{
		return filePath;
	}

	bool JsonDocument::is_dirty () const
	{
		return dirtyState;
	}

	JsonNode* JsonDocument::resolve ( const JsonPointer& pointer ) const
	{
		return pointer.resolve ( rootNode.get () );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void JsonDocument::set_root ( std::unique_ptr<JsonNode> newRoot )
	{
		rootNode = std::move ( newRoot );

		emit reset ();
	}

	std::unique_ptr<JsonNode> JsonDocument::swap_root ( std::unique_ptr<JsonNode> newRoot )
	{
		std::unique_ptr<JsonNode> old = std::move ( rootNode );
		rootNode = std::move ( newRoot );

		return old;
	}

	void JsonDocument::notify_node_changed ( const JsonPointer& pointer, DocumentChange change )
	{
		emit node_changed ( pointer, change );
	}

	void JsonDocument::set_file_path ( const QString& path )
	{
		if ( filePath == path )
		{
			return;
		}

		filePath = path;

		emit file_path_changed ( filePath );
	}

	void JsonDocument::set_dirty ( bool dirty )
	{
		if ( dirtyState == dirty )
		{
			return;
		}

		dirtyState = dirty;

		emit dirty_changed ( dirtyState );
	}
}
