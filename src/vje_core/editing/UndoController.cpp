//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   UndoController implementation. See UndoController.hpp for the operation contract and the list of preconditions
//   enforced here.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/editing/UndoController.hpp>
#include <vje_core/editing/EditCommands.hpp>
#include <vje_core/editing/edit_transforms.hpp>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	UndoController::UndoController ( JsonDocument* document, QObject* parent )
		: QObject   ( parent )
		, document  ( document )
		, undoStack ( this )
	{
		// UNDO-04: the dirty flag is the inverse of the stack's clean state. Undoing back to the last-saved point
		// clears the modified indicator; any edit away from it sets it.

		connect
		(
			&undoStack, &QUndoStack::cleanChanged,
			this,       [ this ] ( bool clean ) { this->document->set_dirty ( !clean ); }
		);
	}

	//=================================================================================================================
	// Stack Interface
	//=================================================================================================================

	QUndoStack* UndoController::stack () const
	{
		return const_cast<QUndoStack*> ( &undoStack );
	}

	bool UndoController::can_undo () const
	{
		return undoStack.canUndo ();
	}

	bool UndoController::can_redo () const
	{
		return undoStack.canRedo ();
	}

	void UndoController::undo ()
	{
		undoStack.undo ();
	}

	void UndoController::redo ()
	{
		undoStack.redo ();
	}

	bool UndoController::is_clean () const
	{
		return undoStack.isClean ();
	}

	void UndoController::set_clean ()
	{
		undoStack.setClean ();
	}

	void UndoController::clear ()
	{
		undoStack.clear ();
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	std::unique_ptr<JsonNode> UndoController::make_default ( JsonKind kind )
	{
		switch ( kind )
		{
			case JsonKind::Null:    return JsonNode::make_null ();
			case JsonKind::Boolean: return JsonNode::make_boolean ( false );
			case JsonKind::Number:  return JsonNode::make_number ( QStringLiteral ( "0" ) );
			case JsonKind::String:  return JsonNode::make_string ( QString () );
			case JsonKind::Array:   return JsonNode::make_array ();
			case JsonKind::Object:  return JsonNode::make_object ();
		}

		return JsonNode::make_null ();
	}

	EditOutcome UndoController::insert_at
	(
		const JsonPointer&        parentPointer,
		JsonNode*                 parentNode,
		int                       index,
		const QString&            key,
		std::unique_ptr<JsonNode> node,
		const QString&            text
	)
	{
		const bool parentIsObject = ( parentNode->kind () == JsonKind::Object );

		// VAL-02: an object cannot gain a duplicate sibling key through an edit.

		if ( parentIsObject && parentNode->has_member ( key ) )
		{
			return EditOutcome::Rejected;
		}

		InsertNodeCommand* command = new InsertNodeCommand ( document, parentPointer, index, parentIsObject, key, std::move ( node ) );
		command->setText ( text );

		undoStack.push ( command );

		return EditOutcome::Applied;
	}

	//=================================================================================================================
	// Scalar Value Edits (EDIT-01)
	//=================================================================================================================

	EditOutcome UndoController::set_string ( const JsonPointer& target, const QString& text )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::String ) )
		{
			return EditOutcome::Rejected;
		}

		if ( node->string_value () == text )
		{
			return EditOutcome::Unchanged;
		}

		undoStack.push ( new SetValueCommand ( document, target, JsonNode::make_string ( text ) ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::set_number ( const JsonPointer& target, const QString& token )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Number ) )
		{
			return EditOutcome::Rejected;
		}

		// VAL-03: a number edit must be a valid JSON number, or it is rejected without altering the document.

		if ( !edit_transforms::is_json_number ( token ) )
		{
			return EditOutcome::Rejected;
		}

		if ( node->number_token () == token )
		{
			return EditOutcome::Unchanged;
		}

		undoStack.push ( new SetValueCommand ( document, target, JsonNode::make_number ( token ) ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::set_boolean ( const JsonPointer& target, bool value )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Boolean ) )
		{
			return EditOutcome::Rejected;
		}

		if ( node->boolean_value () == value )
		{
			return EditOutcome::Unchanged;
		}

		undoStack.push ( new SetValueCommand ( document, target, JsonNode::make_boolean ( value ) ) );

		return EditOutcome::Applied;
	}

	//=================================================================================================================
	// Structural Edits
	//=================================================================================================================

	EditOutcome UndoController::rename_key ( const JsonPointer& target, const QString& newKey )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( ( parent == nullptr ) || ( parent->kind () != JsonKind::Object ) )
		{
			return EditOutcome::Rejected;
		}

		const int     index  = node->index_in_parent ();
		const QString oldKey = parent->member_key ( index );

		if ( oldKey == newKey )
		{
			return EditOutcome::Unchanged;
		}

		// VAL-02: newKey differs from oldKey, so any existing occurrence is a genuine collision.

		if ( parent->has_member ( newKey ) )
		{
			return EditOutcome::Rejected;
		}

		undoStack.push ( new RenameKeyCommand ( document, target.parent (), index, newKey ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::add_node ( const JsonPointer& selection, JsonKind kind, const QString& key )
	{
		JsonNode* node = document->resolve ( selection );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		// EDIT-03 placement: a container takes the new node as its last child; a scalar takes it as its next sibling.

		return node->is_container () ? add_child ( selection, kind, key ) : add_sibling ( selection, kind, key );
	}

	EditOutcome UndoController::add_child ( const JsonPointer& container, JsonKind kind, const QString& key )
	{
		JsonNode* node = document->resolve ( container );

		if ( ( node == nullptr ) || !node->is_container () )
		{
			return EditOutcome::Rejected;
		}

		const int index = ( node->kind () == JsonKind::Object ) ? node->member_count () : node->array_size ();

		return insert_at ( container, node, index, key, make_default ( kind ), QStringLiteral ( "Add Node" ) );
	}

	EditOutcome UndoController::add_sibling ( const JsonPointer& target, JsonKind kind, const QString& key )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( parent == nullptr )
		{
			return EditOutcome::Rejected;                          // The root has no sibling.
		}

		const int index = node->index_in_parent () + 1;

		return insert_at ( target.parent (), parent, index, key, make_default ( kind ), QStringLiteral ( "Add Node" ) );
	}

	EditOutcome UndoController::delete_node ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( parent == nullptr )
		{
			return EditOutcome::Rejected;                          // The root is not deletable.
		}

		const bool parentIsObject = ( parent->kind () == JsonKind::Object );
		const int  index          = node->index_in_parent ();

		undoStack.push ( new RemoveNodeCommand ( document, target.parent (), index, parentIsObject ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::duplicate_node ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( parent == nullptr )
		{
			return EditOutcome::Rejected;                          // The root cannot be duplicated in place.
		}

		const bool parentIsObject = ( parent->kind () == JsonKind::Object );
		const int  index          = node->index_in_parent ();

		QString key;

		if ( parentIsObject )
		{
			key = edit_transforms::duplicate_key_name ( *parent, parent->member_key ( index ) );
		}

		// Insert the clone immediately after the original (arrays) / with a de-duplicated key (objects).

		InsertNodeCommand* command =
			new InsertNodeCommand ( document, target.parent (), index + 1, parentIsObject, key, node->clone () );
		command->setText ( QStringLiteral ( "Duplicate Node" ) );

		undoStack.push ( command );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::move_node ( const JsonPointer& target, MoveDirection direction )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		JsonNode* parent = node->parent ();

		if ( parent == nullptr )
		{
			return EditOutcome::Rejected;                          // The root has no siblings to reorder among.
		}

		const int index = node->index_in_parent ();
		const int count = ( parent->kind () == JsonKind::Object ) ? parent->member_count () : parent->array_size ();
		const int to    = ( direction == MoveDirection::Up ) ? ( index - 1 ) : ( index + 1 );

		if ( ( to < 0 ) || ( to >= count ) )
		{
			return EditOutcome::Unchanged;                         // Already at the edge.
		}

		undoStack.push ( new MoveNodeCommand ( document, target.parent (), index, to ) );

		return EditOutcome::Applied;
	}

	EditOutcome UndoController::change_type ( const JsonPointer& target, JsonKind newKind )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		if ( node->kind () == newKind )
		{
			return EditOutcome::Unchanged;                         // Converting to the current type is a no-op.
		}

		return replace_subtree ( target, edit_transforms::convert_node ( *node, newKind ), QStringLiteral ( "Change Type" ) );
	}

	EditOutcome UndoController::normalize_array ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Array ) )
		{
			return EditOutcome::Rejected;
		}

		// EDIT-11 is enabled only for an array whose elements are all objects.

		for ( int index = 0; index < node->array_size (); ++index )
		{
			if ( node->array_element ( index )->kind () != JsonKind::Object )
			{
				return EditOutcome::Rejected;
			}
		}

		return replace_subtree
		(
			target,
			edit_transforms::normalize_array_elements ( *node ),
			QStringLiteral ( "Normalize Array Elements" )
		);
	}

	EditOutcome UndoController::array_to_objects ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Array ) )
		{
			return EditOutcome::Rejected;
		}

		return replace_subtree ( target, edit_transforms::array_to_object ( *node ), QStringLiteral ( "Convert Array to Objects" ) );
	}

	EditOutcome UndoController::objects_to_array ( const JsonPointer& target )
	{
		JsonNode* node = document->resolve ( target );

		if ( ( node == nullptr ) || ( node->kind () != JsonKind::Object ) )
		{
			return EditOutcome::Rejected;
		}

		return replace_subtree ( target, edit_transforms::object_to_array ( *node ), QStringLiteral ( "Convert Objects to Array" ) );
	}

	EditOutcome UndoController::replace_subtree ( const JsonPointer& target, std::unique_ptr<JsonNode> newSubtree, const QString& text )
	{
		JsonNode* node = document->resolve ( target );

		if ( node == nullptr )
		{
			return EditOutcome::Rejected;
		}

		const bool targetIsRoot    = ( node->parent () == nullptr );
		int        childIndex      = -1;
		bool       parentIsObject  = false;

		if ( !targetIsRoot )
		{
			JsonNode* parent = node->parent ();
			childIndex       = node->index_in_parent ();
			parentIsObject   = ( parent->kind () == JsonKind::Object );
		}

		undoStack.push
		(
			new ReplaceNodeCommand
			(
				document, target, childIndex, parentIsObject, targetIsRoot,
				std::move ( newSubtree ), DocumentChange::SubtreeReplaced, text
			)
		);

		return EditOutcome::Applied;
	}
}
