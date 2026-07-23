//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonLexer -- the single JSON tokenizer in the codebase. It scans source text into
//   a stream of tokens, each carrying its exact source span (offset + length) and 1-based line/column, so callers
//   can report positions (FILE-06) and colour spans (the Code View highlighter, later phase). It is TOTAL: malformed
//   input yields an Invalid token rather than an exception. Insignificant JSON whitespace is skipped between tokens.
//
//   String and number tokens are validated as they are scanned -- a well-formed String / Number token is guaranteed
//   to be RFC 8259 valid, so the parser can decode a string and store a number token without re-checking. Malformed
//   strings/numbers surface as Invalid.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// Token kinds. PascalCase per the enum-class convention.
	//-----------------------------------------------------------------------------------------------------------------

	enum class JsonTokenKind
	{
		BeginObject,        // {
		EndObject,          // }
		BeginArray,         // [
		EndArray,           // ]
		NameSeparator,      // :
		ValueSeparator,     // ,
		String,             // "..."
		Number,             // -?int frac? exp?
		True,               // true
		False,              // false
		Null,               // null
		EndOfInput,         // no more tokens
		Invalid             // malformed input at this span
	};

	//-----------------------------------------------------------------------------------------------------------------
	// A single token: its kind, its exact source span, and where it starts (1-based line/column).
	//-----------------------------------------------------------------------------------------------------------------

	struct JsonToken
	{
		JsonTokenKind kind   = JsonTokenKind::Invalid;
		int           offset = 0;
		int           length = 0;
		int           line   = 1;
		int           column = 1;
	};

	//*****************************************************************************************************************
	// Class: JsonLexer
	//*****************************************************************************************************************

	class JsonLexer
	{
		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QString source;                                        // The text being scanned (owned copy).
		int     position;                                      // Current scan index into source.
		int     line;                                          // Current 1-based line.
		int     column;                                        // Current 1-based column.

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		explicit JsonLexer ( const QString& text );

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		JsonToken next ();                                     // Scan and consume the next token.

		QString text_of ( const JsonToken& token ) const;     // The token's exact source substring.

		//=============================================================================================================
		// Internal Scanners
		//=============================================================================================================

	private:

		void      skip_whitespace ();
		QChar     current_char    () const;
		void      advance         ();                          // Consume one character, tracking line/column.

		JsonToken scan_string  ( int startOffset, int startLine, int startColumn );
		JsonToken scan_number  ( int startOffset, int startLine, int startColumn );
		JsonToken scan_literal ( int startOffset, int startLine, int startColumn );

		JsonToken make_token ( JsonTokenKind kind, int startOffset, int startLine, int startColumn ) const;
	};
}
