//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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

#include <vje_core/services/CellPasteConverter.hpp>

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

	QString cell_display_text ( const JsonNode* node, StringDisplay stringDisplay )
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
				// Code View, which is where the JSON text itself is edited -- and so do the escapes, which is why what
				// the grid shows of a tab or a line break is the user's choice (SET-03) rather than a fixed rendering.

				return string_display_text ( node->string_value (), stringDisplay );
			}
		}

		return QString ();
	}

	QString cell_edit_text ( const JsonNode* node, StringDisplay stringDisplay )
	{
		if ( !is_editable_cell ( node ) )
		{
			return QString ();
		}

		// A string opens in its EDIT notation, which differs from its display notation under exactly one mode:
		// Flattened shows a value with its control characters removed and cannot take an edit back losslessly, so its
		// editor speaks Escaped. Every other kind edits as it displays.

		if ( node->kind () == JsonKind::String )
		{
			return string_edit_text ( node->string_value (), stringDisplay );
		}

		return cell_display_text ( node, stringDisplay );
	}

	//=================================================================================================================
	// Capabilities
	//=================================================================================================================

	std::unique_ptr<JsonNode> typed_entry_value ( const QString& text, StringDisplay stringDisplay )
	{
		std::unique_ptr<JsonNode> interpreted = CellPasteConverter::interpret_typed_entry ( text );

		// Only the bare-word fallback is the notation's. A literal that parsed -- a number, a boolean, null, or a
		// QUOTED string -- has already been read by JsonParser under JSON's own escape rules, and decoding it a second
		// time would resolve one backslash too many.

		const bool isQuotedLiteral = text.trimmed ().startsWith ( QLatin1Char ( '"' ) );

		if ( ( interpreted == nullptr ) || ( interpreted->kind () != JsonKind::String ) || isQuotedLiteral )
		{
			return interpreted;
		}

		QString committed;

		if ( !string_commit_value ( text, stringDisplay, committed ) )
		{
			return nullptr;                                // A malformed escape: refused, exactly as a bad number is.
		}

		return JsonNode::make_string ( committed );
	}

	bool is_editable_cell ( const JsonNode* node )
	{
		return cell_content ( node ) == CellContent::Scalar;
	}

	bool is_drill_in_cell ( const JsonNode* node )
	{
		return cell_content ( node ) == CellContent::Container;
	}
}
