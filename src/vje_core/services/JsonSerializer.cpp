//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonSerializer implementation. See JsonSerializer.hpp for the design notes.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonSerializer.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/json_escapes.hpp>

namespace vje
{
	//=================================================================================================================
	// Public Interface
	//=================================================================================================================

	QString JsonSerializer::serialize ( const JsonNode& node )
	{
		QString out;
		write_value ( node, out );
		return out;
	}

	QString JsonSerializer::encode_string ( const QString& value )
	{
		QString out;
		write_string ( value, out );
		return out;
	}

	//=================================================================================================================
	// Writers
	//=================================================================================================================

	void JsonSerializer::write_value ( const JsonNode& node, QString& out )
	{
		switch ( node.kind () )
		{
			case JsonKind::Null:
			{
				out += QLatin1String ( "null" );
				break;
			}

			case JsonKind::Boolean:
			{
				out += node.boolean_value () ? QLatin1String ( "true" ) : QLatin1String ( "false" );
				break;
			}

			case JsonKind::Number:
			{
				// Verbatim raw token -- exact textual representation is preserved (FILE-10).

				out += node.number_token ();
				break;
			}

			case JsonKind::String:
			{
				write_string ( node.string_value (), out );
				break;
			}

			case JsonKind::Array:
			{
				out += QLatin1Char ( '[' );

				const int count = node.array_size ();

				for ( int i = 0; i < count; ++i )
				{
					if ( i > 0 )
					{
						out += QLatin1Char ( ',' );
					}

					write_value ( *node.array_element ( i ), out );
				}

				out += QLatin1Char ( ']' );
				break;
			}

			case JsonKind::Object:
			{
				out += QLatin1Char ( '{' );

				const int count = node.member_count ();

				for ( int i = 0; i < count; ++i )
				{
					if ( i > 0 )
					{
						out += QLatin1Char ( ',' );
					}

					write_string ( node.member_key ( i ), out );
					out += QLatin1Char ( ':' );
					write_value ( *node.member_value ( i ), out );
				}

				out += QLatin1Char ( '}' );
				break;
			}
		}
	}

	void JsonSerializer::write_string ( const QString& value, QString& out )
	{
		// The escape table is json_escapes' (architecture.md section 4.6). It was moved out of this function when the
		// Form and Text views needed to show a value in escaped notation: a second copy is a second copy that can
		// disagree, and the one that would diverge silently is the one the user reads rather than the one that saves.

		out += QLatin1Char ( '"' );
		out += json_escapes::escape ( value );
		out += QLatin1Char ( '"' );
	}

}
