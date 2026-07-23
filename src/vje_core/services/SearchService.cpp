//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   SearchService implementation. See SearchService.hpp for the FIND-01..04 walk rules.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/SearchService.hpp>
#include <vje_core/document/JsonNode.hpp>

namespace vje
{
	//=================================================================================================================
	// Public Interface
	//=================================================================================================================

	std::vector<JsonPointer> SearchService::find_all ( const JsonNode& root, const SearchQuery& query )
	{
		std::vector<JsonPointer> matches;

		if ( query.text.isEmpty () )
		{
			return matches;                                // An empty needle matches nothing (FIND-03 guard).
		}

		collect ( root, JsonPointer (), nullptr, query, matches );

		return matches;
	}

	JsonNode* SearchService::go_to ( JsonNode* root, const JsonPointer& pointer )
	{
		return pointer.resolve ( root );
	}

	//=================================================================================================================
	// Internals
	//=================================================================================================================

	QString SearchService::scalar_text ( const JsonNode& node )
	{
		switch ( node.kind () )
		{
			case JsonKind::Null:    return QStringLiteral ( "null" );
			case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
			case JsonKind::Number:  return node.number_token ();
			case JsonKind::String:  return node.string_value ();
			default:                return QString ();
		}
	}

	void SearchService::collect ( const JsonNode&           node,
	                              const JsonPointer&        pointer,
	                              const QString*            memberKey,
	                              const SearchQuery&        query,
	                              std::vector<JsonPointer>& out )
	{
		const Qt::CaseSensitivity sensitivity = query.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;

		// This node matches if its owning object key contains the needle, or -- for a scalar -- its textual form does.
		// One entry per node even when both match, so the count is a node count (FIND-02).

		const bool keyMatches   = ( memberKey != nullptr ) && memberKey->contains ( query.text, sensitivity );
		const bool valueMatches = node.is_scalar () && scalar_text ( node ).contains ( query.text, sensitivity );

		if ( keyMatches || valueMatches )
		{
			out.push_back ( pointer );
		}

		// Recurse in document order: array elements by index, then object members by position.

		if ( node.kind () == JsonKind::Array )
		{
			for ( int index = 0; index < node.array_size (); ++index )
			{
				collect ( *node.array_element ( index ), pointer.child ( QString::number ( index ) ), nullptr, query, out );
			}
		}
		else if ( node.kind () == JsonKind::Object )
		{
			for ( int index = 0; index < node.member_count (); ++index )
			{
				const QString& key = node.member_key ( index );

				collect ( *node.member_value ( index ), pointer.child ( key ), &key, query, out );
			}
		}
	}
}
