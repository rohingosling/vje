//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   column_naming -- what a SINGLE-COLUMN array's one column is called (spec section 2.12, EDIT-16).
//
//   The rule is the array's own member key, falling back to the literal "value" only where the array itself has none:
//   an array that is a member of an object has a name the user chose and it belongs at the top of the column, while a
//   root array, or an array nested inside another array, has only a position -- and a column headed "0" would say less
//   than nothing.
//
//   IT LIVES HERE BECAUSE IT HAS TWO CONSUMERS AND MUST NOT DRIFT. CsvCodec writes it as a CSV header; the array
//   table's clipboard writes it as the NAME a copied column travels with (EDIT-16), which is what a paste into an
//   object or an array of objects then writes the values under. If the two computed it separately, the same `roles`
//   column would export headed `roles` and paste as something else -- which is exactly the defect the documentation
//   sweep caught before this was written.
//
//   Derived from the node rather than passed in, so it is a function of the array alone: no document, no pointer, and
//   a headless test can pin it.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

namespace vje
{
	class JsonNode;

	namespace column_naming
	{
		// The name of a single-column array's one column: the array's own member key, or UNNAMED below.

		QString single_column_name ( const JsonNode& array );

		// What a column is called when the array carries no name of its own. A literal rather than a computed value,
		// so the CSV header and the clipboard's column name are the same string by construction.

		inline const QString UNNAMED = QStringLiteral ( "value" );
	}
}
