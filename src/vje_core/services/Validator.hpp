//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Validator -- the UI-free validation surface:
//
//     - VAL-01: RFC 8259 text validation for a Code View commit / file load, reporting line, column, and reason.
//     - VAL-02: duplicate object-key detection. A LOADED file keeps its duplicates (RFC 8259 permits them); an EDIT
//               that would introduce a new duplicate is rejected. validate() with rejectDuplicates == true fails on
//               any duplicate (the Code View commit path); introduces_duplicate() guards the in-place edit paths.
//     - VAL-03: JSON-number validation for Form numeric input, before commit.
//
//   The number check delegates to edit_transforms::is_json_number so "a JSON number" means exactly what the parser
//   accepts -- one definition across the codebase.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QString>

#include <memory>
#include <vector>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// A validation outcome: on success root is non-null; on failure ok is false and issue carries the reason and
	// position. duplicateKeys is populated even on success (the load case, so SET-03 can warn).
	//-----------------------------------------------------------------------------------------------------------------

	struct ValidationResult
	{
		bool                      ok = false;
		std::unique_ptr<JsonNode> root;
		ParseError                issue;
		std::vector<DuplicateKey> duplicateKeys;
	};

	//*****************************************************************************************************************
	// Class: Validator
	//*****************************************************************************************************************

	class Validator
	{
		//=============================================================================================================
		// VAL-01 / VAL-02 -- text validation
		//=============================================================================================================

	public:

		// Validate JSON text. rejectDuplicates == true (Code View commit / edit paths) fails on the FIRST duplicate
		// key with its position; false (load) accepts the text and lists any duplicates in the result.

		static ValidationResult validate ( const QString& text, bool rejectDuplicates );

		//=============================================================================================================
		// VAL-02 -- edit-path duplicate guard
		//=============================================================================================================

	public:

		// Would setting a member's key to key collide with an EXISTING member of object? ignoreIndex excludes a
		// member from the check (a rename comparing against its own slot); pass -1 for an add/paste.

		static bool introduces_duplicate ( const JsonNode& object, const QString& key, int ignoreIndex = -1 );

		//=============================================================================================================
		// VAL-03 -- Form numeric input
		//=============================================================================================================

	public:

		// Is text (surrounding whitespace tolerated) a single valid JSON number? The Form number editor's commit gate.

		static bool is_valid_number ( const QString& text );
	};
}
