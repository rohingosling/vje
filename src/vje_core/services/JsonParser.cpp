//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonParser implementation. See JsonParser.hpp for the design notes. String tokens arrive lexically validated,
//   so decode_string trusts their structure; numbers are stored as their raw token verbatim (FILE-10).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonParser.hpp>

namespace vje
{
	//=================================================================================================================
	// Public Interface
	//=================================================================================================================

	ParseResult JsonParser::parse ( const QString& text )
	{
		JsonParser parser ( text );
		return parser.run ();
	}

	//=================================================================================================================
	// Construction
	//=================================================================================================================

	JsonParser::JsonParser ( const QString& text )
		: lexer    ( text )
		, hasError ( false )
	{
	}

	//=================================================================================================================
	// Driver
	//=================================================================================================================

	ParseResult JsonParser::run ()
	{
		advance ();

		std::unique_ptr<JsonNode> root = parse_value ( 0 );

		ParseResult result;

		if ( hasError )
		{
			result.ok    = false;
			result.error = error;
			return result;
		}

		// A well-formed document is exactly one value; anything after it is an error.

		if ( currentToken.kind != JsonTokenKind::EndOfInput )
		{
			set_error ( QStringLiteral ( "unexpected trailing content after the JSON value" ), currentToken );

			result.ok    = false;
			result.error = error;
			return result;
		}

		result.ok            = true;
		result.root          = std::move ( root );
		result.duplicateKeys = std::move ( duplicateKeys );

		return result;
	}

	//=================================================================================================================
	// Grammar
	//=================================================================================================================

	std::unique_ptr<JsonNode> JsonParser::parse_value ( int depth )
	{
		if ( hasError )
		{
			return nullptr;
		}

		if ( depth > MAX_DEPTH )
		{
			set_error ( QStringLiteral ( "maximum nesting depth exceeded" ), currentToken );
			return nullptr;
		}

		switch ( currentToken.kind )
		{
			case JsonTokenKind::BeginObject:
			{
				return parse_object ( depth );
			}

			case JsonTokenKind::BeginArray:
			{
				return parse_array ( depth );
			}

			case JsonTokenKind::String:
			{
				std::unique_ptr<JsonNode> node = JsonNode::make_string ( decode_string ( lexer.text_of ( currentToken ) ) );
				advance ();
				return node;
			}

			case JsonTokenKind::Number:
			{
				std::unique_ptr<JsonNode> node = JsonNode::make_number ( lexer.text_of ( currentToken ) );
				advance ();
				return node;
			}

			case JsonTokenKind::True:
			{
				advance ();
				return JsonNode::make_boolean ( true );
			}

			case JsonTokenKind::False:
			{
				advance ();
				return JsonNode::make_boolean ( false );
			}

			case JsonTokenKind::Null:
			{
				advance ();
				return JsonNode::make_null ();
			}

			case JsonTokenKind::EndOfInput:
			{
				set_error ( QStringLiteral ( "unexpected end of input; a JSON value was expected" ), currentToken );
				return nullptr;
			}

			case JsonTokenKind::Invalid:
			{
				set_error ( QStringLiteral ( "invalid token" ), currentToken );
				return nullptr;
			}

			default:
			{
				set_error ( QStringLiteral ( "unexpected token; a JSON value was expected" ), currentToken );
				return nullptr;
			}
		}
	}

	std::unique_ptr<JsonNode> JsonParser::parse_object ( int depth )
	{
		// Consume '{'.

		advance ();

		std::unique_ptr<JsonNode> node = JsonNode::make_object ();

		// Empty object.

		if ( currentToken.kind == JsonTokenKind::EndObject )
		{
			advance ();
			return node;
		}

		for ( ; ; )
		{
			// Member key.

			if ( currentToken.kind != JsonTokenKind::String )
			{
				set_error ( QStringLiteral ( "expected a string key" ), currentToken );
				return nullptr;
			}

			const JsonToken keyToken = currentToken;
			const QString   key      = decode_string ( lexer.text_of ( keyToken ) );
			advance ();

			// Name separator.

			if ( currentToken.kind != JsonTokenKind::NameSeparator )
			{
				set_error ( QStringLiteral ( "expected ':' after the object key" ), currentToken );
				return nullptr;
			}

			advance ();

			// Duplicate keys are permitted on load but recorded (VAL-02 / SET-03). Detect before appending, so the
			// recorded pointer names the object (the path so far), not the new member.

			if ( node->has_member ( key ) )
			{
				DuplicateKey duplicate;
				duplicate.objectPointer = JsonPointer::from_tokens ( path );
				duplicate.key           = key;
				duplicate.line          = keyToken.line;
				duplicate.column        = keyToken.column;

				duplicateKeys.push_back ( duplicate );
			}

			// Member value.

			path.append ( key );
			std::unique_ptr<JsonNode> value = parse_value ( depth + 1 );
			path.removeLast ();

			if ( hasError )
			{
				return nullptr;
			}

			node->append_member ( key, std::move ( value ) );

			// Separator or close.

			if ( currentToken.kind == JsonTokenKind::ValueSeparator )
			{
				advance ();
				continue;
			}

			if ( currentToken.kind == JsonTokenKind::EndObject )
			{
				advance ();
				return node;
			}

			set_error ( QStringLiteral ( "expected ',' or '}' in object" ), currentToken );
			return nullptr;
		}
	}

	std::unique_ptr<JsonNode> JsonParser::parse_array ( int depth )
	{
		// Consume '['.

		advance ();

		std::unique_ptr<JsonNode> node = JsonNode::make_array ();

		// Empty array.

		if ( currentToken.kind == JsonTokenKind::EndArray )
		{
			advance ();
			return node;
		}

		int index = 0;

		for ( ; ; )
		{
			path.append ( QString::number ( index ) );
			std::unique_ptr<JsonNode> element = parse_value ( depth + 1 );
			path.removeLast ();

			if ( hasError )
			{
				return nullptr;
			}

			node->append_element ( std::move ( element ) );
			++index;

			if ( currentToken.kind == JsonTokenKind::ValueSeparator )
			{
				advance ();
				continue;
			}

			if ( currentToken.kind == JsonTokenKind::EndArray )
			{
				advance ();
				return node;
			}

			set_error ( QStringLiteral ( "expected ',' or ']' in array" ), currentToken );
			return nullptr;
		}
	}

	//=================================================================================================================
	// Support
	//=================================================================================================================

	void JsonParser::advance ()
	{
		currentToken = lexer.next ();
	}

	void JsonParser::set_error ( const QString& message, const JsonToken& at )
	{
		// Keep the first error; later cascade failures are less informative.

		if ( hasError )
		{
			return;
		}

		hasError      = true;
		error.message = message;
		error.line    = at.line;
		error.column  = at.column;
		error.offset  = at.offset;
	}

	QString JsonParser::decode_string ( const QString& rawTokenText )
	{
		// rawTokenText includes the surrounding quotes; the lexer guarantees it is well-formed.

		QString decoded;
		decoded.reserve ( rawTokenText.size () );

		const int end = rawTokenText.size () - 1;   // Exclude the closing quote.
		int       i   = 1;                          // Skip the opening quote.

		while ( i < end )
		{
			const QChar character = rawTokenText [ i ];

			if ( character != QLatin1Char ( '\\' ) )
			{
				decoded.append ( character );
				++i;
				continue;
			}

			// Escape sequence.

			const QChar escape = rawTokenText [ i + 1 ];

			switch ( escape.unicode () )
			{
				case u'"':  decoded.append ( QLatin1Char ( '"'  ) ); i += 2; break;
				case u'\\': decoded.append ( QLatin1Char ( '\\' ) ); i += 2; break;
				case u'/':  decoded.append ( QLatin1Char ( '/'  ) ); i += 2; break;
				case u'b':  decoded.append ( QChar ( u'\b' ) );      i += 2; break;
				case u'f':  decoded.append ( QChar ( u'\f' ) );      i += 2; break;
				case u'n':  decoded.append ( QChar ( u'\n' ) );      i += 2; break;
				case u'r':  decoded.append ( QChar ( u'\r' ) );      i += 2; break;
				case u't':  decoded.append ( QChar ( u'\t' ) );      i += 2; break;

				case u'u':
				{
					// \uXXXX -- four hex digits. Surrogate pairs fall out naturally: each half becomes one UTF-16
					// code unit, and two consecutive halves form the intended code point in the QString.

					const ushort code = rawTokenText.mid ( i + 2, 4 ).toUShort ( nullptr, 16 );
					decoded.append ( QChar ( code ) );
					i += 6;
					break;
				}

				default:
				{
					// Unreachable for a lexically valid token; copy the escape char defensively.

					decoded.append ( escape );
					i += 2;
					break;
				}
			}
		}

		return decoded;
	}
}
