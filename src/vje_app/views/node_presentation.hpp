//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   node_presentation -- "which node does a selection actually PRESENT, and how?" (EDITOR-02 / EDITOR-03), as pure
//   logic over the document.
//
//   The rule is short and entirely non-obvious: a container presents itself, and a SCALAR presents its PARENT with
//   itself indicated -- because a scalar has no members to lay out, and what the user needs to see is the row it sits
//   in. A scalar document ROOT is the one exception, having no parent to fall back to.
//
//   WHY IT IS ITS OWN FILE. It was the Form View's alone in Phase 7. Phase 8's Text View is defined by EDITOR-06 as a
//   plain-text rendering of "the Form View's PRESENTATION of the selected node" -- the same rule, deliberately, so the
//   two tabs never disagree about what is on screen. Sharing the rule is what makes that identity structural; a second
//   copy in the Text View would be a promise to keep them aligned by hand.
//
//   It is pure and document-only (no widgets, no services), which is what keeps it testable headlessly.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonPointer.hpp>

namespace vje
{
	class JsonDocument;

	//-----------------------------------------------------------------------------------------------------------------
	// What a selection resolves to: which face to show, for which container, with which child made current.
	//-----------------------------------------------------------------------------------------------------------------

	struct NodePresentation
	{
		enum class Mode
		{
			Nothing,        // No document, or a pointer that does not resolve.
			ObjectForm,     // The labeled form: an object, or a scalar document root.
			ArrayTable      // The spreadsheet-style table.
		};

		Mode        mode = Mode::Nothing;
		JsonPointer container;          // The node the form / table presents.
		JsonPointer focus;              // The child to make current; meaningful only while hasFocus is true.
		bool        hasFocus = false;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// EDITOR-02 / 03's presentation rule. A scalar resolves to its PARENT plus itself as the focus; a scalar ROOT is
	// the one scalar that presents itself, as a lone single-row form.
	//-----------------------------------------------------------------------------------------------------------------

	NodePresentation resolve_presentation ( const JsonDocument* document, const JsonPointer& pointer );
}
