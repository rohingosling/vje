//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   SearchService -- whole-document find (FIND-01..03) and Go To (FIND-04), UI-free. A
//   straight pre-order walk matches the query against object member KEYS and the textual form of SCALAR values
//   (string content, raw number token, true / false / null), returning each matching node as a JSON Pointer in
//   DOCUMENT ORDER. Matching is case-insensitive by default; a Match Case option makes it case-sensitive (FIND-01).
//   Search never mutates the tree.
//
//   A node contributes AT MOST ONE match even when both its key and its value match (so the match count is a node
//   count). A key match lets containers be found by name; a value match covers the scalars.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonPointer.hpp>

#include <QString>

#include <vector>

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// A find request: the needle and whether the match is case-sensitive (FIND-01). An empty needle matches nothing.
	//-----------------------------------------------------------------------------------------------------------------

	struct SearchQuery
	{
		QString text;
		bool    matchCase = false;
	};

	//*****************************************************************************************************************
	// Class: SearchService
	//*****************************************************************************************************************

	class SearchService
	{
		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		// Every match in document order (FIND-01..03). Empty when the needle is empty or nothing matches.

		static std::vector<JsonPointer> find_all ( const JsonNode& root, const SearchQuery& query );

		// FIND-04: resolve pointer for Go To; nullptr when unresolvable (thin over JsonPointer::resolve).

		static JsonNode* go_to ( JsonNode* root, const JsonPointer& pointer );

		//=============================================================================================================
		// Internals
		//=============================================================================================================

	private:

		// Append matches under node (reached via pointer, whose owning object key is *memberKey, or nullptr for the
		// root / an array element) to out, pre-order.

		static void collect ( const JsonNode&           node,
		                      const JsonPointer&        pointer,
		                      const QString*            memberKey,
		                      const SearchQuery&        query,
		                      std::vector<JsonPointer>& out );

		static QString scalar_text ( const JsonNode& node );   // Textual form of a scalar for value matching.
	};
}
