//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   edit_transforms -- the pure, side-effect-free node-to-node computations that back several edit commands. Each
//   function takes a source node and returns a freshly built subtree (deep copies of any retained values), so the
//   caller can hand the result to a ReplaceNodeCommand / InsertNodeCommand without aliasing the live tree. Keeping
//   the logic here (free functions, no QUndoStack, no document) makes the conversion and array-transform rules
//   directly unit-testable.
//
//   Covers: the EDIT-09 scalar/container type-conversion matrix; the three array transforms Normalize Array Elements
//   (EDIT-11), Convert Array to Objects (EDIT-12), and Convert Objects to Array (EDIT-13); the EDIT-07 duplicate-key
//   naming rule; and the JSON-number check used by the conversion matrix.
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
	namespace edit_transforms
	{
		// True when text is, in its entirety, a single RFC 8259 number token (no surrounding whitespace or trailing
		// characters). Uses the shared JsonLexer so the definition of "number" matches the parser exactly.

		bool is_json_number ( const QString& text );

		// EDIT-09: build the value that results from re-typing node to newKind, per the spec's conversion convention.
		// Any source kind is accepted (scalars and containers). Converting to the same kind returns a clone.

		std::unique_ptr<JsonNode> convert_node ( const JsonNode& node, JsonKind newKind );

		// EDIT-11: rewrite an array-of-objects so every element carries the union of all member keys, in
		// first-encountered order, filling any key an element lacks with null; existing values are preserved. Idempotent.
		// Precondition: array.kind() == Array and every element kind() == Object.

		std::unique_ptr<JsonNode> normalize_array_elements ( const JsonNode& array );

		// EDIT-12: replace an array with an object keyed by former index ("0", "1", ...), preserving order.
		// Precondition: array.kind() == Array.

		std::unique_ptr<JsonNode> array_to_object ( const JsonNode& array );

		// EDIT-13: replace an object with an array of its member values in member order; keys are discarded (lossy).
		// Precondition: object.kind() == Object.

		std::unique_ptr<JsonNode> object_to_array ( const JsonNode& object );

		// EDIT-07: a sibling key for a duplicated object member that does not collide with an existing key --
		// "key (copy)", then "key (copy 2)", "key (copy 3)", ... until unique within object.

		QString duplicate_key_name ( const JsonNode& object, const QString& baseKey );
	}
}
