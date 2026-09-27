//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

		// THE DISAMBIGUATOR, and it is deliberately NOT part of the RFC 6901 text.
		//
		// An object may carry the same key twice (RFC 8259 permits it, FILE-04 preserves it), and a JSON Pointer names
		// only the FIRST such member -- so `/testObject/name` cannot say WHICH `name`, and every command routed by
		// pointer acted on the first one whatever the user had selected. This records the member INDEX for a token
		// whose key is duplicated, so a pointer built by a route that KNOWS which member it means -- from_node, and
		// the tree's own shadow -- resolves to that member and no other.
		//
		// Empty, or exactly one entry per token: the member index where the token's key is duplicated among its
		// siblings, and -1 everywhere else. It stays EMPTY unless at least one token needs it, which is what keeps a
		// pointer parsed from text equal to the pointer the tree built for the same unique key -- Go To, Find and the
		// expansion restore all compare pointers, and every one of them would break if this were set unconditionally.
		//
		// to_string() ignores it entirely, so Copy JSON Pointer still produces text Go To accepts, and the ambiguity
		// that text carries is RFC 6901's rather than something this class invented.

		QList<int> memberOccurrences;

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

		// Which member index a token should record, given the parent object and the member's position in it: the
		// position itself where that key is duplicated among its siblings, and -1 where it is unique.
		//
		// One rule, one statement -- from_node and JsonTreeModel::pointer_for_index both ask it, and a pointer that
		// recorded an occurrence for a unique key would compare unequal to the same pointer parsed from text.

		static int occurrence_for ( const JsonNode& parent, int memberIndex );

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		bool           is_root     () const;
		int            token_count () const;
		const QString& token       ( int index ) const;

		QString to_string () const;                            // RFC 6901 text (encoded).

		// The member index the token at `index` names, or -1 where the token carries no disambiguator -- which is
		// every token of a pointer parsed from text, and every token naming a key that is unique among its siblings.

		int  occurrence      ( int index ) const;
		bool has_occurrences () const;

		// A copy with one token's occurrence recorded. For the routes that know which member they mean.

		JsonPointer with_occurrence ( int tokenIndex, int memberIndex ) const;

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		JsonPointer child ( const QString& decodedToken ) const;   // Append one token.

		// The deepest pointer that is an ancestor of, or equal to, both. Used to collapse a batch of changes into ONE
		// notification naming the subtree that contains all of them (JsonDocument's change batch).

		static JsonPointer common_ancestor ( const JsonPointer& left, const JsonPointer& right );
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

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void set_occurrences       ( const QList<int>& occurrences );  // Ignored unless one entry per token.
		void normalize_occurrences ();                                 // All -1 becomes empty -- see operator==.
	};
}
