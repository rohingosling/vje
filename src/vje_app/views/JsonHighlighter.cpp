//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonHighlighter implementation. See the header for why per-block lexing is exact for JSON and where the one piece
//   of lookahead stops.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/JsonHighlighter.hpp"

#include <vje_core/services/JsonLexer.hpp>

#include <QTextCharFormat>

#include <vector>

namespace vje
{
	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	JsonHighlighter::JsonHighlighter ( QTextDocument* document )
		: QSyntaxHighlighter ( document )
	{
	}

	//=================================================================================================================
	// Mutators
	//=================================================================================================================

	void JsonHighlighter::set_token_palette ( const CodeTokenPalette& newTokens )
	{
		tokens = newTokens;

		rehighlight ();
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	const CodeTokenPalette& JsonHighlighter::token_palette () const
	{
		return tokens;
	}

	//=================================================================================================================
	// QSyntaxHighlighter
	//=================================================================================================================

	void JsonHighlighter::highlightBlock ( const QString& text )
	{
		if ( text.isEmpty () )
		{
			return;
		}

		// The whole line's tokens are collected before any is coloured, because a string's colour depends on whether
		// the NEXT token is a ':' -- see the header. One pass could not answer that without pushing the previous token
		// forward through the loop, which is the same lookahead written less legibly.

		std::vector<JsonToken> lineTokens;

		JsonLexer lexer ( text );

		for ( ;; )
		{
			const JsonToken token = lexer.next ();

			if ( ( token.kind == JsonTokenKind::EndOfInput ) || ( token.kind == JsonTokenKind::Invalid ) )
			{
				// Invalid stops the line rather than being skipped: the lexer has no way to resynchronize, so anything
				// it reported after this point would be guesswork painted onto the user's screen.

				break;
			}

			lineTokens.push_back ( token );
		}

		for ( std::size_t index = 0; index < lineTokens.size (); ++index )
		{
			const JsonToken& token = lineTokens [ index ];

			QColor colour;

			switch ( token.kind )
			{
				case JsonTokenKind::String:
				{
					const bool followedByColon = ( index + 1 < lineTokens.size () )
					                          && ( lineTokens [ index + 1 ].kind == JsonTokenKind::NameSeparator );

					colour = followedByColon ? tokens.memberName : tokens.stringValue;

					break;
				}

				case JsonTokenKind::Number:
				{
					colour = tokens.number;

					break;
				}

				case JsonTokenKind::True:
				case JsonTokenKind::False:
				case JsonTokenKind::Null:
				{
					colour = tokens.literal;

					break;
				}

				case JsonTokenKind::BeginObject:
				case JsonTokenKind::EndObject:
				case JsonTokenKind::BeginArray:
				case JsonTokenKind::EndArray:
				case JsonTokenKind::NameSeparator:
				case JsonTokenKind::ValueSeparator:
				{
					colour = tokens.punctuation;

					break;
				}

				default:
				{
					continue;
				}
			}

			// An invalid colour is what a default-constructed CodeTokenPalette carries, which is what a highlighter
			// that has not been given a palette yet would otherwise paint the document with -- black on black under the
			// dark theme.

			if ( colour.isValid () )
			{
				QTextCharFormat format;

				format.setForeground ( colour );

				setFormat ( token.offset, token.length, format );
			}
		}
	}
}
