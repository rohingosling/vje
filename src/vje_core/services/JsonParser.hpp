//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonParser -- a small recursive-descent parser over JsonLexer that builds a JsonNode tree while preserving
//   member order (FILE-04) and raw number tokens (FILE-10). Errors are returned, never thrown: a ParseResult carries either a root (ok) or a ParseError with
//   line/column/offset (FILE-06).
//
//   Duplicate object keys do NOT fail the parse -- RFC 8259 permits them and a file may be loaded with them
//   (VAL-02). They are accepted into the order-preserving model and reported in ParseResult::duplicateKeys, so the
//   application layer can honour the "On duplicate keys when loading" setting (SET-03).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/services/JsonLexer.hpp>

#include <QString>

#include <memory>
#include <vector>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// A parse failure: a human-readable reason and where it occurred (1-based line/column; 0-based offset).
	//-----------------------------------------------------------------------------------------------------------------

	struct ParseError
	{
		QString message;
		int     line   = 0;
		int     column = 0;
		int     offset = 0;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// A duplicate object key found during a successful parse: the pointer to the containing object, the repeated
	// key, and the position of the offending occurrence.
	//-----------------------------------------------------------------------------------------------------------------

	struct DuplicateKey
	{
		JsonPointer objectPointer;
		QString     key;
		int         line   = 0;
		int         column = 0;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The outcome of a parse: on success, root is non-null and duplicateKeys lists any duplicates; on failure, ok is
	// false, root is null, and error describes the first failure.
	//-----------------------------------------------------------------------------------------------------------------

	struct ParseResult
	{
		std::unique_ptr<JsonNode> root;
		bool                      ok = false;
		ParseError                error;
		std::vector<DuplicateKey> duplicateKeys;
	};

	//*****************************************************************************************************************
	// Class: JsonParser
	//*****************************************************************************************************************

	class JsonParser
	{
		//=============================================================================================================
		// Constants
		//=============================================================================================================

	public:

		static constexpr int MAX_DEPTH = 512;                  // Guards against stack overflow on pathological nesting.

		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		static ParseResult parse ( const QString& text );

		// Decode a JSON string LITERAL (surrounding quotes included) to its value: "a~1b" escapes resolved, \uXXXX pairs
		// combined. Public because it is the codebase's single definition of that decoding, for the same reason
		// JsonLexer is its single tokenizer -- the Code View's pointer index (vje_app) has to turn a member's key token
		// into the decoded token a JsonPointer is built from, and a second copy of the escape table is a second copy
		// that can disagree.
		//
		// Precondition: the text is a WELL-FORMED string token, which is what JsonLexer guarantees for a String token.

		static QString decode_string ( const QString& rawTokenText );

		//=============================================================================================================
		// Internal State
		//=============================================================================================================

	private:

		explicit JsonParser ( const QString& text );

		ParseResult run ();

		std::unique_ptr<JsonNode> parse_value  ( int depth );
		std::unique_ptr<JsonNode> parse_object ( int depth );
		std::unique_ptr<JsonNode> parse_array  ( int depth );

		void advance   ();
		void set_error ( const QString& message, const JsonToken& at );

		JsonLexer                 lexer;
		JsonToken                 currentToken;
		QStringList               path;                        // Decoded tokens to the current position.
		std::vector<DuplicateKey> duplicateKeys;
		bool                      hasError;
		ParseError                error;
	};
}
