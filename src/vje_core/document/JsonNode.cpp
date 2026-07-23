//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonNode implementation. See JsonNode.hpp for the design notes (order preservation, raw number tokens,
//   unique_ptr ownership with non-owning parent back-pointers).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonNode.hpp>

namespace vje
{
	//=================================================================================================================
	// Constructors / Factories
	//=================================================================================================================

	JsonNode::JsonNode ( JsonKind kind )
		: nodeKind     ( kind )
		, parentNode   ( nullptr )
		, booleanValue ( false )
	{
	}

	std::unique_ptr<JsonNode> JsonNode::make_null ()
	{
		return std::make_unique<JsonNode> ( JsonKind::Null );
	}

	std::unique_ptr<JsonNode> JsonNode::make_boolean ( bool value )
	{
		auto node = std::make_unique<JsonNode> ( JsonKind::Boolean );
		node->booleanValue = value;
		return node;
	}

	std::unique_ptr<JsonNode> JsonNode::make_number ( const QString& rawToken )
	{
		auto node = std::make_unique<JsonNode> ( JsonKind::Number );
		node->scalarText = rawToken;
		return node;
	}

	std::unique_ptr<JsonNode> JsonNode::make_string ( const QString& value )
	{
		auto node = std::make_unique<JsonNode> ( JsonKind::String );
		node->scalarText = value;
		return node;
	}

	std::unique_ptr<JsonNode> JsonNode::make_array ()
	{
		return std::make_unique<JsonNode> ( JsonKind::Array );
	}

	std::unique_ptr<JsonNode> JsonNode::make_object ()
	{
		return std::make_unique<JsonNode> ( JsonKind::Object );
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	JsonKind JsonNode::kind () const
	{
		return nodeKind;
	}

	JsonNode* JsonNode::parent () const
	{
		return parentNode;
	}

	int JsonNode::index_in_parent () const
	{
		if ( parentNode == nullptr )
		{
			return -1;
		}

		const std::vector<std::unique_ptr<JsonNode>>& siblings = parentNode->children;

		for ( std::size_t i = 0; i < siblings.size (); ++i )
		{
			if ( siblings [ i ].get () == this )
			{
				return static_cast<int> ( i );
			}
		}

		return -1;
	}

	bool JsonNode::is_scalar () const
	{
		return ( nodeKind != JsonKind::Array ) && ( nodeKind != JsonKind::Object );
	}

	bool JsonNode::is_container () const
	{
		return ( nodeKind == JsonKind::Array ) || ( nodeKind == JsonKind::Object );
	}

	bool JsonNode::boolean_value () const
	{
		return booleanValue;
	}

	const QString& JsonNode::number_token () const
	{
		return scalarText;
	}

	const QString& JsonNode::string_value () const
	{
		return scalarText;
	}

	int JsonNode::array_size () const
	{
		return static_cast<int> ( children.size () );
	}

	JsonNode* JsonNode::array_element ( int index ) const
	{
		if ( ( index < 0 ) || ( index >= static_cast<int> ( children.size () ) ) )
		{
			return nullptr;
		}

		return children [ static_cast<std::size_t> ( index ) ].get ();
	}

	int JsonNode::member_count () const
	{
		return static_cast<int> ( children.size () );
	}

	const QString& JsonNode::member_key ( int index ) const
	{
		return memberKeys [ index ];
	}

	JsonNode* JsonNode::member_value ( int index ) const
	{
		if ( ( index < 0 ) || ( index >= static_cast<int> ( children.size () ) ) )
		{
			return nullptr;
		}

		return children [ static_cast<std::size_t> ( index ) ].get ();
	}

	JsonNode* JsonNode::find_member ( const QString& key ) const
	{
		const int count = static_cast<int> ( memberKeys.size () );

		for ( int i = 0; i < count; ++i )
		{
			if ( memberKeys [ i ] == key )
			{
				return children [ static_cast<std::size_t> ( i ) ].get ();
			}
		}

		return nullptr;
	}

	bool JsonNode::has_member ( const QString& key ) const
	{
		return memberKeys.contains ( key );
	}

	int JsonNode::key_count ( const QString& key ) const
	{
		return static_cast<int> ( memberKeys.count ( key ) );
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void JsonNode::set_boolean_value ( bool value )
	{
		booleanValue = value;
	}

	void JsonNode::set_number_token ( const QString& rawToken )
	{
		scalarText = rawToken;
	}

	void JsonNode::set_string_value ( const QString& value )
	{
		scalarText = value;
	}

	JsonNode* JsonNode::append_element ( std::unique_ptr<JsonNode> element )
	{
		element->parentNode = this;

		JsonNode* raw = element.get ();
		children.push_back ( std::move ( element ) );

		return raw;
	}

	JsonNode* JsonNode::append_member ( const QString& key, std::unique_ptr<JsonNode> value )
	{
		value->parentNode = this;

		JsonNode* raw = value.get ();
		memberKeys.append ( key );
		children.push_back ( std::move ( value ) );

		return raw;
	}

	//=================================================================================================================
	// Structural Mutators (used by the edit commands)
	//=================================================================================================================

	void JsonNode::insert_element ( int index, std::unique_ptr<JsonNode> element )
	{
		element->parentNode = this;

		children.insert ( children.begin () + index, std::move ( element ) );
	}

	std::unique_ptr<JsonNode> JsonNode::take_element ( int index )
	{
		if ( ( index < 0 ) || ( index >= static_cast<int> ( children.size () ) ) )
		{
			return nullptr;
		}

		std::unique_ptr<JsonNode> node = std::move ( children [ static_cast<std::size_t> ( index ) ] );
		children.erase ( children.begin () + index );

		node->parentNode = nullptr;

		return node;
	}

	std::unique_ptr<JsonNode> JsonNode::replace_element ( int index, std::unique_ptr<JsonNode> element )
	{
		element->parentNode = this;

		std::unique_ptr<JsonNode> old = std::move ( children [ static_cast<std::size_t> ( index ) ] );
		old->parentNode = nullptr;

		children [ static_cast<std::size_t> ( index ) ] = std::move ( element );

		return old;
	}

	void JsonNode::insert_member ( int index, const QString& key, std::unique_ptr<JsonNode> value )
	{
		value->parentNode = this;

		memberKeys.insert ( index, key );
		children.insert ( children.begin () + index, std::move ( value ) );
	}

	std::unique_ptr<JsonNode> JsonNode::take_member ( int index, QString* outKey )
	{
		if ( ( index < 0 ) || ( index >= static_cast<int> ( children.size () ) ) )
		{
			return nullptr;
		}

		if ( outKey != nullptr )
		{
			*outKey = memberKeys [ index ];
		}

		memberKeys.removeAt ( index );

		std::unique_ptr<JsonNode> node = std::move ( children [ static_cast<std::size_t> ( index ) ] );
		children.erase ( children.begin () + index );

		node->parentNode = nullptr;

		return node;
	}

	std::unique_ptr<JsonNode> JsonNode::replace_member_value ( int index, std::unique_ptr<JsonNode> value )
	{
		value->parentNode = this;

		std::unique_ptr<JsonNode> old = std::move ( children [ static_cast<std::size_t> ( index ) ] );
		old->parentNode = nullptr;

		children [ static_cast<std::size_t> ( index ) ] = std::move ( value );

		return old;
	}

	void JsonNode::set_member_key ( int index, const QString& key )
	{
		memberKeys [ index ] = key;
	}

	void JsonNode::move_child ( int fromIndex, int toIndex )
	{
		if ( fromIndex == toIndex )
		{
			return;
		}

		// Move the child value, and -- for an object -- its parallel key, so keys and values stay in lockstep.

		std::unique_ptr<JsonNode> child = std::move ( children [ static_cast<std::size_t> ( fromIndex ) ] );
		children.erase ( children.begin () + fromIndex );
		children.insert ( children.begin () + toIndex, std::move ( child ) );

		if ( nodeKind == JsonKind::Object )
		{
			const QString key = memberKeys [ fromIndex ];
			memberKeys.removeAt ( fromIndex );
			memberKeys.insert ( toIndex, key );
		}
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	std::unique_ptr<JsonNode> JsonNode::clone () const
	{
		auto copy = std::make_unique<JsonNode> ( nodeKind );

		copy->scalarText   = scalarText;
		copy->booleanValue = booleanValue;
		copy->memberKeys   = memberKeys;

		copy->children.reserve ( children.size () );

		for ( const std::unique_ptr<JsonNode>& child : children )
		{
			std::unique_ptr<JsonNode> childCopy = child->clone ();
			childCopy->parentNode = copy.get ();
			copy->children.push_back ( std::move ( childCopy ) );
		}

		return copy;
	}

	bool JsonNode::equals ( const JsonNode& other ) const
	{
		if ( nodeKind != other.nodeKind )
		{
			return false;
		}

		switch ( nodeKind )
		{
			case JsonKind::Null:
			{
				return true;
			}

			case JsonKind::Boolean:
			{
				return booleanValue == other.booleanValue;
			}

			case JsonKind::Number:
			case JsonKind::String:
			{
				return scalarText == other.scalarText;
			}

			case JsonKind::Array:
			{
				if ( children.size () != other.children.size () )
				{
					return false;
				}

				for ( std::size_t i = 0; i < children.size (); ++i )
				{
					if ( !children [ i ]->equals ( *other.children [ i ] ) )
					{
						return false;
					}
				}

				return true;
			}

			case JsonKind::Object:
			{
				if ( children.size () != other.children.size () )
				{
					return false;
				}

				// Member order is significant (FILE-04): compare keys and values positionally.

				if ( memberKeys != other.memberKeys )
				{
					return false;
				}

				for ( std::size_t i = 0; i < children.size (); ++i )
				{
					if ( !children [ i ]->equals ( *other.children [ i ] ) )
					{
						return false;
					}
				}

				return true;
			}
		}

		return false;
	}
}
