//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   CellPasteConverter -- the array-table cell-clipboard TYPE engine (EDITOR-11): the
//   paste conversion matrix and the EDITOR-12 JSON-literal typed-entry interpretation, as pure functions preserving
//   raw number tokens (FILE-10). It is deliberately STRICTER than EDIT-09's change-type coercions -- a mismatch is
//   refused with a reason rather than silently degraded.
//
//   Scalar pairings resolve here directly (Converted or Incompatible). CONTAINER sources into a matching container /
//   untyped target return NeedsShapeCheck: the structural check itself is JsonShapeComparer's job (the controller
//   runs it against the column, then applies the SET-05 jagged flow). "null is universal": a null source pastes
//   anywhere as null, and an untyped (null / provisional / missing) SCALAR target takes any scalar source as-is.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonNode.hpp>

#include <QString>

#include <memory>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The target cell's type. Untyped covers a null cell, a provisional-row cell, and a missing (ragged) cell -- all
	// accept a value "as-is" (a container as-is is still shape-checked against the column).
	//-----------------------------------------------------------------------------------------------------------------

	enum class CellTarget
	{
		String,
		Number,
		Boolean,
		Object,
		Array,
		Untyped
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The outcome of resolving a paste:
	//   - Converted:      value holds the node to place in the cell.
	//   - NeedsShapeCheck: value holds the (cloned) container source; the caller shape-checks it against the column.
	//   - Incompatible:   message describes why (the incompatible-data-type message box); value is null.
	//-----------------------------------------------------------------------------------------------------------------

	enum class PasteResolution
	{
		Converted,
		NeedsShapeCheck,
		Incompatible
	};

	struct CellPasteOutcome
	{
		PasteResolution           resolution = PasteResolution::Incompatible;
		std::unique_ptr<JsonNode> value;
		QString                   message;
	};

	//*****************************************************************************************************************
	// Class: CellPasteConverter
	//*****************************************************************************************************************

	class CellPasteConverter
	{
		//=============================================================================================================
		// Paste resolution (the conversion matrix)
		//=============================================================================================================

	public:

		static CellPasteOutcome resolve_paste ( const JsonNode& source, CellTarget target );

		//=============================================================================================================
		// Value resolution helpers
		//=============================================================================================================

	public:

		// EDITOR-11: interpret clipboard TEXT as a paste source -- valid JSON parses to its type (containers
		// included); anything else is a plain string.

		static std::unique_ptr<JsonNode> resolve_clipboard_text ( const QString& text );

		// EDITOR-12: interpret committed cell TEXT as a JSON literal for typed entry into a null / provisional /
		// missing cell -- a valid JSON number / true / false / null / quoted string commits as that value; ANYTHING
		// else (including a bare object / array literal) commits as a plain string (quoting is the escape hatch).

		static std::unique_ptr<JsonNode> interpret_typed_entry ( const QString& text );

		//=============================================================================================================
		// Internals
		//=============================================================================================================

	private:

		static CellPasteOutcome converted   ( std::unique_ptr<JsonNode> value );
		static CellPasteOutcome incompatible ( const QString& sourceName, CellTarget target );
	};
}
