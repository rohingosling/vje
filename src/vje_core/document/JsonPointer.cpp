//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

		// Walk up to the root, prepending each level's token. The order is built root-ward, so prepend keeps the
		// tokens in root-to-node order.

		for ( const JsonNode* current = node; ( current != nullptr ) && ( current->parent () != nullptr ); current = current->parent () )
		{
			JsonNode* parentNode = current->parent ();
			const int index      = current->index_in_parent ();

			if ( parentNode->kind () == JsonKind::Object )
			{
				tokens.prepend ( parentNode->member_key ( index ) );
			}
			else
			{
				tokens.prepend ( QString::number ( index ) );
			}
		}

		return from_tokens ( tokens );
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

	//=================================================================================================================
	// Methods
	//=================================================================================================================

	JsonPointer JsonPointer::child ( const QString& decodedToken ) const
	{
		JsonPointer result = *this;
		result.referenceTokens.append ( decodedToken );
		return result;
	}

	JsonPointer JsonPointer::parent () const
	{
		JsonPointer result = *this;

		if ( !result.referenceTokens.isEmpty () )
		{
			result.referenceTokens.removeLast ();
		}

		return result;
	}

	JsonNode* JsonPointer::resolve ( JsonNode* root ) const
	{
		JsonNode* current = root;

		for ( const QString& token : referenceTokens )
		{
			if ( current == nullptr )
			{
				return nullptr;
			}

			switch ( current->kind () )
			{
				case JsonKind::Object:
				{
					current = current->find_member ( token );
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
		return referenceTokens == other.referenceTokens;
	}

	bool JsonPointer::operator!= ( const JsonPointer& other ) const
	{
		return referenceTokens != other.referenceTokens;
	}
}
