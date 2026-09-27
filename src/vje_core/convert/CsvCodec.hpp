//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CsvCodec -- a hand-rolled RFC 4180 CSV import/export codec (FILE-11). No external
//   dependency: CSV is simple enough to scan directly, and hand-rolling keeps the quoting/line-ending rules exact.
//
//   EXPORT OPERATES ON ANY NON-EMPTY ARRAY, and that is the whole precondition. A table is a table only when it has
//   rows of named fields, so the shape of the output follows the shape of the array rather than being demanded of it:
//
//     - Every element an OBJECT -> the table form. Columns are the union of member keys in first-encountered order
//       (the same rule the Form View's array table and EDIT-11 Normalize both use), header row first.
//     - Anything else -> the SINGLE-COLUMN form: one field per element, headed by the array's own member key, or
//       "value" where the array has no key of its own (a root array, or an array that is itself an array element).
//       This covers an array of scalars and an array of MIXED kinds alike, following the Form View's own rule that a
//       mixed array is one column rather than an invented column set.
//
//   A ragged array needs no repair: absent members write empty cells, so the file is the NORMALIZED view of the array
//   while the document stays ragged. That is deliberately not what EDIT-11 Normalize would write, which fills absent
//   members with `null` -- here an empty cell means "this element does not have that member" and the literal `null`
//   means "it has it, and it is null", a distinction a CSV can carry and is worth carrying.
//
//   Cell text: a null writes `null`, an empty string writes an empty cell, a number writes its raw token, a boolean
//   writes true/false, and a nested OBJECT or ARRAY writes the shared one-slot placeholder (`{...}` / `[...]`, see
//   services/value_placeholders.hpp) -- the same text the Form View's table shows, which is the point of it. Such a
//   cell is LOSSY and cannot be read back: CsvExportResult::placeholderCells counts them so a caller can say so.
//
//   Fields are quoted per RFC 4180 (a field containing a comma, double-quote, CR, or LF is wrapped in double-quotes
//   with embedded quotes doubled); records are separated by CRLF.
//
//   Import parses CSV text (tolerating LF or CRLF, quoted fields, embedded separators/newlines, doubled quotes) into
//   a root array of flat objects: the first record supplies the member keys, each later record one object. A cell is
//   parsed as a JSON number / boolean / null where its text is unambiguously one, otherwise as a string -- CSV
//   carries no types, so this inference is the documented, lossy convention.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

#include <memory>

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// What stands between a node and a CSV export of it. Only two things do, and both are about the node being the
	// wrong SHAPE rather than the wrong content -- there is no such thing as an array CSV export cannot represent, only
	// one it has to represent in a single column or with a placeholder.
	//
	// The wording of each is the UI's (converters.cpp), not the codec's: the same blocker reads differently in a menu
	// tooltip and in an error dialog, and the codec has no business choosing.
	//-----------------------------------------------------------------------------------------------------------------

	enum class CsvExportBlocker
	{
		None,
		NotAnArray,      // A CSV record set needs rows; an object, a scalar or a null has none.
		EmptyArray       // Nothing to write -- not even a header, since the columns come from the elements.
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The outcome of an import or export: on success ok is true (import carries the built root); on failure ok is false
	// and error explains why (an empty file, a selection that is not a non-empty array).
	//-----------------------------------------------------------------------------------------------------------------

	struct CsvExportResult
	{
		bool    ok = false;
		QString csv;
		QString error;

		// How many cells were written as a container placeholder rather than as their value. Non-zero means the file
		// cannot be read back into the document it came from, which is a warning worth passing to the user rather than
		// a failure -- the export they asked for is exactly what they asked for.

		int placeholderCells = 0;
	};

	struct CsvImportResult
	{
		std::unique_ptr<JsonNode> root;
		bool                      ok = false;
		QString                   error;
	};

	//*****************************************************************************************************************
	// Class: CsvCodec
	//*****************************************************************************************************************

	class CsvCodec
	{
		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		// Why this node cannot be exported as CSV, or None when it can. Returned as a REASON rather than as a bool
		// because the UI has to tell the user what is in the way (FILE-11): "the selection is an object" and "the
		// selection is an empty array" are different problems with different fixes, and a bare false says neither.

		static CsvExportBlocker exportability ( const JsonNode& node );

		// exportability(node) == None. The FILE-11 export precondition, kept as its own name because that is the
		// question the enablement and the export itself both ask.

		static bool is_exportable ( const JsonNode& node );

		// Serialize a non-empty array to RFC 4180 CSV, in whichever of the two forms the array's shape calls for (see
		// the header comment). Fails (ok == false) only for the two blockers above.

		static CsvExportResult export_array ( const JsonNode& node );

		// Parse CSV text into a root array of flat objects (header row -> keys). Fails on empty input.

		static CsvImportResult import_text ( const QString& text );
	};
}
