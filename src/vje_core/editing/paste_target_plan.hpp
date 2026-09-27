//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   paste_target_plan -- EDIT-16's one decision: given a list of values pasted at a node, WHERE do the values land and
//   what are they CALLED? Pure, so the whole rule is pinned by a headless test rather than inferred from what a table
//   looked like afterwards.
//
//   WHY IT IS A FUNCTION AND NOT TWO IMPLEMENTATIONS. EDIT-16 states as a requirement that a one-dimensional array
//   gives the same result whether it was selected in the TREE or its single column was selected in the table. Two
//   routes reach that question from opposite ends of the application -- MainWindow's node paste and the array table's
//   header paste -- and two implementations of one rule are exactly how an invariant like that stops being true. It
//   also has to live where a test can reach it: there is no MainWindow harness (lesson D27), so a rule left in the
//   window would have no case behind it.
//
//   THE ROUTE IS A PARAMETER RATHER THAN AN INFERENCE, and that changed in Phase 15h.2. It used to be read off the
//   PRESENCE of targetColumnName -- the header route supplied one, the tree route did not -- which worked only while
//   the two routes agreed on the one case where a header selection has no column key: a single-column table, where
//   both passed nothing. EDIT-16 now makes them differ there (the tree converts the array, the header overwrites its
//   values), so the inference is not merely awkward, it is UNABLE TO EXPRESS THE REQUIREMENT.
//
//   The one sentence the whole split follows from: a TREE selection names an ARRAY and asks what the array should
//   become, while a COLUMN selection names a COLUMN and asks what that column should hold.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

#include <optional>

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// What the paste does with the values. Refused carries the reason; every other kind carries a name, which is empty
	// where the target takes BARE values (there being nothing to attach a name to).
	//-----------------------------------------------------------------------------------------------------------------

	enum class PasteTargetKind
	{
		Refused,
		Object,                  // One new member holding an array of the values, keyed `name`.
		EmptyArray,              // The array BECOMES the values.
		OneDimensionalArray,     // Overwrite element by element; extras append as bare elements.
		ConvertToArrayOfObjects, // Each bare value moves under `existingName`; the values arrive as a new `name` column.
		ArrayOfObjects           // A column under `name`: created where absent, de-duplicated where present.
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Which selection the paste was aimed from. EDIT-16's two answers differ by this and by nothing else.
	//-----------------------------------------------------------------------------------------------------------------

	enum class PasteRoute
	{
		Node,          // A node selected in the tree -- the target is an ARRAY, and the question is what it should become.
		Column,        // A column selected from its header, pasted OVER -- the question is what that column holds.
		InsertColumn   // A column selected from its header, pasted as a NEW column in front of it (EDITOR-18, 2026-09-23).
	};

	// InsertColumn plans as the NODE route does -- the array gains a column, or a single-column array is reshaped to
	// take one -- because an inserted column is a new column exactly as a tree paste's is. It differs in two places
	// only. WHERE the column goes is the applier's business (paste_value_list places it before the column aimed at),
	// and a name is TAKEN if ANY element carries it rather than every one: an insert must never write into a cell
	// that already exists, and the node route's every-element test would let a ragged column's name through and then
	// overwrite the elements that have it.

	struct PasteTargetPlan
	{
		PasteTargetKind kind = PasteTargetKind::Refused;

		// What the values are written under. Empty for EmptyArray and OneDimensionalArray, which hold values rather
		// than named members -- writing them under a name would change what kind of array the target is.

		QString name;

		// ConvertToArrayOfObjects only: the key the array's EXISTING bare values move under as it is reshaped. It is
		// section 2.12's single-column name, which is the same rule that named the column on the way out -- so a
		// `data` array converted by a paste keeps its values under `data`.

		QString existingName;

		// Non-empty only when kind is Refused, and phrased for the user (VAL-05 reports it in both channels).

		QString refusal;

		bool is_refused () const { return kind == PasteTargetKind::Refused; }
	};

	//-----------------------------------------------------------------------------------------------------------------
	// EDIT-16. target is the node the paste is aimed at; listName is the name a copied column travels with (empty for a
	// copied row, which is an element rather than a named list); route says which selection aimed it; targetColumnName
	// is the column the HEADER route aimed at, absent on a single-column table which has a name but no member key.
	//
	// THE NAME IS THE LIST'S OWN, AND THE `(copy)` SEQUENCE IS A COLLISION MARKER (Phase 15h.2). A column named `roles`
	// arrives as `roles`; only where that key is already taken -- an object's member, or a column every element carries
	// -- does EDIT-07's sequence give `roles (copy)`, then `roles (copy 2)`. That needs the target, which is why the
	// de-duplication happens here rather than at the call site. Until 15h.2 the suffix was unconditional, which renamed
	// what nothing had collided with and left the user to rename it back.
	//-----------------------------------------------------------------------------------------------------------------

	PasteTargetPlan plan_paste_target
	(
		const JsonNode*               target,
		const QString&                listName,
		PasteRoute                    route,
		const std::optional<QString>& targetColumnName = std::nullopt
	);

	//-----------------------------------------------------------------------------------------------------------------
	// Does this array render as a single column (EDITOR-03 / section 2.12's shape table)? True unless every element is
	// an object and there is at least one -- so a scalar array, a mixed-kind array, an array of arrays, and an array
	// only PARTLY of objects are all single-column, which is what EDIT-16 means by "one-dimensional".
	//
	// Exposed because the table projection asks the same question, and one answer is the point.
	//-----------------------------------------------------------------------------------------------------------------

	bool renders_single_column ( const JsonNode& array );
}
