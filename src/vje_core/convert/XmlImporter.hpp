//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   XmlImporter -- XML -> JSON import (FILE-13). XML is parsed once
//   (QXmlStreamReader) into an intermediate XmlElement tree, then a chosen IXmlImportStrategy maps that tree to a
//   JsonNode document. All four strategies share ONE structural shell: an element becomes an object member named by
//   the element name, nested elements nest, and same-named sibling elements collapse into an array under that name.
//   The strategies differ only in how they place an element's ATTRIBUTES and TEXT content, trading round-trip
//   fidelity against readability:
//
//     - BadgerFish            : lossless -- attributes under @-prefixed keys, text under "$"; always an object.
//     - Direct attribute keys : lossy, default -- attributes as sibling members, text under "content".
//     - Grouped attributes    : lossy -- attributes in a nested "attributes" object, text under "content".
//     - Custom flattened      : lossy -- attributes and text as direct members; the text key is configurable
//                               (default: the element's own name), with a "-text" suffix on collision.
//
//   For the three lossy strategies a SIMPLE element (no attributes and no child elements) collapses to a scalar --
//   its text -- so ordinary data reads cleanly (<age>30</age> -> "age": "30"), rather than wrapping every leaf in an
//   object. BadgerFish never collapses, so it retains full structure.
//
//   Scalars map as strings by default; the Infer-scalar-types toggle instead types null / true / false and JSON
//   INTEGERS, while decimals and exponents stay strings (so version="1.0" survives as text) -- a narrower inference
//   than JSON's, chosen so type-guessing does not silently rewrite values.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>

#include <memory>
#include <vector>

namespace vje
{
	class JsonNode;

	//-----------------------------------------------------------------------------------------------------------------
	// The intermediate parsed form: one XML element with its ordered attributes, its concatenated (trimmed) text
	// content, and its ordered child elements. Built by XmlImporter's parser; consumed by the strategies.
	//-----------------------------------------------------------------------------------------------------------------

	struct XmlAttribute
	{
		QString name;
		QString value;
	};

	struct XmlElement
	{
		QString                   name;
		std::vector<XmlAttribute> attributes;
		QString                   text;                       // Concatenated character data, trimmed; empty if none.
		std::vector<XmlElement>   children;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The four conversion strategies (FILE-13).
	//-----------------------------------------------------------------------------------------------------------------

	enum class XmlImportStrategyKind
	{
		BadgerFish,
		DirectAttributeKeys,               // The default ("(Recommended)").
		GroupedAttributes,
		CustomFlattened
	};

	//*****************************************************************************************************************
	// Class: IXmlImportStrategy -- maps a parsed element to its JSON value; the structural shell is shared here.
	//*****************************************************************************************************************

	class IXmlImportStrategy
	{
		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		virtual ~IXmlImportStrategy () = default;

		virtual QString display_name () const = 0;
		virtual QString description  () const = 0;
		virtual bool    is_default   () const { return false; }

		// Map one element to its JSON value. Recurses (via the shared shell) into children.

		virtual std::unique_ptr<JsonNode> convert ( const XmlElement& element, bool inferScalars ) const = 0;

		//=============================================================================================================
		// Shared Shell (available to every strategy)
		//=============================================================================================================

	protected:

		// A scalar value from text: a plain string unless inference is on (null / true / false / JSON integer only).

		std::unique_ptr<JsonNode> map_scalar ( const QString& text, bool inferScalars ) const;

		// Append the members contributed by child elements, collapsing same-named siblings into an array. Recurses
		// through convert() so the same strategy maps the descendants.

		void append_child_members ( JsonNode& object, const XmlElement& element, bool inferScalars ) const;

		// A simple element: no attributes and no child elements (text-only or empty).

		static bool is_leaf ( const XmlElement& element );
	};

	//*****************************************************************************************************************
	// Class: XmlImporter -- parse the XML and drive a strategy.
	//*****************************************************************************************************************

	class XmlImporter
	{
		//=============================================================================================================
		// Types
		//=============================================================================================================

	public:

		struct Result
		{
			std::unique_ptr<JsonNode> root;
			bool                      ok = false;
			QString                   error;
		};

		//=============================================================================================================
		// Public Interface
		//=============================================================================================================

	public:

		// Build a strategy by kind. textValueKey applies to CustomFlattened only (empty -> the element's own name).

		static std::unique_ptr<IXmlImportStrategy> make_strategy ( XmlImportStrategyKind kind, const QString& textValueKey = QString () );

		// Import XML text with the given strategy. The root JSON is a single-member object keyed by the XML root
		// element's name. On malformed XML, ok is false and error reports the reason with line/column.

		static Result import_text ( const QString& xml, const IXmlImportStrategy& strategy, bool inferScalars );

		//=============================================================================================================
		// Internals
		//=============================================================================================================

	private:

		static bool parse_root ( const QString& xml, XmlElement& outRoot, QString& outError );
	};
}
