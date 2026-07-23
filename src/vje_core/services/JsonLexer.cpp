//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonLexer implementation. See JsonLexer.hpp for the design notes. RFC 8259 lexical grammar for strings and
//   numbers is enforced here so downstream code can trust a String / Number token.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/JsonLexer.hpp>

namespace vje
{
	//=================================================================================================================
	// Local Helpers
	//=================================================================================================================

	namespace
	{
		bool is_digit ( QChar character )
		{
			return ( character >= QLatin1Char ( '0' ) ) && ( character <= QLatin1Char ( '9' ) );
		}

		bool is_hex_digit ( QChar character )
		{
			return is_digit ( character )
			    || ( ( character >= QLatin1Char ( 'a' ) ) && ( character <= QLatin1Char ( 'f' ) ) )
			    || ( ( character >= QLatin1Char ( 'A' ) ) && ( character <= QLatin1Char ( 'F' ) ) );
		}

		bool is_json_whitespace ( QChar character )
		{
			return ( character == QLatin1Char ( ' '  ) )
			    || ( character == QLatin1Char ( '\t' ) )
			    || ( character == QLatin1Char ( '\n' ) )
			    || ( character == QLatin1Char ( '\r' ) );
		}
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonLexer::JsonLexer ( const QString& text )
		: source   ( text )
		, position ( 0 )
		, line     ( 1 )
		, column   ( 1 )
	{
	}

	//=================================================================================================================
	// Cursor
	//=================================================================================================================

	QChar JsonLexer::current_char () const
	{
		if ( position >= source.size () )
		{
			return QChar ( u'\0' );
		}

		return source [ position ];
	}

	void JsonLexer::advance ()
	{
		if ( position >= source.size () )
		{
			return;
		}

		const QChar consumed = source [ position ];
		++position;

		if ( consumed == QLatin1Char ( '\n' ) )
		{
			++line;
			column = 1;
		}
		else
		{
			++column;
		}
	}

	void JsonLexer::skip_whitespace ()
	{
		while ( ( position < source.size () ) && is_json_whitespace ( source [ position ] ) )
		{
			advance ();
		}
	}

	JsonToken JsonLexer::make_token ( JsonTokenKind kind, int startOffset, int startLine, int startColumn ) const
	{
		JsonToken token;
		token.kind   = kind;
		token.offset = startOffset;
		token.length = position - startOffset;
		token.line   = startLine;
		token.column = startColumn;

		return token;
	}

	QString JsonLexer::text_of ( const JsonToken& token ) const
	{
		return source.mid ( token.offset, token.length );
	}

	//=================================================================================================================
	// Token Dispatch
	//=================================================================================================================

	JsonToken JsonLexer::next ()
	{
		skip_whitespace ();

		const int startOffset = position;
		const int startLine   = line;
		const int startColumn = column;

		if ( position >= source.size () )
		{
			return make_token ( JsonTokenKind::EndOfInput, startOffset, startLine, startColumn );
		}

		const QChar character = current_char ();

		switch ( character.unicode () )
		{
			case u'{': advance (); return make_token ( JsonTokenKind::BeginObject,    startOffset, startLine, startColumn );
			case u'}': advance (); return make_token ( JsonTokenKind::EndObject,      startOffset, startLine, startColumn );
			case u'[': advance (); return make_token ( JsonTokenKind::BeginArray,     startOffset, startLine, startColumn );
			case u']': advance (); return make_token ( JsonTokenKind::EndArray,       startOffset, startLine, startColumn );
			case u':': advance (); return make_token ( JsonTokenKind::NameSeparator,  startOffset, startLine, startColumn );
			case u',': advance (); return make_token ( JsonTokenKind::ValueSeparator, startOffset, startLine, startColumn );

			case u'"':
			{
				return scan_string ( startOffset, startLine, startColumn );
			}

			default:
			{
				if ( ( character == QLatin1Char ( '-' ) ) || is_digit ( character ) )
				{
					return scan_number ( startOffset, startLine, startColumn );
				}

				return scan_literal ( startOffset, startLine, startColumn );
			}
		}
	}

	//=================================================================================================================
	// String Scanner (RFC 8259 section 7)
	//=================================================================================================================

	JsonToken JsonLexer::scan_string ( int startOffset, int startLine, int startColumn )
	{
		// Consume the opening quote.

		advance ();

		while ( position < source.size () )
		{
			const QChar character = current_char ();

			if ( character == QLatin1Char ( '"' ) )
			{
				advance ();
				return make_token ( JsonTokenKind::String, startOffset, startLine, startColumn );
			}

			if ( character == QLatin1Char ( '\\' ) )
			{
				advance ();

				const QChar escape = current_char ();

				switch ( escape.unicode () )
				{
					case u'"':
					case u'\\':
					case u'/':
					case u'b':
					case u'f':
					case u'n':
					case u'r':
					case u't':
					{
						advance ();
						break;
					}

					case u'u':
					{
						advance ();

						for ( int i = 0; i < 4; ++i )
						{
							if ( !is_hex_digit ( current_char () ) )
							{
								return make_token ( JsonTokenKind::Invalid, startOffset, startLine, startColumn );
							}

							advance ();
						}

						break;
					}

					default:
					{
						// Unknown escape, or end of input after the backslash.

						return make_token ( JsonTokenKind::Invalid, startOffset, startLine, startColumn );
					}
				}

				continue;
			}

			// Unescaped control characters (< 0x20) are not allowed inside a JSON string.

			if ( character.unicode () < 0x20 )
			{
				return make_token ( JsonTokenKind::Invalid, startOffset, startLine, startColumn );
			}

			advance ();
		}

		// Reached end of input with no closing quote.

		return make_token ( JsonTokenKind::Invalid, startOffset, startLine, startColumn );
	}

	//=================================================================================================================
	// Number Scanner (RFC 8259 section 6): -? ( 0 | [1-9][0-9]* ) ( . [0-9]+ )? ( [eE] [+-]? [0-9]+ )?
	//=================================================================================================================

	JsonToken JsonLexer::scan_number ( int startOffset, int startLine, int startColumn )
	{
		// Optional minus.

		if ( current_char () == QLatin1Char ( '-' ) )
		{
			advance ();
		}

		// Integer part: a single 0, or a non-zero digit followed by more digits.

		if ( current_char () == QLatin1Char ( '0' ) )
		{
			advance ();
		}
		else if ( is_digit ( current_char () ) )
		{
			while ( is_digit ( current_char () ) )
			{
				advance ();
			}
		}
		else
		{
			return make_token ( JsonTokenKind::Invalid, startOffset, startLine, startColumn );
		}

		// Optional fractional part: '.' then one or more digits.

		if ( current_char () == QLatin1Char ( '.' ) )
		{
			advance ();

			if ( !is_digit ( current_char () ) )
			{
				return make_token ( JsonTokenKind::Invalid, startOffset, startLine, startColumn );
			}

			while ( is_digit ( current_char () ) )
			{
				advance ();
			}
		}

		// Optional exponent: [eE] [+-]? one or more digits.

		if ( ( current_char () == QLatin1Char ( 'e' ) ) || ( current_char () == QLatin1Char ( 'E' ) ) )
		{
			advance ();

			if ( ( current_char () == QLatin1Char ( '+' ) ) || ( current_char () == QLatin1Char ( '-' ) ) )
			{
				advance ();
			}

			if ( !is_digit ( current_char () ) )
			{
				return make_token ( JsonTokenKind::Invalid, startOffset, startLine, startColumn );
			}

			while ( is_digit ( current_char () ) )
			{
				advance ();
			}
		}

		return make_token ( JsonTokenKind::Number, startOffset, startLine, startColumn );
	}

	//=================================================================================================================
	// Literal Scanner: true / false / null.
	//=================================================================================================================

	JsonToken JsonLexer::scan_literal ( int startOffset, int startLine, int startColumn )
	{
		struct Keyword
		{
			QLatin1String text;
			JsonTokenKind kind;
		};

		const Keyword keywords [] =
		{
			{ QLatin1String ( "true"  ), JsonTokenKind::True  },
			{ QLatin1String ( "false" ), JsonTokenKind::False },
			{ QLatin1String ( "null"  ), JsonTokenKind::Null  },
		};

		for ( const Keyword& keyword : keywords )
		{
			const int wordLength = keyword.text.size ();

			if ( ( position + wordLength ) <= source.size ()
			  && ( source.mid ( position, wordLength ) == keyword.text ) )
			{
				for ( int i = 0; i < wordLength; ++i )
				{
					advance ();
				}

				return make_token ( keyword.kind, startOffset, startLine, startColumn );
			}
		}

		// Not a recognized token: consume one character so the caller always makes progress.

		advance ();
		return make_token ( JsonTokenKind::Invalid, startOffset, startLine, startColumn );
	}
}
