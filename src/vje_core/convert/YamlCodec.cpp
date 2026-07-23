//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   YamlCodec implementation over yaml-cpp. Export drives a YAML::Emitter recursively; import walks a parsed
//   YAML::Node and reconstructs the JsonNode tree, using the scalar's tag to tell a quoted string from a plain
//   (inferable) scalar. yaml-cpp throws on malformed input, so from_yaml() wraps the parse in a try/catch and returns
//   the message rather than propagating (errors are values on our boundary).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/convert/YamlCodec.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/edit_transforms.hpp>

#include <yaml-cpp/yaml.h>

#include <string>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// Export: emit a node and, for containers, its descendants. Strings are forced double-quoted so their type is
		// unambiguous on re-import; numbers/booleans/null emit plain so they re-infer to their JSON type.
		//-------------------------------------------------------------------------------------------------------------

		void emit_node ( YAML::Emitter& out, const JsonNode& node )
		{
			switch ( node.kind () )
			{
				case JsonKind::Null:
				{
					out << YAML::Null;
					break;
				}

				case JsonKind::Boolean:
				{
					out << node.boolean_value ();                 // Emits plain true / false.
					break;
				}

				case JsonKind::Number:
				{
					// The raw token as a plain (unquoted) scalar -- yaml-cpp keeps a numeric-looking string plain, so
					// it re-infers to a number on import while the exact token (e.g. 1.50) is preserved verbatim.

					out << node.number_token ().toStdString ();
					break;
				}

				case JsonKind::String:
				{
					out << YAML::DoubleQuoted << node.string_value ().toStdString ();
					break;
				}

				case JsonKind::Array:
				{
					out << YAML::BeginSeq;

					for ( int elementIndex = 0; elementIndex < node.array_size (); ++elementIndex )
					{
						emit_node ( out, *node.array_element ( elementIndex ) );
					}

					out << YAML::EndSeq;
					break;
				}

				case JsonKind::Object:
				{
					out << YAML::BeginMap;

					for ( int memberIndex = 0; memberIndex < node.member_count (); ++memberIndex )
					{
						out << YAML::Key   << node.member_key ( memberIndex ).toStdString ();
						out << YAML::Value;
						emit_node ( out, *node.member_value ( memberIndex ) );
					}

					out << YAML::EndMap;
					break;
				}
			}
		}

		//-------------------------------------------------------------------------------------------------------------
		// Import: a plain scalar is inferred to its JSON type; a quoted scalar (non-specific "!" tag) stays a string.
		//-------------------------------------------------------------------------------------------------------------

		std::unique_ptr<JsonNode> build_scalar ( const YAML::Node& node )
		{
			const QString text = QString::fromStdString ( node.Scalar () );

			// yaml-cpp assigns the non-specific "!" tag to a quoted scalar -- an explicit string, never inferred.

			if ( node.Tag () == "!" )
			{
				return JsonNode::make_string ( text );
			}

			if ( text == QStringLiteral ( "null" ) || text == QStringLiteral ( "~" ) || text.isEmpty () )
			{
				return JsonNode::make_null ();
			}

			if ( text == QStringLiteral ( "true" ) )  return JsonNode::make_boolean ( true );
			if ( text == QStringLiteral ( "false" ) ) return JsonNode::make_boolean ( false );

			if ( edit_transforms::is_json_number ( text ) )
			{
				return JsonNode::make_number ( text );
			}

			return JsonNode::make_string ( text );
		}

		std::unique_ptr<JsonNode> build_node ( const YAML::Node& node )
		{
			if ( !node.IsDefined () || node.IsNull () )
			{
				return JsonNode::make_null ();
			}

			if ( node.IsScalar () )
			{
				return build_scalar ( node );
			}

			if ( node.IsSequence () )
			{
				std::unique_ptr<JsonNode> array = JsonNode::make_array ();

				for ( const YAML::Node& element : node )
				{
					array->append_element ( build_node ( element ) );
				}

				return array;
			}

			if ( node.IsMap () )
			{
				std::unique_ptr<JsonNode> object = JsonNode::make_object ();

				for ( YAML::const_iterator entry = node.begin (); entry != node.end (); ++entry )
				{
					object->append_member ( QString::fromStdString ( entry->first.Scalar () ), build_node ( entry->second ) );
				}

				return object;
			}

			return JsonNode::make_null ();
		}
	}

	//-----------------------------------------------------------------------------------------------------------------
	// to_yaml -- run the emitter over the document.
	//-----------------------------------------------------------------------------------------------------------------

	QString YamlCodec::to_yaml ( const JsonNode& root )
	{
		YAML::Emitter out;

		emit_node ( out, root );

		return QString::fromUtf8 ( out.c_str () );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// from_yaml -- parse, then rebuild; yaml-cpp exceptions become an error value.
	//-----------------------------------------------------------------------------------------------------------------

	YamlCodec::ImportResult YamlCodec::from_yaml ( const QString& text )
	{
		ImportResult result;

		try
		{
			const YAML::Node document = YAML::Load ( text.toStdString () );

			result.root = build_node ( document );
			result.ok   = true;
		}
		catch ( const YAML::Exception& exception )
		{
			result.error = QString::fromStdString ( exception.what () );
		}

		return result;
	}
}
