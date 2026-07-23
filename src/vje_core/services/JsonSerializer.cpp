//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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
		out += QLatin1Char ( '"' );

		for ( const QChar character : value )
		{
			switch ( character.unicode () )
			{
				case u'"':  out += QLatin1String ( "\\\"" ); break;
				case u'\\': out += QLatin1String ( "\\\\" ); break;
				case u'\b': out += QLatin1String ( "\\b"  ); break;
				case u'\f': out += QLatin1String ( "\\f"  ); break;
				case u'\n': out += QLatin1String ( "\\n"  ); break;
				case u'\r': out += QLatin1String ( "\\r"  ); break;
				case u'\t': out += QLatin1String ( "\\t"  ); break;

				default:
				{
					if ( character.unicode () < 0x20 )
					{
						// Other control characters as \u00XX.

						out += QString::asprintf ( "\\u%04x", character.unicode () );
					}
					else
					{
						out += character;
					}

					break;
				}
			}
		}

		out += QLatin1Char ( '"' );
	}
}
