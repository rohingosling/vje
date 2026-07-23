//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   DocumentIo -- the UI-free load/save boundary for the document. Load parses text (or
//   a UTF-8 file) into a JsonNode tree, reporting a line/column position on malformed input (FILE-06); it accepts and
//   preserves pre-existing duplicate object keys (VAL-02), listing them for the SET-03 load policy. Save renders the
//   tree through the document format profile (JsonFormatter) and produces the exact bytes File > Save writes: UTF-8
//   with NO BOM, LF line endings, and a single trailing newline (FILE-03). new_document() supplies a fresh empty
//   root.
//
//   The functions are synchronous; running load/save off the UI thread (NFR-04) is the application layer's concern.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/document/JsonNode.hpp>
#include <vje_core/services/JsonFormatter.hpp>
#include <vje_core/services/JsonParser.hpp>

#include <QByteArray>
#include <QString>

#include <memory>
#include <vector>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The outcome of a load: on success root is non-null and duplicateKeys lists any preserved duplicates; on failure
	// ok is false, root is null, and error carries the reason and position (a file-access failure reports offset 0).
	//-----------------------------------------------------------------------------------------------------------------

	struct LoadResult
	{
		std::unique_ptr<JsonNode> root;
		bool                      ok = false;
		ParseError                error;
		std::vector<DuplicateKey> duplicateKeys;
	};

	//*****************************************************************************************************************
	// Class: DocumentIo
	//*****************************************************************************************************************

	class DocumentIo
	{
		//=============================================================================================================
		// Load
		//=============================================================================================================

	public:

		// Parse in-memory text (FILE-06). The single load entry point; load_file() decodes then delegates here.

		static LoadResult load_text ( const QString& text );

		// Read path as UTF-8 (BOM tolerated on input) and parse it. A file-access failure yields ok == false with a
		// human-readable error.message and line/column/offset 0.

		static LoadResult load_file ( const QString& path );

		//=============================================================================================================
		// Save
		//=============================================================================================================

	public:

		// The exact bytes File > Save writes: format profile output as UTF-8 (no BOM), LF newlines, one trailing LF.

		static QByteArray save_bytes ( const JsonNode& root, const FormatProfile& profile );

		// Write save_bytes() to path. Returns true on success; on failure returns false and sets *errorMessage.

		static bool save_file ( const QString& path, const JsonNode& root, const FormatProfile& profile, QString* errorMessage = nullptr );

		//=============================================================================================================
		// New document
		//=============================================================================================================

	public:

		static std::unique_ptr<JsonNode> new_document ();     // A fresh empty object {}.
	};
}
