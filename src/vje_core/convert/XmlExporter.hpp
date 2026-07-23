//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   XmlExporter -- the simple, readable JSON -> XML mapping. Export only: it is NOT the
//   inverse of any XmlImporter strategy (import fidelity is the strategy's job). The whole document is wrapped in a
//   <document> root; an object member becomes an element named by its key, an array becomes repeated <item> elements,
//   and a scalar becomes the element's text content (number -> raw token, boolean -> true/false, string -> its text,
//   null -> an empty element). Output is 2-space indented, UTF-8, with an XML declaration and a single trailing
//   newline; text content is XML-escaped (& < >). Object keys are written verbatim as element names -- the mapping
//   is best-effort readable and assumes keys are valid XML names.
//
//   Null and the empty string both render as an empty element -- an accepted, documented loss of this readable
//   export (round-trip fidelity is the four-strategy XmlImporter's concern, not this exporter's).
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
	// Class: XmlExporter
	//*****************************************************************************************************************

	class XmlExporter
	{
		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		// Serialize the whole document to XML. Always succeeds (every JSON tree maps).

		static QString export_document ( const JsonNode& root );

		//=============================================================================================================
		// Internals
		//=============================================================================================================

	private:

		static void    write_node ( QString& out, const QString& name, const JsonNode& node, int level );
		static QString escape_text ( const QString& text );
		static QString scalar_text ( const JsonNode& node );
	};
}
