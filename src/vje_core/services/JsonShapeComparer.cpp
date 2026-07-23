//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonShapeComparer implementation. See JsonShapeComparer.hpp for the EDITOR-11 shape rules.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonShapeComparer.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QSet>
#include <QString>

namespace vje
{
	namespace
	{
		// The key SET of an object (order-insensitive). Duplicate keys collapse -- shape is about which keys exist.

		QSet<QString> key_set ( const JsonNode& object )
		{
			QSet<QString> keys;

			for ( int index = 0; index < object.member_count (); ++index )
			{
				keys.insert ( object.member_key ( index ) );
			}

			return keys;
		}
	}

	//=================================================================================================================
	// Core relation
	//=================================================================================================================

	bool JsonShapeComparer::shapes_compatible ( const JsonNode& a, const JsonNode& b )
	{
		// null is a wildcard at every level.

		if ( ( a.kind () == JsonKind::Null ) || ( b.kind () == JsonKind::Null ) )
		{
			return true;
		}

		if ( a.kind () != b.kind () )
		{
			return false;
		}

		switch ( a.kind () )
		{
			case JsonKind::String:
			case JsonKind::Number:
			case JsonKind::Boolean:
			{
				return true;                               // Same scalar kind -- values are irrelevant to shape.
			}

			case JsonKind::Object:
			{
				if ( key_set ( a ) != key_set ( b ) )
				{
					return false;                          // Recursive, order-insensitive key-set equality.
				}

				for ( int index = 0; index < a.member_count (); ++index )
				{
					const QString&  key      = a.member_key ( index );
					const JsonNode* other    = b.find_member ( key );
					const JsonNode* mine     = a.member_value ( index );

					if ( ( other == nullptr ) || !shapes_compatible ( *mine, *other ) )
					{
						return false;
					}
				}

				return true;
			}

			case JsonKind::Array:
			{
				// Empty array matches any array; length is ignored -- compare the representative element shapes.

				if ( ( a.array_size () == 0 ) || ( b.array_size () == 0 ) )
				{
					return true;
				}

				return shapes_compatible ( *a.array_element ( 0 ), *b.array_element ( 0 ) );
			}

			case JsonKind::Null:
			{
				return true;                               // Unreachable (handled above); keeps the switch total.
			}
		}

		return false;
	}

	//=================================================================================================================
	// Column checks
	//=================================================================================================================

	bool JsonShapeComparer::column_is_homogeneous ( const std::vector<const JsonNode*>& columnValues )
	{
		const JsonNode* reference = nullptr;

		for ( const JsonNode* value : columnValues )
		{
			if ( ( value == nullptr ) || ( value->kind () == JsonKind::Null ) )
			{
				continue;                                  // null values impose no shape.
			}

			if ( reference == nullptr )
			{
				reference = value;
			}
			else if ( !shapes_compatible ( *reference, *value ) )
			{
				return false;                              // Two incompatible non-null values => heterogeneous.
			}
		}

		return true;
	}

	bool JsonShapeComparer::compatible_with_column ( const JsonNode& candidate, const std::vector<const JsonNode*>& columnValues )
	{
		if ( !column_is_homogeneous ( columnValues ) )
		{
			return true;                                   // Already-jagged column: the check is waived.
		}

		for ( const JsonNode* value : columnValues )
		{
			if ( ( value != nullptr ) && ( value->kind () != JsonKind::Null ) )
			{
				return shapes_compatible ( candidate, *value );   // First non-null value is the reference shape.
			}
		}

		return true;                                       // Empty / all-null column: no constraint.
	}
}
