//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonShapeComparer -- the structural shape-compatibility engine for the container cell paste check (EDITOR-11). Two values are shape-compatible when:
//
//     - either is null (null is a wildcard at every level);
//     - both are scalars of the same kind (String / Number / Boolean);
//     - both are objects with the same key SET (recursive, order-insensitive) and pairwise-compatible member values;
//     - both are arrays whose ELEMENT shapes are compatible -- an empty array matches any array, and length is
//       ignored.
//
//   A column's reference shape is derived from its OTHER non-null values; when those values are themselves already
//   heterogeneous the shape check is waived (an already-jagged column accepts anything).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vector>

namespace vje
{
	class JsonNode;

	//*****************************************************************************************************************
	// Class: JsonShapeComparer
	//*****************************************************************************************************************

	class JsonShapeComparer
	{
		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		// The core relation: are a and b the same shape (per the rules above)?

		static bool shapes_compatible ( const JsonNode& a, const JsonNode& b );

		// Is candidate acceptable for a column whose other cell values are columnValues? Returns true when the column
		// has no constraint (empty, or all-null) OR is already heterogeneous (the check is waived), else the
		// compatibility of candidate against the column's homogeneous reference shape.

		static bool compatible_with_column ( const JsonNode& candidate, const std::vector<const JsonNode*>& columnValues );

		// Do the non-null members of columnValues share one shape? False when they are mutually heterogeneous (the
		// waive case) or when there is no non-null value (treated as homogeneous / unconstrained).

		static bool column_is_homogeneous ( const std::vector<const JsonNode*>& columnValues );
	};
}
