//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   XmlExporter implementation. A straight recursive walk emitting indented elements; hand-rolled (rather than
//   QXmlStreamWriter) so the exact bytes -- declaration, indentation, empty-element form -- are under our control and
//   match the goldens.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/convert/XmlExporter.hpp>
#include <vje_core/document/JsonNode.hpp>

namespace vje
{
	namespace
	{
		constexpr int INDENT_WIDTH = 2;                            // Spaces per nesting level.
	}

	//-----------------------------------------------------------------------------------------------------------------
	// export_document -- declaration, then the <document> root, then a single trailing newline.
	//-----------------------------------------------------------------------------------------------------------------

	QString XmlExporter::export_document ( const JsonNode& root )
	{
		QString out = QStringLiteral ( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" );

		write_node ( out, QStringLiteral ( "document" ), root, 0 );

		return out;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// write_node -- a scalar (or empty container) on one line; a non-empty container across nested lines.
	//-----------------------------------------------------------------------------------------------------------------

	void XmlExporter::write_node ( QString& out, const QString& name, const JsonNode& node, int level )
	{
		const QString indent = QString ( level * INDENT_WIDTH, QLatin1Char ( ' ' ) );
		const QString open   = QStringLiteral ( "<" )  + name + QStringLiteral ( ">" );
		const QString close  = QStringLiteral ( "</" ) + name + QStringLiteral ( ">" );

		if ( node.is_scalar () )
		{
			out += indent + open + escape_text ( scalar_text ( node ) ) + close + QLatin1Char ( '\n' );
			return;
		}

		if ( node.kind () == JsonKind::Object )
		{
			if ( node.member_count () == 0 )
			{
				out += indent + open + close + QLatin1Char ( '\n' );
				return;
			}

			out += indent + open + QLatin1Char ( '\n' );

			for ( int memberIndex = 0; memberIndex < node.member_count (); ++memberIndex )
			{
				write_node ( out, node.member_key ( memberIndex ), *node.member_value ( memberIndex ), level + 1 );
			}

			out += indent + close + QLatin1Char ( '\n' );
			return;
		}

		// Array.

		if ( node.array_size () == 0 )
		{
			out += indent + open + close + QLatin1Char ( '\n' );
			return;
		}

		out += indent + open + QLatin1Char ( '\n' );

		for ( int elementIndex = 0; elementIndex < node.array_size (); ++elementIndex )
		{
			write_node ( out, QStringLiteral ( "item" ), *node.array_element ( elementIndex ), level + 1 );
		}

		out += indent + close + QLatin1Char ( '\n' );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// escape_text -- the three mandatory character-data escapes.
	//-----------------------------------------------------------------------------------------------------------------

	QString XmlExporter::escape_text ( const QString& text )
	{
		QString escaped = text;

		escaped.replace ( QLatin1Char ( '&' ), QStringLiteral ( "&amp;" ) );   // Ampersand first, before the others introduce '&'.
		escaped.replace ( QLatin1Char ( '<' ), QStringLiteral ( "&lt;" ) );
		escaped.replace ( QLatin1Char ( '>' ), QStringLiteral ( "&gt;" ) );

		return escaped;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// scalar_text -- the text content of a scalar element (null -> empty).
	//-----------------------------------------------------------------------------------------------------------------

	QString XmlExporter::scalar_text ( const JsonNode& node )
	{
		switch ( node.kind () )
		{
			case JsonKind::Number:  return node.number_token ();
			case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
			case JsonKind::String:  return node.string_value ();
			default:                return QString ();             // Null -> an empty element.
		}
	}
}
