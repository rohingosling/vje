//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   CsvCodec -- a hand-rolled RFC 4180 CSV import/export codec (FILE-11). No external
//   dependency: CSV is simple enough to scan directly, and hand-rolling keeps the quoting/line-ending rules exact.
//
//   Export operates on a tree selection that is an ARRAY OF FLAT OBJECTS. Columns are the union of member keys in
//   first-encountered order, header row first; a null value writes the literal text `null`, an empty string writes an
//   empty cell, a number writes its raw token, a boolean writes true/false, a missing key writes an empty cell.
//   Fields are quoted per RFC 4180 (a field containing a comma, double-quote, CR, or LF is wrapped in double-quotes
//   with embedded quotes doubled); records are separated by CRLF. Export is only meaningful for a UNIFORM key set --
//   is_exportable() reports that precondition so the UI can gate the command (a ragged array is repaired by
//   Normalize Array Elements, EDIT-11).
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
	// The outcome of an import or export: on success ok is true (import carries the built root); on failure ok is false
	// and error explains why (an empty file, a malformed selection, a ragged export array).
	//-----------------------------------------------------------------------------------------------------------------

	struct CsvExportResult
	{
		bool    ok = false;
		QString csv;
		QString error;
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

		// True when node is a non-empty array whose every element is an object of scalar-only members with a UNIFORM
		// key set (every element carries the same keys). This is the FILE-11 export precondition; the UI gates the
		// Export CSV command on it.

		static bool is_exportable ( const JsonNode& node );

		// Serialize an array of flat objects to RFC 4180 CSV. Fails (ok == false) when node is not an array of flat
		// objects; a ragged (non-uniform) array still exports, filling absent members with empty cells, because the
		// column set is the union of keys -- but the UI only offers export when is_exportable() holds.

		static CsvExportResult export_array ( const JsonNode& node );

		// Parse CSV text into a root array of flat objects (header row -> keys). Fails on empty input.

		static CsvImportResult import_text ( const QString& text );
	};
}
