//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   JsonPointer implementation (RFC 6901). See JsonPointer.hpp for the design notes.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/document/JsonPointer.hpp>

#include <algorithm>
#include <vje_core/document/JsonNode.hpp>

namespace vje
{
	//=================================================================================================================
	// Local Helpers
	//=================================================================================================================

	namespace
	{
		// True when the token is a canonical, non-negative array index: "0", or a digit run with no leading zero.

		bool is_canonical_index ( const QString& token, int& indexOut )
		{
			if ( token.isEmpty () )
			{
				return false;
			}

			if ( ( token.size () > 1 ) && ( token [ 0 ] == QLatin1Char ( '0' ) ) )
			{
				return false;
			}

			for ( const QChar character : token )
			{
				if ( ( character < QLatin1Char ( '0' ) ) || ( character > QLatin1Char ( '9' ) ) )
				{
					return false;
				}
			}

			bool converted = false;
			const int value = token.toInt ( &converted );

			if ( !converted )
			{
				return false;
			}

			indexOut = value;
			return true;
		}
	}

	//=================================================================================================================
	// Token Escaping (RFC 6901 section 3)
	//=================================================================================================================

	QString JsonPointer::encode_token ( const QString& decodedToken )
	{
		// Order matters: encode "~" first, then "/", so a literal "~1" is not mangled.

		QString encoded = decodedToken;
		encoded.replace ( QLatin1Char ( '~' ), QLatin1String ( "~0" ) );
		encoded.replace ( QLatin1Char ( '/' ), QLatin1String ( "~1" ) );

		return encoded;
	}

	QString JsonPointer::decode_token ( const QString& encodedToken )
	{
		// Inverse order: "~1" -> "/" first, then "~0" -> "~".

		QString decoded = encodedToken;
		decoded.replace ( QLatin1String ( "~1" ), QLatin1String ( "/" ) );
		decoded.replace ( QLatin1String ( "~0" ), QLatin1String ( "~" ) );

		return decoded;
	}

	//=================================================================================================================
	// Construction
	//=================================================================================================================

	JsonPointer JsonPointer::parse ( const QString& text, bool* ok )
	{
		JsonPointer pointer;

		// The empty string is the valid root pointer.

		if ( text.isEmpty () )
		{
			if ( ok != nullptr ) { *ok = true; }
			return pointer;
		}

		// A non-empty pointer must begin with '/'.

		if ( text [ 0 ] != QLatin1Char ( '/' ) )
		{
			if ( ok != nullptr ) { *ok = false; }
			return JsonPointer ();
		}

		// Split on '/' after the leading one. Qt's split keeps empty parts, which is exactly RFC 6901's
		// treatment of an empty reference token (a member whose key is "").

		const QStringList encodedTokens = text.mid ( 1 ).split ( QLatin1Char ( '/' ) );

		for ( const QString& encoded : encodedTokens )
		{
			pointer.referenceTokens.append ( decode_token ( encoded ) );
		}

		if ( ok != nullptr ) { *ok = true; }
		return pointer;
	}

	JsonPointer JsonPointer::from_tokens ( const QStringList& decodedTokens )
	{
		JsonPointer pointer;
		pointer.referenceTokens = decodedTokens;
		return pointer;
	}

	JsonPointer JsonPointer::from_node ( const JsonNode* node )
	{
		QStringList tokens;
		QList<int>  occurrences;

		// Walk up to the root, prepending each level's token. The order is built root-ward, so prepend keeps the
		// tokens in root-to-node order.

		for ( const JsonNode* current = node; ( current != nullptr ) && ( current->parent () != nullptr ); current = current->parent () )
		{
			JsonNode* parentNode = current->parent ();
			const int index      = current->index_in_parent ();

			if ( parentNode->kind () == JsonKind::Object )
			{
				tokens.prepend ( parentNode->member_key ( index ) );

				// The node is IN the tree here, so its own position is known exactly -- which is the whole reason
				// from_node can disambiguate where parse cannot.

				occurrences.prepend ( occurrence_for ( *parentNode, index ) );
			}
			else
			{
				tokens.prepend ( QString::number ( index ) );

				occurrences.prepend ( -1 );                // An array index is already unambiguous.
			}
		}

		JsonPointer result = from_tokens ( tokens );

		result.set_occurrences ( occurrences );

		return result;
	}

	//=================================================================================================================
	// Value Accessors
	//=================================================================================================================

	bool JsonPointer::is_root () const
	{
		return referenceTokens.isEmpty ();
	}

	int JsonPointer::token_count () const
	{
		return static_cast<int> ( referenceTokens.size () );
	}

	const QString& JsonPointer::token ( int index ) const
	{
		return referenceTokens [ index ];
	}

	QString JsonPointer::to_string () const
	{
		QString text;

		for ( const QString& decoded : referenceTokens )
		{
			text += QLatin1Char ( '/' );
			text += encode_token ( decoded );
		}

		return text;
	}

	int JsonPointer::occurrence ( int index ) const
	{
		const bool inRange = ( index >= 0 ) && ( index < memberOccurrences.size () );

		return inRange ? memberOccurrences.at ( index ) : -1;
	}

	bool JsonPointer::has_occurrences () const
	{
		return !memberOccurrences.isEmpty ();
	}

	int JsonPointer::occurrence_for ( const JsonNode& parent, int memberIndex )
	{
		const bool inRange = ( parent.kind () == JsonKind::Object )
		                  && ( memberIndex >= 0 )
		                  && ( memberIndex < parent.member_count () );

		if ( !inRange )
		{
			return -1;
		}

		const QString& key = parent.member_key ( memberIndex );

		// Duplicated among its siblings, or not. Only the first case earns a disambiguator -- see the header for why
		// recording one unconditionally would break pointer equality everywhere.

		for ( int index = 0; index < parent.member_count (); ++index )
		{
			if ( ( index != memberIndex ) && ( parent.member_key ( index ) == key ) )
			{
				return memberIndex;
			}
		}

		return -1;
	}

	JsonPointer JsonPointer::with_occurrence ( int tokenIndex, int memberIndex ) const
	{
		JsonPointer result = *this;

		const bool inRange = ( tokenIndex >= 0 ) && ( tokenIndex < result.referenceTokens.size () );

		if ( !inRange )
		{
			return result;
		}

		if ( result.memberOccurrences.isEmpty () )
		{
			result.memberOccurrences = QList<int> ( result.referenceTokens.size (), -1 );
		}

		result.memberOccurrences [ tokenIndex ] = memberIndex;

		result.normalize_occurrences ();

		return result;
	}

	void JsonPointer::set_occurrences ( const QList<int>& occurrences )
	{
		memberOccurrences = ( occurrences.size () == referenceTokens.size () ) ? occurrences : QList<int> ();

		normalize_occurrences ();
	}

	void JsonPointer::normalize_occurrences ()
	{
		// A list of nothing but -1 IS no list, and saying so here rather than at each caller is what makes equality
		// safe: every pointer that needs no disambiguator carries the same empty list whatever route built it.

		for ( const int position : memberOccurrences )
		{
			if ( position >= 0 )
			{
				return;
			}
		}

		memberOccurrences.clear ();
	}

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	JsonPointer JsonPointer::child ( const QString& decodedToken ) const
	{
		JsonPointer result = *this;

		result.referenceTokens.append ( decodedToken );

		// The new token carries no disambiguator, which is correct: a caller building a child by NAME is asking for
		// the first member with that name, exactly as the RFC says. Only the routes that know a position -- from_node
		// and the tree's shadow -- record one.

		if ( !result.memberOccurrences.isEmpty () )
		{
			result.memberOccurrences.append ( -1 );
		}

		return result;
	}

	JsonPointer JsonPointer::common_ancestor ( const JsonPointer& left, const JsonPointer& right )
	{
		QStringList shared;

		const int limit = std::min ( left.token_count (), right.token_count () );

		QList<int> sharedOccurrences;

		for ( int index = 0; index < limit; ++index )
		{
			// The OCCURRENCE is part of the comparison, because two tokens with the same key and different positions
			// name different nodes -- so the common ancestor has to stop above them rather than claim one of them
			// contains the other.

			if ( ( left.token ( index ) != right.token ( index ) )
			  || ( left.occurrence ( index ) != right.occurrence ( index ) ) )
			{
				break;
			}

			shared.append ( left.token ( index ) );
			sharedOccurrences.append ( left.occurrence ( index ) );
		}

		JsonPointer result = from_tokens ( shared );

		result.set_occurrences ( sharedOccurrences );

		return result;
	}

	JsonPointer JsonPointer::parent () const
	{
		JsonPointer result = *this;

		if ( !result.referenceTokens.isEmpty () )
		{
			result.referenceTokens.removeLast ();
		}

		if ( !result.memberOccurrences.isEmpty () )
		{
			result.memberOccurrences.removeLast ();

			result.normalize_occurrences ();
		}

		return result;
	}

	JsonNode* JsonPointer::resolve ( JsonNode* root ) const
	{
		JsonNode* current = root;

		for ( int tokenIndex = 0; tokenIndex < referenceTokens.size (); ++tokenIndex )
		{
			const QString& token = referenceTokens.at ( tokenIndex );

			if ( current == nullptr )
			{
				return nullptr;
			}

			switch ( current->kind () )
			{
				case JsonKind::Object:
				{
					// THE DISAMBIGUATOR, where the pointer carries one. The KEY is verified against the position
					// before it is trusted: an occurrence is a snapshot of where the member sat when the pointer was
					// built, and an edit since then can have moved it. Falling back to find_member where it no longer
					// matches degrades to the RFC's own answer rather than confidently resolving the wrong node --
					// which is the failure this whole mechanism exists to prevent, and it would be worse arriving
					// from the fix.

					const int position = occurrence ( tokenIndex );

					const bool positionHolds = ( position >= 0 )
					                        && ( position < current->member_count () )
					                        && ( current->member_key ( position ) == token );

					current = positionHolds ? current->member_value ( position ) : current->find_member ( token );

					break;
				}

				case JsonKind::Array:
				{
					int index = 0;

					if ( !is_canonical_index ( token, index ) )
					{
						return nullptr;
					}

					current = current->array_element ( index );
					break;
				}

				default:
				{
					// A scalar has no children to descend into.

					return nullptr;
				}
			}
		}

		return current;
	}

	bool JsonPointer::operator== ( const JsonPointer& other ) const
	{
		// The occurrences are part of identity: the two `name` members of one object are different nodes, and the
		// tree's selection, its expansion restore and every echo guard compare pointers.
		//
		// They are NORMALIZED to empty when no token needs one, which is what keeps a pointer parsed from text equal
		// to the pointer the tree built for the same unique key -- so Go To, Find and the restore-by-pointer are all
		// untouched by this. A list of nothing but -1 would compare unequal to an empty one and break every one of
		// them, which is why normalize_occurrences exists rather than being left to each caller.

		return ( referenceTokens == other.referenceTokens ) && ( memberOccurrences == other.memberOccurrences );
	}

	bool JsonPointer::operator!= ( const JsonPointer& other ) const
	{
		return !( *this == other );
	}
}
