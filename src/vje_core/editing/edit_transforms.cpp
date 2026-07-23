//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   edit_transforms implementation. See edit_transforms.hpp for the rule references (EDIT-07/09/11/12/13).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/editing/edit_transforms.hpp>
#include <vje_core/services/JsonLexer.hpp>

namespace vje
{
	namespace edit_transforms
	{
		//=============================================================================================================
		// Number recognition
		//=============================================================================================================

		bool is_json_number ( const QString& text )
		{
			if ( text.isEmpty () )
			{
				return false;
			}

			// A well-formed Number token spans the whole string with no leading whitespace and nothing after it.

			JsonLexer lexer ( text );

			const JsonToken first = lexer.next ();
			const JsonToken after = lexer.next ();

			const bool spansAll = ( first.kind == JsonTokenKind::Number )
			                   && ( first.offset == 0 )
			                   && ( first.length == static_cast<int> ( text.length () ) );

			return spansAll && ( after.kind == JsonTokenKind::EndOfInput );
		}

		//=============================================================================================================
		// Type conversion (EDIT-09)
		//=============================================================================================================

		namespace
		{
			// The scalar's textual form used when converting to a string (raw number token / "true" / "false"; null
			// and containers collapse to the empty string).

			QString textual_form ( const JsonNode& node )
			{
				switch ( node.kind () )
				{
					case JsonKind::Number:  return node.number_token ();
					case JsonKind::String:  return node.string_value ();
					case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
					default:                return QString ();
				}
			}
		}

		std::unique_ptr<JsonNode> convert_node ( const JsonNode& node, JsonKind newKind )
		{
			if ( node.kind () == newKind )
			{
				return node.clone ();
			}

			switch ( newKind )
			{
				case JsonKind::String:
				{
					return JsonNode::make_string ( textual_form ( node ) );
				}

				case JsonKind::Number:
				{
					// A string that is itself a valid JSON number converts verbatim; booleans map to 1/0; anything
					// else (null, containers, non-numeric strings) becomes 0.

					if ( ( node.kind () == JsonKind::String ) && is_json_number ( node.string_value () ) )
					{
						return JsonNode::make_number ( node.string_value () );
					}

					if ( node.kind () == JsonKind::Boolean )
					{
						return JsonNode::make_number ( node.boolean_value () ? QStringLiteral ( "1" ) : QStringLiteral ( "0" ) );
					}

					return JsonNode::make_number ( QStringLiteral ( "0" ) );
				}

				case JsonKind::Boolean:
				{
					// The exact strings "true"/"false" convert; a number is true when non-zero; anything else is false.

					if ( node.kind () == JsonKind::String )
					{
						return JsonNode::make_boolean ( node.string_value () == QStringLiteral ( "true" ) );
					}

					if ( node.kind () == JsonKind::Number )
					{
						return JsonNode::make_boolean ( node.number_token ().toDouble () != 0.0 );
					}

					return JsonNode::make_boolean ( false );
				}

				case JsonKind::Null:
				{
					return JsonNode::make_null ();
				}

				case JsonKind::Array:
				{
					return JsonNode::make_array ();      // Neutral []; undo recovers any dropped content.
				}

				case JsonKind::Object:
				{
					return JsonNode::make_object ();     // Neutral {}; undo recovers any dropped content.
				}
			}

			return node.clone ();
		}

		//=============================================================================================================
		// Array transforms (EDIT-11 / 12 / 13)
		//=============================================================================================================

		std::unique_ptr<JsonNode> normalize_array_elements ( const JsonNode& array )
		{
			// First pass: the union of member keys across all elements, in first-encountered order.

			QStringList unionKeys;

			for ( int elementIndex = 0; elementIndex < array.array_size (); ++elementIndex )
			{
				const JsonNode* element = array.array_element ( elementIndex );

				for ( int memberIndex = 0; memberIndex < element->member_count (); ++memberIndex )
				{
					const QString& key = element->member_key ( memberIndex );

					if ( !unionKeys.contains ( key ) )
					{
						unionKeys.append ( key );
					}
				}
			}

			// Second pass: rebuild each element with the full key set in union order, keeping existing values and
			// filling absent members with null.

			std::unique_ptr<JsonNode> result = JsonNode::make_array ();

			for ( int elementIndex = 0; elementIndex < array.array_size (); ++elementIndex )
			{
				const JsonNode*           element = array.array_element ( elementIndex );
				std::unique_ptr<JsonNode> rebuilt = JsonNode::make_object ();

				for ( const QString& key : unionKeys )
				{
					const JsonNode* existing = element->find_member ( key );

					if ( existing != nullptr )
					{
						rebuilt->append_member ( key, existing->clone () );
					}
					else
					{
						rebuilt->append_member ( key, JsonNode::make_null () );
					}
				}

				result->append_element ( std::move ( rebuilt ) );
			}

			return result;
		}

		std::unique_ptr<JsonNode> array_to_object ( const JsonNode& array )
		{
			std::unique_ptr<JsonNode> result = JsonNode::make_object ();

			for ( int index = 0; index < array.array_size (); ++index )
			{
				result->append_member ( QString::number ( index ), array.array_element ( index )->clone () );
			}

			return result;
		}

		std::unique_ptr<JsonNode> object_to_array ( const JsonNode& object )
		{
			std::unique_ptr<JsonNode> result = JsonNode::make_array ();

			for ( int index = 0; index < object.member_count (); ++index )
			{
				result->append_element ( object.member_value ( index )->clone () );
			}

			return result;
		}

		//=============================================================================================================
		// Duplicate-key naming (EDIT-07)
		//=============================================================================================================

		QString duplicate_key_name ( const JsonNode& object, const QString& baseKey )
		{
			const QString firstCandidate = baseKey + QStringLiteral ( " (copy)" );

			if ( !object.has_member ( firstCandidate ) )
			{
				return firstCandidate;
			}

			for ( int suffix = 2; ; ++suffix )
			{
				const QString candidate = baseKey + QStringLiteral ( " (copy " ) + QString::number ( suffix ) + QStringLiteral ( ")" );

				if ( !object.has_member ( candidate ) )
				{
					return candidate;
				}
			}
		}
	}
}
