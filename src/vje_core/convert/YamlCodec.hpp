//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   YamlCodec -- YAML import/export via yaml-cpp 0.8.0 (OQ-A4; the only external library
//   VJE fetches, gated behind VJE_ENABLE_YAML). A near-lossless whole-document mapping:
//
//     - Export walks the JsonNode with a YAML::Emitter. Numbers emit as their RAW TOKEN as a plain scalar (so 1.50
//       survives verbatim, FILE-10); booleans and null emit plain (true / false / ~); every STRING is emitted
//       double-quoted, so a string that looks like a number/boolean/null keeps its type on re-import.
//     - Import parses with yaml-cpp and walks the node tree: maps -> objects (document order preserved, FILE-04),
//       sequences -> arrays, null nodes -> null. A quoted scalar (yaml-cpp's non-specific "!" tag) is always a
//       string; a plain scalar is inferred (null / true / false / a JSON number, else a string). This pairing makes
//       our own exports round-trip exactly and reads ordinary third-party YAML sensibly.
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

	//*****************************************************************************************************************
	// Class: YamlCodec
	//*****************************************************************************************************************

	class YamlCodec
	{
		//=============================================================================================================
		// Types
		//=============================================================================================================

	public:

		struct ImportResult
		{
			std::unique_ptr<JsonNode> root;
			bool                      ok = false;
			QString                   error;
		};

		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		// Serialize the whole document to YAML (no trailing newline is added beyond yaml-cpp's own output).

		static QString to_yaml ( const JsonNode& root );

		// Parse YAML text into a document. On a YAML syntax error, ok is false and error carries yaml-cpp's message.

		static ImportResult from_yaml ( const QString& text );
	};
}
