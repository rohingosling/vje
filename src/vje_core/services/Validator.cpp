//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Validator implementation. See Validator.hpp for the VAL-01..03 rule references.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/Validator.hpp>
#include <vje_core/editing/edit_transforms.hpp>

namespace vje
{
	//=================================================================================================================
	// VAL-01 / VAL-02 -- text validation
	//=================================================================================================================

	ValidationResult Validator::validate ( const QString& text, bool rejectDuplicates )
	{
		ParseResult parsed = JsonParser::parse ( text );

		ValidationResult result;
		result.ok            = parsed.ok;
		result.issue         = parsed.error;
		result.duplicateKeys = parsed.duplicateKeys;
		result.root          = std::move ( parsed.root );

		if ( !result.ok )
		{
			return result;                                 // Malformed input: the parse error already describes it.
		}

		// VAL-02 on an edit path: a duplicate key is a rejection with the offending occurrence's position.

		if ( rejectDuplicates && !result.duplicateKeys.empty () )
		{
			const DuplicateKey& first = result.duplicateKeys.front ();

			result.ok            = false;
			result.root.reset ();
			result.issue.message = QStringLiteral ( "Duplicate object key \"" ) + first.key + QStringLiteral ( "\"." );
			result.issue.line    = first.line;
			result.issue.column  = first.column;
			result.issue.offset  = 0;
		}

		return result;
	}

	//=================================================================================================================
	// VAL-02 -- edit-path duplicate guard
	//=================================================================================================================

	bool Validator::introduces_duplicate ( const JsonNode& object, const QString& key, int ignoreIndex )
	{
		if ( object.kind () != JsonKind::Object )
		{
			return false;
		}

		for ( int index = 0; index < object.member_count (); ++index )
		{
			if ( index == ignoreIndex )
			{
				continue;
			}

			if ( object.member_key ( index ) == key )
			{
				return true;
			}
		}

		return false;
	}

	//=================================================================================================================
	// VAL-03 -- Form numeric input
	//=================================================================================================================

	bool Validator::is_valid_number ( const QString& text )
	{
		return edit_transforms::is_json_number ( text.trimmed () );
	}
}
