//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonPointer -- an RFC 6901 JSON Pointer value type used to name a node by path: for change signals, selection
//   restoration, undo targeting, and Go To (FIND-04). A pointer is a sequence of DECODED reference tokens; the
//   empty sequence is the root.
//
//   Text form: "/foo/0/a~1b/m~0n"; on decode "~1" -> "/" and "~0" -> "~" (in that order). Object tokens match
//   member keys verbatim (first match wins when duplicate keys exist); array tokens must be a canonical index
//   ("0" or a non-zero-leading run of digits).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>
#include <QStringList>

namespace vje
{
	class JsonNode;

	//*****************************************************************************************************************
	// Class: JsonPointer
	//*****************************************************************************************************************

	class JsonPointer
	{
		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QStringList referenceTokens;                           // Decoded tokens; empty => root.

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		JsonPointer () = default;                              // Root pointer.

		// Parse RFC 6901 text ("" or "/tok/tok..."). On malformed input returns the root and sets *ok = false.

		static JsonPointer parse ( const QString& text, bool* ok = nullptr );

		// Build directly from already-decoded tokens.

		static JsonPointer from_tokens ( const QStringList& decodedTokens );

		// The pointer that names a node within its tree, derived by walking parent links to the root (an array parent
		// contributes the child index; an object parent contributes the member key at that position). A node with no
		// parent is the root and yields the root pointer. Used for change signals and undo targeting.

		static JsonPointer from_node ( const JsonNode* node );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		bool           is_root     () const;
		int            token_count () const;
		const QString& token       ( int index ) const;

		QString to_string () const;                            // RFC 6901 text (encoded).

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		JsonPointer child ( const QString& decodedToken ) const;   // Append one token.
		JsonPointer parent () const;                               // Drop the last token (root stays root).

		// Resolve against a tree root; nullptr if any token fails to resolve. A root pointer resolves to root itself.

		JsonNode* resolve ( JsonNode* root ) const;

		bool operator== ( const JsonPointer& other ) const;
		bool operator!= ( const JsonPointer& other ) const;

		//=============================================================================================================
		// Static Helpers -- RFC 6901 token escaping.
		//=============================================================================================================

	public:

		static QString encode_token ( const QString& decodedToken );   // "a/b" -> "a~1b".
		static QString decode_token ( const QString& encodedToken );   // "a~1b" -> "a/b".
	};
}
