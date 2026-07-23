//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   EditCommands implementation. See EditCommands.hpp for the design notes (pointer targeting, ownership across the
//   redo/undo cycle, and the no-dirty-side-effect discipline).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/editing/EditCommands.hpp>

namespace vje
{
	//=================================================================================================================
	// EditCommand -- base
	//=================================================================================================================

	EditCommand::EditCommand ( JsonDocument* document, const QString& text )
		: QUndoCommand ( text )
		, document     ( document )
	{
	}

	JsonNode* EditCommand::node_at ( const JsonPointer& pointer ) const
	{
		return document->resolve ( pointer );
	}

	void EditCommand::notify ( const JsonPointer& pointer, DocumentChange change ) const
	{
		document->notify_node_changed ( pointer, change );
	}

	//=================================================================================================================
	// SetValueCommand -- EDIT-01
	//=================================================================================================================

	SetValueCommand::SetValueCommand ( JsonDocument* document, const JsonPointer& target, std::unique_ptr<JsonNode> newScalar )
		: EditCommand ( document, QStringLiteral ( "Edit Value" ) )
		, target      ( target )
		, newScalar   ( std::move ( newScalar ) )
	{
		JsonNode* node = node_at ( target );

		if ( node != nullptr )
		{
			oldScalar = node->clone ();
		}
	}

	void SetValueCommand::assign ( const JsonNode& source ) const
	{
		JsonNode* node = node_at ( target );

		if ( node == nullptr )
		{
			return;
		}

		switch ( source.kind () )
		{
			case JsonKind::Boolean: node->set_boolean_value ( source.boolean_value () ); break;
			case JsonKind::Number:  node->set_number_token  ( source.number_token () );  break;
			case JsonKind::String:  node->set_string_value  ( source.string_value () );  break;
			default:                                                                     break;  // Null: nothing to set.
		}
	}

	void SetValueCommand::redo ()
	{
		assign ( *newScalar );
		notify ( target, DocumentChange::ValueChanged );
	}

	void SetValueCommand::undo ()
	{
		assign ( *oldScalar );
		notify ( target, DocumentChange::ValueChanged );
	}

	//=================================================================================================================
	// RenameKeyCommand -- EDIT-02
	//=================================================================================================================

	RenameKeyCommand::RenameKeyCommand ( JsonDocument* document, const JsonPointer& parentPointer, int index, const QString& newKey )
		: EditCommand   ( document, QStringLiteral ( "Rename Key" ) )
		, parentPointer ( parentPointer )
		, index         ( index )
		, newKey        ( newKey )
	{
		JsonNode* parent = node_at ( parentPointer );

		if ( parent != nullptr )
		{
			oldKey = parent->member_key ( index );
		}
	}

	void RenameKeyCommand::redo ()
	{
		node_at ( parentPointer )->set_member_key ( index, newKey );
		notify  ( parentPointer, DocumentChange::KeyRenamed );
	}

	void RenameKeyCommand::undo ()
	{
		node_at ( parentPointer )->set_member_key ( index, oldKey );
		notify  ( parentPointer, DocumentChange::KeyRenamed );
	}

	//=================================================================================================================
	// InsertNodeCommand -- EDIT-03/04 add, EDIT-07 duplicate
	//=================================================================================================================

	InsertNodeCommand::InsertNodeCommand
	(
		JsonDocument*             document,
		const JsonPointer&        parentPointer,
		int                       index,
		bool                      parentIsObject,
		const QString&            key,
		std::unique_ptr<JsonNode> node
	)
		: EditCommand     ( document, QStringLiteral ( "Add Node" ) )
		, parentPointer   ( parentPointer )
		, index           ( index )
		, parentIsObject  ( parentIsObject )
		, key             ( key )
		, stashed         ( std::move ( node ) )
	{
	}

	void InsertNodeCommand::redo ()
	{
		JsonNode* parent = node_at ( parentPointer );

		if ( parentIsObject )
		{
			parent->insert_member ( index, key, std::move ( stashed ) );
		}
		else
		{
			parent->insert_element ( index, std::move ( stashed ) );
		}

		notify ( parentPointer, DocumentChange::NodeAdded );
	}

	void InsertNodeCommand::undo ()
	{
		JsonNode* parent = node_at ( parentPointer );

		stashed = parentIsObject ? parent->take_member ( index ) : parent->take_element ( index );

		notify ( parentPointer, DocumentChange::NodeRemoved );
	}

	//=================================================================================================================
	// RemoveNodeCommand -- EDIT-05
	//=================================================================================================================

	RemoveNodeCommand::RemoveNodeCommand ( JsonDocument* document, const JsonPointer& parentPointer, int index, bool parentIsObject )
		: EditCommand    ( document, QStringLiteral ( "Delete Node" ) )
		, parentPointer  ( parentPointer )
		, index          ( index )
		, parentIsObject ( parentIsObject )
	{
		if ( parentIsObject )
		{
			JsonNode* parent = node_at ( parentPointer );

			if ( parent != nullptr )
			{
				key = parent->member_key ( index );
			}
		}
	}

	void RemoveNodeCommand::redo ()
	{
		JsonNode* parent = node_at ( parentPointer );

		stashed = parentIsObject ? parent->take_member ( index ) : parent->take_element ( index );

		notify ( parentPointer, DocumentChange::NodeRemoved );
	}

	void RemoveNodeCommand::undo ()
	{
		JsonNode* parent = node_at ( parentPointer );

		if ( parentIsObject )
		{
			parent->insert_member ( index, key, std::move ( stashed ) );
		}
		else
		{
			parent->insert_element ( index, std::move ( stashed ) );
		}

		notify ( parentPointer, DocumentChange::NodeAdded );
	}

	//=================================================================================================================
	// MoveNodeCommand -- EDIT-08
	//=================================================================================================================

	MoveNodeCommand::MoveNodeCommand ( JsonDocument* document, const JsonPointer& parentPointer, int fromIndex, int toIndex )
		: EditCommand   ( document, QStringLiteral ( "Move Node" ) )
		, parentPointer ( parentPointer )
		, fromIndex     ( fromIndex )
		, toIndex       ( toIndex )
	{
	}

	void MoveNodeCommand::redo ()
	{
		node_at ( parentPointer )->move_child ( fromIndex, toIndex );
		notify  ( parentPointer, DocumentChange::NodeMoved );
	}

	void MoveNodeCommand::undo ()
	{
		node_at ( parentPointer )->move_child ( toIndex, fromIndex );
		notify  ( parentPointer, DocumentChange::NodeMoved );
	}

	//=================================================================================================================
	// ReplaceNodeCommand -- EDIT-09 type change, EDIT-11..13 transforms, Code View commit
	//=================================================================================================================

	ReplaceNodeCommand::ReplaceNodeCommand
	(
		JsonDocument*             document,
		const JsonPointer&        target,
		int                       childIndex,
		bool                      parentIsObject,
		bool                      targetIsRoot,
		std::unique_ptr<JsonNode> newNode,
		DocumentChange            change,
		const QString&            text
	)
		: EditCommand     ( document, text )
		, target          ( target )
		, childIndex      ( childIndex )
		, parentIsObject  ( parentIsObject )
		, targetIsRoot    ( targetIsRoot )
		, change          ( change )
		, stashed         ( std::move ( newNode ) )
	{
	}

	void ReplaceNodeCommand::swap ()
	{
		// redo and undo are the same operation: exchange the installed node with the stashed one. After the first
		// call the tree holds the new node and stashed holds the old; the next call reverses it.

		if ( targetIsRoot )
		{
			stashed = document->swap_root ( std::move ( stashed ) );
		}
		else
		{
			JsonNode* parent = node_at ( target.parent () );

			stashed = parentIsObject
			        ? parent->replace_member_value ( childIndex, std::move ( stashed ) )
			        : parent->replace_element      ( childIndex, std::move ( stashed ) );
		}

		notify ( target, change );
	}

	void ReplaceNodeCommand::redo ()
	{
		swap ();
	}

	void ReplaceNodeCommand::undo ()
	{
		swap ();
	}
}
