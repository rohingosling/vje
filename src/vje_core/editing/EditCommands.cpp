//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include <algorithm>

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

	void EditCommand::mark_child ( const JsonPointer& parentPointer, int index ) const
	{
		const JsonNode* const parent = node_at ( parentPointer );

		if ( parent == nullptr )
		{
			return;
		}

		document->mark_changed ( ( parent->kind () == JsonKind::Object ) ? parent->member_value ( index ) : parent->array_element ( index ) );
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
		node_at    ( parentPointer )->set_member_key ( index, newKey );
		mark_child ( parentPointer, index );
		notify     ( parentPointer, DocumentChange::KeyRenamed );
	}

	void RenameKeyCommand::undo ()
	{
		node_at    ( parentPointer )->set_member_key ( index, oldKey );
		mark_child ( parentPointer, index );
		notify     ( parentPointer, DocumentChange::KeyRenamed );
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

		mark_child ( parentPointer, index );
		notify     ( parentPointer, DocumentChange::NodeAdded );
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

		mark_child ( parentPointer, index );
		notify     ( parentPointer, DocumentChange::NodeAdded );
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
		node_at    ( parentPointer )->move_child ( fromIndex, toIndex );
		mark_child ( parentPointer, toIndex );
		notify     ( parentPointer, DocumentChange::NodeMoved );
	}

	void MoveNodeCommand::undo ()
	{
		node_at    ( parentPointer )->move_child ( toIndex, fromIndex );
		mark_child ( parentPointer, fromIndex );
		notify     ( parentPointer, DocumentChange::NodeMoved );
	}

	//=================================================================================================================
	// SortArrayCommand -- EDIT-15
	//=================================================================================================================

	SortArrayCommand::SortArrayCommand ( JsonDocument* document, const JsonPointer& arrayPointer, std::vector<int> permutation )
		: EditCommand   ( document, QStringLiteral ( "Sort Array" ) )
		, arrayPointer  ( arrayPointer )
		, permutation   ( std::move ( permutation ) )
	{
		// inversePermutation [ oldIndex ] = newIndex, which is exactly the order undo has to apply to put every
		// element back where it started. Built here, once, rather than derived on each undo.

		inversePermutation.resize ( this->permutation.size () );

		for ( std::size_t newIndex = 0; newIndex < this->permutation.size (); ++newIndex )
		{
			inversePermutation [ static_cast<std::size_t> ( this->permutation [ newIndex ] ) ] = static_cast<int> ( newIndex );
		}
	}

	void SortArrayCommand::apply ( const std::vector<int>& order ) const
	{
		JsonNode* const array = node_at ( arrayPointer );

		// Every element is detached FIRST and then re-inserted in the requested order. Rearranging in place would
		// mean each move renumbering the elements the rest of the order still refers to -- the positional-pointer
		// trap (Q22) in its purest form, since here the indices are the whole of the plan.

		std::vector<std::unique_ptr<JsonNode>> elements;
		elements.reserve ( order.size () );

		for ( int index = array->array_size () - 1; index >= 0; --index )
		{
			elements.push_back ( array->take_element ( index ) );
		}

		std::reverse ( elements.begin (), elements.end () );

		for ( std::size_t position = 0; position < order.size (); ++position )
		{
			array->insert_element
			(
				static_cast<int> ( position ),
				std::move ( elements [ static_cast<std::size_t> ( order [ position ] ) ] )
			);

			// TREE-10. An element is changed where the sort moved it: [n] is a position, and it now holds something
			// else. One that stayed put is not, which is what leaves an already-sorted run unmarked.

			if ( order [ position ] != static_cast<int> ( position ) )
			{
				document->mark_changed ( array->array_element ( static_cast<int> ( position ) ) );
			}
		}

		notify ( arrayPointer, DocumentChange::NodeMoved );
	}

	void SortArrayCommand::redo ()
	{
		apply ( permutation );
	}

	void SortArrayCommand::undo ()
	{
		apply ( inversePermutation );
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

		// TREE-10. stashed now holds what was replaced, so the installed node can be compared with it -- and it is the
		// same comparison either way round, which is what makes an undo mark the places its redo marked. A Code View
		// commit therefore marks what the edit changed rather than the whole document.

		JsonNode* const installed = node_at ( target );

		if ( ( installed != nullptr ) && ( stashed != nullptr ) )
		{
			document->mark_differences ( *stashed, *installed );
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
