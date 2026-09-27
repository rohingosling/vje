//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include <optional>
#include <vector>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// A validation outcome: on success root is non-null; on failure ok is false and issue carries the reason and
	// position. duplicateKeys is populated even on success (the load case, so SET-03 can warn).
	//-----------------------------------------------------------------------------------------------------------------

	//-----------------------------------------------------------------------------------------------------------------
	// One object's duplicated key, and how many times that object carries it -- the unit of the CENSUS below.
	//-----------------------------------------------------------------------------------------------------------------

	struct DuplicateKeyGroup
	{
		QString key;
		int     count = 0;
	};

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

		// Validate JSON text. Duplicate object keys are ACCEPTED and listed in the result, exactly as a load accepts
		// them -- RFC 8259 permits them and FILE-04 preserves them. What to DO about them is the caller's policy, and
		// introduced_duplicate below is how the edit paths ask it.
		//
		// IT USED TO TAKE A rejectDuplicates FLAG, and the Code View commit passed true: fail on ANY duplicate in the
		// text. That is stricter than VAL-02, which rejects a duplicate an EDIT INTRODUCES -- so a document that
		// ARRIVED with duplicates could never have a Code View edit committed at all, and the refusal named a node the
		// user had not touched and could not see from where they were typing. Reported 2026-08-20 against a file whose
		// duplicates are its own FILE-04 demonstration. The flag is deleted rather than left unused, because a mode
		// nothing produces is how a defect hides for six phases (lesson D19).

		static ValidationResult validate ( const QString& text );

		//=============================================================================================================
		// VAL-02 -- the duplicate-key policy
		//
		// Two questions, because the edit paths ask two different ones. An in-place edit knows the object and the key
		// it is about to write, so it asks introduces_duplicate. A whole-document replacement -- the Code View commit
		// -- knows neither, so it compares what the document HAD against what it would have.
		//=============================================================================================================

	public:

		// Would setting a member's key to key collide with an EXISTING member of object? ignoreIndex excludes a
		// member from the check (a rename comparing against its own slot); pass -1 for an add/paste.

		static bool introduces_duplicate ( const JsonNode& object, const QString& key, int ignoreIndex = -1 );

		// Every duplicated key in the tree, as ( key, occurrences ) pairs -- one entry per OBJECT that carries a key
		// more than once -- in a stable sorted order, so two censuses compare as multisets.
		//
		// IT CARRIES NO POSITION, AND THAT IS THE POINT. The obvious identity for "the same duplicate" is the
		// containing object's JSON Pointer, and it is exactly wrong here: an array element's pointer token IS its
		// position, so deleting one element renames every duplicate below it and a pointer-keyed comparison would
		// report them all as newly introduced. Deleting array elements is the edit the reported defect was made
		// during, so an identity that breaks on it would have replaced one false refusal with another.

		static std::vector<DuplicateKeyGroup> duplicate_census ( const JsonNode& root );

		// VAL-02 for a whole-document replacement: which key, if any, does `after` duplicate that `before` did not?
		// nullopt when after's census is CONTAINED in before's -- so an edit that moves, deletes or re-shapes the
		// duplicates a file arrived with introduces nothing, and only an edit that ADDS duplication is refused.
		//
		// Multiset containment rather than a per-key total, which is what catches both shapes of introduction: a
		// fourth "colour" in an object that carried three (a group of 4 matches no group of 3), and a second object
		// gaining a pair of its own.

		static std::optional<QString> introduced_duplicate ( const JsonNode& before, const JsonNode& after );

		//=============================================================================================================
		// VAL-03 -- Form numeric input
		//=============================================================================================================

	public:

		// Is text (surrounding whitespace tolerated) a single valid JSON number? The Form number editor's commit gate.

		static bool is_valid_number ( const QString& text );
	};
}
