//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonSerializer -- the baseline JsonNode -> text serializer: compact, canonical JSON that preserves member order
//   (FILE-04) and emits number nodes as their raw token verbatim (FILE-10). It is the round-trip counterpart to
//   JsonParser; the configurable pretty-printer (indentation, brace style, separator alignment) is the separate
//   JsonFormatter, delivered with the services phase.
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

	//*****************************************************************************************************************
	// Class: JsonSerializer
	//*****************************************************************************************************************

	class JsonSerializer
	{
	public:

		static QString serialize     ( const JsonNode& node );     // Compact canonical JSON for the whole subtree.
		static QString encode_string ( const QString& value );     // A JSON string literal, quotes included.

	private:

		static void write_value  ( const JsonNode& node, QString& out );
		static void write_string ( const QString& value, QString& out );
	};
}
