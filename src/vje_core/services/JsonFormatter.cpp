//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonFormatter implementation. See JsonFormatter.hpp for the format-profile design notes (SET-07 / FILE-03).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/JsonSerializer.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <algorithm>

namespace vje
{
	//=================================================================================================================
	// Public Interface
	//=================================================================================================================

	QString JsonFormatter::format ( const JsonNode& node, const FormatProfile& profile )
	{
		QString out;
		write_value ( node, out, 0, profile );
		return out;
	}

	//=================================================================================================================
	// Internals
	//=================================================================================================================

	QString JsonFormatter::indent_text ( int level, const FormatProfile& profile )
	{
		if ( profile.indent == IndentKind::Tabs )
		{
			return QString ( level, QLatin1Char ( '\t' ) );
		}

		const int size = ( profile.indentSize >= 1 ) ? profile.indentSize : 1;

		return QString ( level * size, QLatin1Char ( ' ' ) );
	}

	bool JsonFormatter::is_non_empty_container ( const JsonNode& node )
	{
		if ( node.kind () == JsonKind::Array )
		{
			return node.array_size () > 0;
		}

		if ( node.kind () == JsonKind::Object )
		{
			return node.member_count () > 0;
		}

		return false;
	}

	void JsonFormatter::write_value ( const JsonNode& node, QString& out, int containerLevel, const FormatProfile& profile )
	{
		switch ( node.kind () )
		{
			case JsonKind::Null:
			{
				out += QLatin1String ( "null" );
				return;
			}

			case JsonKind::Boolean:
			{
				out += node.boolean_value () ? QLatin1String ( "true" ) : QLatin1String ( "false" );
				return;
			}

			case JsonKind::Number:
			{
				out += node.number_token ();               // Verbatim raw token (FILE-10).
				return;
			}

			case JsonKind::String:
			{
				out += JsonSerializer::encode_string ( node.string_value () );
				return;
			}

			case JsonKind::Array:
			{
				const int count = node.array_size ();

				if ( count == 0 )
				{
					out += QLatin1String ( "[]" );         // Empty container inline in both brace styles.
					return;
				}

				const QString childIndent = indent_text ( containerLevel + 1, profile );
				const QString closeIndent  = indent_text ( containerLevel, profile );

				out += QLatin1Char ( '[' );
				out += QLatin1Char ( '\n' );

				for ( int i = 0; i < count; ++i )
				{
					out += childIndent;
					write_value ( *node.array_element ( i ), out, containerLevel + 1, profile );

					if ( i < count - 1 )
					{
						out += QLatin1Char ( ',' );
					}

					out += QLatin1Char ( '\n' );
				}

				out += closeIndent;
				out += QLatin1Char ( ']' );
				return;
			}

			case JsonKind::Object:
			{
				const int count = node.member_count ();

				if ( count == 0 )
				{
					out += QLatin1String ( "{}" );         // Empty container inline in both brace styles.
					return;
				}

				const QString childIndent = indent_text ( containerLevel + 1, profile );
				const QString closeIndent  = indent_text ( containerLevel, profile );

				// Alignment pads each encoded key to the widest sibling key so the ':' separators line up (SET-07).

				int keyFieldWidth = 0;

				if ( profile.alignNameSeparators )
				{
					for ( int i = 0; i < count; ++i )
					{
						keyFieldWidth = std::max ( keyFieldWidth, static_cast<int> ( JsonSerializer::encode_string ( node.member_key ( i ) ).length () ) );
					}
				}

				out += QLatin1Char ( '{' );
				out += QLatin1Char ( '\n' );

				for ( int i = 0; i < count; ++i )
				{
					const JsonNode* value      = node.member_value ( i );
					const QString   encodedKey = JsonSerializer::encode_string ( node.member_key ( i ) );

					out += childIndent;
					out += encodedKey;

					if ( profile.alignNameSeparators )
					{
						out += QString ( keyFieldWidth - encodedKey.length (), QLatin1Char ( ' ' ) );
					}

					out += QLatin1Char ( ':' );

					// Allman drops a non-empty container's brace onto its own line at the member's indent; otherwise
					// the value continues on this line after a single space (K&R, all scalars, empty containers).

					if ( ( profile.braceStyle == BraceStyle::Allman ) && is_non_empty_container ( *value ) )
					{
						out += QLatin1Char ( '\n' );
						out += childIndent;
					}
					else
					{
						out += QLatin1Char ( ' ' );
					}

					write_value ( *value, out, containerLevel + 1, profile );

					if ( i < count - 1 )
					{
						out += QLatin1Char ( ',' );
					}

					out += QLatin1Char ( '\n' );
				}

				out += closeIndent;
				out += QLatin1Char ( '}' );
				return;
			}
		}
	}
}
