//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   cell_presentation implementation. See the header for why the object form and the array table share it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "models/cell_presentation.hpp"

namespace vje
{
	//=================================================================================================================
	// Classification
	//=================================================================================================================

	CellContent cell_content ( const JsonNode* node )
	{
		// The absent member of a ragged row (EDITOR-03). Not an error and not an empty string -- a distinct cell kind,
		// because pasting into it CREATES the member rather than setting a value (EDITOR-11, Phase 9).

		if ( node == nullptr )
		{
			return CellContent::Missing;
		}

		if ( node->is_container () )
		{
			return CellContent::Container;
		}

		if ( node->kind () == JsonKind::Null )
		{
			return CellContent::Null;
		}

		return CellContent::Scalar;
	}

	//=================================================================================================================
	// Display
	//=================================================================================================================

	QString cell_display_text ( const JsonNode* node )
	{
		if ( node == nullptr )
		{
			return QString ();
		}

		switch ( node->kind () )
		{
			case JsonKind::Object:
			{
				return cell_text::OBJECT_PLACEHOLDER;
			}

			case JsonKind::Array:
			{
				return cell_text::ARRAY_PLACEHOLDER;
			}

			case JsonKind::Null:
			{
				return cell_text::NULL_PLACEHOLDER;
			}

			case JsonKind::Boolean:
			{
				return node->boolean_value () ? cell_text::BOOLEAN_TRUE : cell_text::BOOLEAN_FALSE;
			}

			case JsonKind::Number:
			{
				// The RAW token, never a re-rendered double. A document holding 1.50 or 1e3 shows exactly that, and a
				// round trip through the grid cannot quietly rewrite it (FILE-10).

				return node->number_token ();
			}

			case JsonKind::String:
			{
				// Unquoted: the grid shows the string's CONTENT, the way a spreadsheet does. The quotes belong to the
				// Code View, which is where the JSON text itself is edited.

				return node->string_value ();
			}
		}

		return QString ();
	}

	QString cell_edit_text ( const JsonNode* node )
	{
		return is_editable_cell ( node ) ? cell_display_text ( node ) : QString ();
	}

	//=================================================================================================================
	// Capabilities
	//=================================================================================================================

	bool is_editable_cell ( const JsonNode* node )
	{
		return cell_content ( node ) == CellContent::Scalar;
	}

	bool is_drill_in_cell ( const JsonNode* node )
	{
		return cell_content ( node ) == CellContent::Container;
	}
}
