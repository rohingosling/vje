//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   json_order -- the TOTAL order over JSON values that EDIT-15's array sort is built on, and the stable permutation
//   that orders an array by one of its columns. Pure and free of the document, the undo stack and any widget, so the
//   whole of the comparison is pinned by a headless test rather than inferred from what a table looked like.
//
//   WHY THIS IS NOT JsonPathQuery::compare_values. That function answers "is this comparison TRUE?" for a filter
//   predicate, and is deliberately PARTIAL: a cross-kind pair, an absent operand and a container all make every
//   ordering comparison false, because a filter that quietly ordered a string against a number would select elements
//   on a rule the user never wrote. A sort asks a different question -- "which of these comes FIRST?" -- and a
//   comparison that declines to answer it leaves the elements in an arbitrary order instead of a stated one. So the
//   two orders differ on purpose, and neither is the other's bug.
//
//   THE ORDER, AS SPEC EDIT-15 STATES IT: null < false < true < numbers < strings < arrays < objects. A MISSING
//   member sorts as null, which is the value EDIT-11's Normalize fills one with, so a ragged column sorts rather
//   than the command refusing. Numbers compare numerically while their raw tokens are never touched (FILE-10);
//   strings compare as QString::compare does; two containers compare EQUAL, which under a stable sort is what keeps
//   them in document order rather than in some order derived from their contents.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <Qt>
#include <QString>

#include <optional>
#include <vector>

namespace vje
{
	class JsonNode;

	namespace json_order
	{
		// Three-way comparison over the total order above: negative when left sorts first, positive when right does,
		// zero when the two are equal and a stable sort must therefore leave them as it found them.
		//
		// A NULL POINTER MEANS ABSENT and compares exactly as a null node does. That is the whole of the ragged-array
		// rule, stated once here rather than at each of the two call sites that can produce a missing value (an
		// element that is not an object at all, and an object element that lacks the column's member).

		int compare_values ( const JsonNode* left, const JsonNode* right );

		// The stable ordering of array's elements by one column, returned as permutation [ newIndex ] = oldIndex.
		//
		// memberKey names the column: std::nullopt sorts by the ELEMENTS THEMSELVES, which is what a single-column
		// (scalar) array's one column is, while a value sorts by that member of each object element. It is an
		// optional rather than an empty-string sentinel because "" is a legal JSON member key, so the two cases have
		// to be distinguishable by something other than the key's contents.
		//
		// Stability holds in BOTH directions: Qt::DescendingOrder negates the value comparison only, so equal elements
		// are still equal under it and std::stable_sort leaves them in document order rather than reversing them.
		//
		// Precondition: array.kind () == JsonKind::Array.

		std::vector<int> sort_permutation
		(
			const JsonNode&               array,
			const std::optional<QString>& memberKey,
			Qt::SortOrder                 order
		);

		// True when the permutation leaves every element where it already was -- the no-op a sort of an already
		// ordered array produces, which EDIT-15 reports as Unchanged rather than pushing an undo step for.

		bool is_identity ( const std::vector<int>& permutation );
	}
}
