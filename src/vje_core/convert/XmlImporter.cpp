//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   XmlImporter implementation: the QXmlStreamReader parser (recursive-descent over the token stream, so XML nesting
//   maps directly to recursion with no pointer-invalidation hazard), the four concrete strategies, and the shared
//   shell (child-member building with same-named-sibling collapse, scalar mapping/inference, the leaf test).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/convert/XmlImporter.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/edit_transforms.hpp>

#include <QStringList>
#include <QXmlStreamReader>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// A JSON integer: a valid JSON number carrying no fraction and no exponent. Reuses the lexer-backed number
		// check (so "007" and other malformed tokens are rejected exactly as the parser rejects them) and then
		// excludes the decimal/exponent forms, which the Infer-scalar-types rule deliberately keeps as strings.
		//-------------------------------------------------------------------------------------------------------------

		bool is_json_integer ( const QString& text )
		{
			return edit_transforms::is_json_number ( text )
			    && !text.contains ( QLatin1Char ( '.' ) )
			    && !text.contains ( QLatin1Char ( 'e' ) )
			    && !text.contains ( QLatin1Char ( 'E' ) );
		}

		//*********************************************************************************************************
		// The four strategies. Each differs only in attribute/text placement; the shared shell does the rest.
		//*********************************************************************************************************

		class BadgerFishStrategy : public IXmlImportStrategy
		{
		public:

			QString display_name () const override { return QStringLiteral ( "BadgerFish" ); }
			QString description  () const override { return QStringLiteral ( "Attributes prefixed with @; text content under $. Lossless -- the JSON can be converted back to XML." ); }

			std::unique_ptr<JsonNode> convert ( const XmlElement& element, bool inferScalars ) const override
			{
				// Lossless: always an object, retaining the attribute/content distinction even for a bare leaf.

				std::unique_ptr<JsonNode> object = JsonNode::make_object ();

				for ( const XmlAttribute& attribute : element.attributes )
				{
					object->append_member ( QStringLiteral ( "@" ) + attribute.name, map_scalar ( attribute.value, inferScalars ) );
				}

				append_child_members ( *object, element, inferScalars );

				if ( !element.text.isEmpty () )
				{
					object->append_member ( QStringLiteral ( "$" ), map_scalar ( element.text, inferScalars ) );
				}

				return object;
			}
		};

		class DirectAttributeKeysStrategy : public IXmlImportStrategy
		{
		public:

			QString display_name () const override { return QStringLiteral ( "Direct attribute keys" ); }
			QString description  () const override { return QStringLiteral ( "Attributes become sibling members; text content under a content key. Clean, readable JSON." ); }
			bool    is_default   () const override { return true; }

			std::unique_ptr<JsonNode> convert ( const XmlElement& element, bool inferScalars ) const override
			{
				if ( is_leaf ( element ) )
				{
					return map_scalar ( element.text, inferScalars );
				}

				std::unique_ptr<JsonNode> object = JsonNode::make_object ();

				for ( const XmlAttribute& attribute : element.attributes )
				{
					object->append_member ( attribute.name, map_scalar ( attribute.value, inferScalars ) );
				}

				append_child_members ( *object, element, inferScalars );

				if ( !element.text.isEmpty () )
				{
					object->append_member ( QStringLiteral ( "content" ), map_scalar ( element.text, inferScalars ) );
				}

				return object;
			}
		};

		class GroupedAttributesStrategy : public IXmlImportStrategy
		{
		public:

			QString display_name () const override { return QStringLiteral ( "Grouped attributes" ); }
			QString description  () const override { return QStringLiteral ( "Attributes collected in a nested attributes object; text content under a content key." ); }

			std::unique_ptr<JsonNode> convert ( const XmlElement& element, bool inferScalars ) const override
			{
				if ( is_leaf ( element ) )
				{
					return map_scalar ( element.text, inferScalars );
				}

				std::unique_ptr<JsonNode> object = JsonNode::make_object ();

				if ( !element.attributes.empty () )
				{
					std::unique_ptr<JsonNode> attributes = JsonNode::make_object ();

					for ( const XmlAttribute& attribute : element.attributes )
					{
						attributes->append_member ( attribute.name, map_scalar ( attribute.value, inferScalars ) );
					}

					object->append_member ( QStringLiteral ( "attributes" ), std::move ( attributes ) );
				}

				append_child_members ( *object, element, inferScalars );

				if ( !element.text.isEmpty () )
				{
					object->append_member ( QStringLiteral ( "content" ), map_scalar ( element.text, inferScalars ) );
				}

				return object;
			}
		};

		class CustomFlattenedStrategy : public IXmlImportStrategy
		{
		public:

			explicit CustomFlattenedStrategy ( const QString& textValueKey ) : textValueKey ( textValueKey ) {}

			QString display_name () const override { return QStringLiteral ( "Custom flattened" ); }
			QString description  () const override { return QStringLiteral ( "Attributes and text content all become direct members; the text member's key is configurable." ); }

			std::unique_ptr<JsonNode> convert ( const XmlElement& element, bool inferScalars ) const override
			{
				if ( is_leaf ( element ) )
				{
					return map_scalar ( element.text, inferScalars );
				}

				std::unique_ptr<JsonNode> object = JsonNode::make_object ();

				for ( const XmlAttribute& attribute : element.attributes )
				{
					object->append_member ( attribute.name, map_scalar ( attribute.value, inferScalars ) );
				}

				append_child_members ( *object, element, inferScalars );

				if ( !element.text.isEmpty () )
				{
					// The configured key, or the element's own name by default; suffix "-text" until it is unique
					// against the attribute/child members already placed (the collision rule).

					QString key = textValueKey.isEmpty () ? element.name : textValueKey;

					while ( object->has_member ( key ) )
					{
						key += QStringLiteral ( "-text" );
					}

					object->append_member ( key, map_scalar ( element.text, inferScalars ) );
				}

				return object;
			}

		private:

			QString textValueKey;
		};
	}

	//*****************************************************************************************************************
	// IXmlImportStrategy -- the shared shell.
	//*****************************************************************************************************************

	std::unique_ptr<JsonNode> IXmlImportStrategy::map_scalar ( const QString& text, bool inferScalars ) const
	{
		if ( !inferScalars )
		{
			return JsonNode::make_string ( text );
		}

		if ( text == QStringLiteral ( "null" ) )  return JsonNode::make_null ();
		if ( text == QStringLiteral ( "true" ) )  return JsonNode::make_boolean ( true );
		if ( text == QStringLiteral ( "false" ) ) return JsonNode::make_boolean ( false );

		if ( is_json_integer ( text ) )
		{
			return JsonNode::make_number ( text );
		}

		return JsonNode::make_string ( text );
	}

	void IXmlImportStrategy::append_child_members ( JsonNode& object, const XmlElement& element, bool inferScalars ) const
	{
		// Child element names in first-encountered order; a name seen more than once collapses to an array.

		QStringList order;

		for ( const XmlElement& child : element.children )
		{
			if ( !order.contains ( child.name ) )
			{
				order.append ( child.name );
			}
		}

		for ( const QString& name : order )
		{
			std::vector<const XmlElement*> group;

			for ( const XmlElement& child : element.children )
			{
				if ( child.name == name )
				{
					group.push_back ( &child );
				}
			}

			if ( group.size () == 1 )
			{
				object.append_member ( name, convert ( *group.front (), inferScalars ) );
			}
			else
			{
				std::unique_ptr<JsonNode> array = JsonNode::make_array ();

				for ( const XmlElement* child : group )
				{
					array->append_element ( convert ( *child, inferScalars ) );
				}

				object.append_member ( name, std::move ( array ) );
			}
		}
	}

	bool IXmlImportStrategy::is_leaf ( const XmlElement& element )
	{
		return element.attributes.empty () && element.children.empty ();
	}

	//*****************************************************************************************************************
	// XmlImporter
	//*****************************************************************************************************************

	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// Read the element the reader is positioned on (a StartElement just consumed) up to its matching EndElement,
		// recursing into child elements. Whitespace-only character runs (element indentation) are skipped; the rest
		// is concatenated and trimmed. Mixed content (text interleaved with children) degrades to text + children.
		//-------------------------------------------------------------------------------------------------------------

		XmlElement read_element ( QXmlStreamReader& reader )
		{
			XmlElement element;
			element.name = reader.name ().toString ();

			const QXmlStreamAttributes attributes = reader.attributes ();

			for ( const QXmlStreamAttribute& attribute : attributes )
			{
				element.attributes.push_back ( { attribute.name ().toString (), attribute.value ().toString () } );
			}

			QString textAccumulator;

			while ( !reader.atEnd () )
			{
				const QXmlStreamReader::TokenType token = reader.readNext ();

				if ( token == QXmlStreamReader::StartElement )
				{
					element.children.push_back ( read_element ( reader ) );
				}
				else if ( token == QXmlStreamReader::Characters )
				{
					if ( !reader.isWhitespace () )
					{
						textAccumulator += reader.text ();
					}
				}
				else if ( token == QXmlStreamReader::EndElement )
				{
					break;
				}
			}

			element.text = textAccumulator.trimmed ();

			return element;
		}
	}

	bool XmlImporter::parse_root ( const QString& xml, XmlElement& outRoot, QString& outError )
	{
		QXmlStreamReader reader ( xml );

		while ( !reader.atEnd () )
		{
			if ( reader.readNext () == QXmlStreamReader::StartElement )
			{
				outRoot = read_element ( reader );

				if ( reader.hasError () )
				{
					outError = QStringLiteral ( "%1 (line %2, column %3)." )
					               .arg ( reader.errorString () )
					               .arg ( reader.lineNumber () )
					               .arg ( reader.columnNumber () );
					return false;
				}

				return true;
			}
		}

		if ( reader.hasError () )
		{
			outError = QStringLiteral ( "%1 (line %2, column %3)." )
			               .arg ( reader.errorString () )
			               .arg ( reader.lineNumber () )
			               .arg ( reader.columnNumber () );
		}
		else
		{
			outError = QStringLiteral ( "The XML document has no root element." );
		}

		return false;
	}

	std::unique_ptr<IXmlImportStrategy> XmlImporter::make_strategy ( XmlImportStrategyKind kind, const QString& textValueKey )
	{
		switch ( kind )
		{
			case XmlImportStrategyKind::BadgerFish:          return std::make_unique<BadgerFishStrategy> ();
			case XmlImportStrategyKind::DirectAttributeKeys: return std::make_unique<DirectAttributeKeysStrategy> ();
			case XmlImportStrategyKind::GroupedAttributes:   return std::make_unique<GroupedAttributesStrategy> ();
			case XmlImportStrategyKind::CustomFlattened:     return std::make_unique<CustomFlattenedStrategy> ( textValueKey );
		}

		return std::make_unique<DirectAttributeKeysStrategy> ();       // Unreachable; keeps the compiler happy.
	}

	XmlImporter::Result XmlImporter::import_text ( const QString& xml, const IXmlImportStrategy& strategy, bool inferScalars )
	{
		Result     result;
		XmlElement rootElement;

		if ( !parse_root ( xml, rootElement, result.error ) )
		{
			return result;
		}

		// The document is a single-member object keyed by the XML root element's name (the worked example).

		std::unique_ptr<JsonNode> root = JsonNode::make_object ();
		root->append_member ( rootElement.name, strategy.convert ( rootElement, inferScalars ) );

		result.root = std::move ( root );
		result.ok   = true;

		return result;
	}
}
