//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   node_presentation implementation. See the header for why the rule is shared rather than owned by the Form View.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "views/node_presentation.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>

namespace vje
{
	NodePresentation resolve_presentation ( const JsonDocument* document, const JsonPointer& pointer )
	{
		NodePresentation result;

		if ( ( document == nullptr ) || !document->has_root () )
		{
			return result;
		}

		const JsonNode* const node = document->resolve ( pointer );

		if ( node == nullptr )
		{
			return result;
		}

		// -- A container presents itself.

		if ( node->kind () == JsonKind::Array )
		{
			result.mode      = NodePresentation::Mode::ArrayTable;
			result.container = pointer;

			return result;
		}

		if ( node->kind () == JsonKind::Object )
		{
			result.mode      = NodePresentation::Mode::ObjectForm;
			result.container = pointer;

			return result;
		}

		// -- A scalar presents its PARENT with itself indicated (EDITOR-02). The scalar document ROOT is the single
		//    exception: it has no parent, so it renders as a lone single-row form.

		result.focus    = pointer;
		result.hasFocus = true;

		if ( pointer.is_root () )
		{
			result.mode      = NodePresentation::Mode::ObjectForm;
			result.container = pointer;

			return result;
		}

		const JsonPointer     parentPointer = pointer.parent ();
		const JsonNode* const parentNode    = document->resolve ( parentPointer );

		if ( parentNode == nullptr )
		{
			return NodePresentation ();
		}

		result.mode      = ( parentNode->kind () == JsonKind::Array ) ? NodePresentation::Mode::ArrayTable
		                                                             : NodePresentation::Mode::ObjectForm;
		result.container = parentPointer;

		return result;
	}
}
