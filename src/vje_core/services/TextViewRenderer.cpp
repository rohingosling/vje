//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   TextViewRenderer implementation. See TextViewRenderer.hpp for the EDITOR-06 / SET-06 rendering rules; the eight
//   table styles reproduce the spec's section 2.10 examples exactly.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/services/TextViewRenderer.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QStringList>

#include <algorithm>

namespace vje
{
	namespace
	{
		//=============================================================================================================
		// Box-drawing glyphs (U+2500 block) used by the bordered table styles.
		//=============================================================================================================

		const QChar HORIZONTAL      ( 0x2500 );   // -
		const QChar VERTICAL        ( 0x2502 );   // |
		const QChar CORNER_TOP_LEFT ( 0x250C );   // +
		const QChar CORNER_TOP_RIGHT( 0x2510 );
		const QChar CORNER_BOT_LEFT ( 0x2514 );
		const QChar CORNER_BOT_RIGHT( 0x2518 );
		const QChar TEE_LEFT        ( 0x251C );   // |-
		const QChar TEE_RIGHT       ( 0x2524 );   // -|
		const QChar TEE_TOP         ( 0x252C );   // T
		const QChar TEE_BOTTOM      ( 0x2534 );   // _|_ inverted
		const QChar CROSS           ( 0x253C );   // +

		//=============================================================================================================
		// Small string helpers
		//=============================================================================================================

		QString spaces ( int count )
		{
			return QString ( std::max ( 0, count ), QLatin1Char ( ' ' ) );
		}

		QString pad_right ( const QString& text, int width )
		{
			return text + spaces ( width - static_cast<int> ( text.length () ) );
		}

		QString rule_of ( QChar glyph, int count )
		{
			return QString ( std::max ( 0, count ), glyph );
		}

		// The textual form shown in a cell / value column: scalars as their plain text, containers as {...} / [...].

		QString cell_text ( const JsonNode& node )
		{
			switch ( node.kind () )
			{
				case JsonKind::Null:    return QStringLiteral ( "null" );
				case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
				case JsonKind::Number:  return node.number_token ();
				case JsonKind::String:  return node.string_value ();
				case JsonKind::Array:   return QStringLiteral ( "[...]" );
				case JsonKind::Object:  return QStringLiteral ( "{...}" );
			}

			return QString ();
		}

		//=============================================================================================================
		// The table model shared by the eight styles: an optional header row and a grid of already-stringified cells.
		//=============================================================================================================

		struct TableGrid
		{
			QStringList              headers;   // Empty => no header row (a scalar / single-column array).
			std::vector<QStringList> rows;
			int                      columns = 0;

			bool has_header () const { return !headers.isEmpty (); }

			std::vector<int> column_widths () const
			{
				std::vector<int> widths ( columns, 0 );

				for ( int column = 0; column < columns; ++column )
				{
					if ( has_header () )
					{
						widths [ column ] = static_cast<int> ( headers [ column ].length () );
					}

					for ( const QStringList& row : rows )
					{
						widths [ column ] = std::max ( widths [ column ], static_cast<int> ( row [ column ].length () ) );
					}
				}

				return widths;
			}
		};

		// Build the grid for an array selection: one column per union key (array of objects) or one unheadered
		// column of values (scalar / mixed array).

		TableGrid build_grid ( const JsonNode& array )
		{
			TableGrid grid;

			const int size = array.array_size ();

			bool allObjects = size > 0;

			for ( int index = 0; index < size; ++index )
			{
				if ( array.array_element ( index )->kind () != JsonKind::Object )
				{
					allObjects = false;
					break;
				}
			}

			if ( allObjects )
			{
				// Columns = the union of member keys in first-encountered order (EDITOR-03).

				for ( int index = 0; index < size; ++index )
				{
					const JsonNode* element = array.array_element ( index );

					for ( int member = 0; member < element->member_count (); ++member )
					{
						const QString& key = element->member_key ( member );

						if ( !grid.headers.contains ( key ) )
						{
							grid.headers.append ( key );
						}
					}
				}

				grid.columns = static_cast<int> ( grid.headers.length () );

				for ( int index = 0; index < size; ++index )
				{
					const JsonNode* element = array.array_element ( index );
					QStringList     row;

					for ( const QString& key : grid.headers )
					{
						const JsonNode* value = element->find_member ( key );
						row << ( ( value != nullptr ) ? cell_text ( *value ) : QString () );   // Missing => empty cell.
					}

					grid.rows.push_back ( row );
				}
			}
			else
			{
				// Single unheadered column of element values.

				grid.columns = 1;

				for ( int index = 0; index < size; ++index )
				{
					grid.rows.push_back ( QStringList { cell_text ( *array.array_element ( index ) ) } );
				}
			}

			return grid;
		}

		//=============================================================================================================
		// Row assembly used by several styles
		//=============================================================================================================

		// Cells padded to their column width and joined by a two-space gap (Academic / Minimal), trailing space
		// trimmed off the line end.

		QString gap_joined ( const QStringList& cells, const std::vector<int>& widths )
		{
			QString line;

			for ( int column = 0; column < cells.length (); ++column )
			{
				if ( column > 0 )
				{
					line += QStringLiteral ( "  " );
				}

				line += pad_right ( cells [ column ], widths [ column ] );
			}

			while ( line.endsWith ( QLatin1Char ( ' ' ) ) )
			{
				line.chop ( 1 );
			}

			return line;
		}

		// The full-width padded row body used inside a Compact border (cells joined by two spaces, NOT trimmed).

		QString full_row ( const QStringList& cells, const std::vector<int>& widths )
		{
			QString line;

			for ( int column = 0; column < cells.length (); ++column )
			{
				if ( column > 0 )
				{
					line += QStringLiteral ( "  " );
				}

				line += pad_right ( cells [ column ], widths [ column ] );
			}

			return line;
		}

		// A boxed content line: "| cell | cell | ... |" with a one-space margin inside each cell (Columnar /
		// Spreadsheet / Markdown share the shape; the border glyph and dashes vary).

		QString boxed_line ( const QStringList& cells, const std::vector<int>& widths, QChar border )
		{
			QString line ( border );

			for ( int column = 0; column < cells.length (); ++column )
			{
				line += QLatin1Char ( ' ' );
				line += pad_right ( cells [ column ], widths [ column ] );
				line += QLatin1Char ( ' ' );
				line += border;
			}

			return line;
		}

		// A segmented rule "L---J---R" for the box styles (widths + 2 dashes per column, joined by the mid glyph).

		QString segmented_rule ( const std::vector<int>& widths, QChar left, QChar mid, QChar right )
		{
			QString line ( left );

			for ( std::size_t column = 0; column < widths.size (); ++column )
			{
				if ( column > 0 )
				{
					line += mid;
				}

				line += rule_of ( HORIZONTAL, widths [ column ] + 2 );
			}

			line += right;
			return line;
		}

		int total_width ( const std::vector<int>& widths )
		{
			int sum = 0;

			for ( const int width : widths )
			{
				sum += width;
			}

			return sum + 2 * std::max ( 0, static_cast<int> ( widths.size () ) - 1 );   // Two-space gaps between columns.
		}

		//=============================================================================================================
		// CSV / TSV field handling
		//=============================================================================================================

		QString csv_field ( const QString& value )
		{
			const bool mustQuote = value.contains ( QLatin1Char ( ',' ) )
			                    || value.contains ( QLatin1Char ( '"' ) )
			                    || value.contains ( QLatin1Char ( '\n' ) )
			                    || value.contains ( QLatin1Char ( '\r' ) );

			if ( !mustQuote )
			{
				return value;
			}

			QString escaped = value;
			escaped.replace ( QLatin1Char ( '"' ), QStringLiteral ( "\"\"" ) );

			return QLatin1Char ( '"' ) + escaped + QLatin1Char ( '"' );
		}

		//=============================================================================================================
		// The eight table styles
		//=============================================================================================================

		QString render_table ( const TableGrid& grid, TableStyle style )
		{
			if ( grid.columns == 0 )
			{
				return QString ();                         // An empty array has no columns to show.
			}

			const std::vector<int> widths = grid.column_widths ();
			QStringList            lines;

			switch ( style )
			{
				case TableStyle::Academic:
				{
					const QString rule = rule_of ( HORIZONTAL, total_width ( widths ) );

					lines << rule;

					if ( grid.has_header () )
					{
						lines << gap_joined ( grid.headers, widths );
						lines << rule;
					}

					for ( const QStringList& row : grid.rows )
					{
						lines << gap_joined ( row, widths );
					}

					lines << rule;
					break;
				}

				case TableStyle::Compact:
				{
					const int     inner = total_width ( widths ) + 2;   // One-space margin each side.
					const QString top   = CORNER_TOP_LEFT + rule_of ( HORIZONTAL, inner ) + CORNER_TOP_RIGHT;
					const QString mid   = TEE_LEFT        + rule_of ( HORIZONTAL, inner ) + TEE_RIGHT;
					const QString bot   = CORNER_BOT_LEFT + rule_of ( HORIZONTAL, inner ) + CORNER_BOT_RIGHT;

					lines << top;

					if ( grid.has_header () )
					{
						lines << VERTICAL + QLatin1Char ( ' ' ) + full_row ( grid.headers, widths ) + QLatin1Char ( ' ' ) + VERTICAL;
						lines << mid;
					}

					for ( const QStringList& row : grid.rows )
					{
						lines << VERTICAL + QLatin1Char ( ' ' ) + full_row ( row, widths ) + QLatin1Char ( ' ' ) + VERTICAL;
					}

					lines << bot;
					break;
				}

				case TableStyle::Columnar:
				{
					lines << segmented_rule ( widths, CORNER_TOP_LEFT, TEE_TOP, CORNER_TOP_RIGHT );

					if ( grid.has_header () )
					{
						lines << boxed_line ( grid.headers, widths, VERTICAL );
						lines << segmented_rule ( widths, TEE_LEFT, CROSS, TEE_RIGHT );
					}

					for ( const QStringList& row : grid.rows )
					{
						lines << boxed_line ( row, widths, VERTICAL );
					}

					lines << segmented_rule ( widths, CORNER_BOT_LEFT, TEE_BOTTOM, CORNER_BOT_RIGHT );
					break;
				}

				case TableStyle::Spreadsheet:
				{
					const QString mid = segmented_rule ( widths, TEE_LEFT, CROSS, TEE_RIGHT );

					lines << segmented_rule ( widths, CORNER_TOP_LEFT, TEE_TOP, CORNER_TOP_RIGHT );

					bool first = true;

					if ( grid.has_header () )
					{
						lines << boxed_line ( grid.headers, widths, VERTICAL );
						first = false;
					}

					for ( const QStringList& row : grid.rows )
					{
						if ( !first )
						{
							lines << mid;
						}

						lines << boxed_line ( row, widths, VERTICAL );
						first = false;
					}

					lines << segmented_rule ( widths, CORNER_BOT_LEFT, TEE_BOTTOM, CORNER_BOT_RIGHT );
					break;
				}

				case TableStyle::Minimal:
				{
					if ( grid.has_header () )
					{
						lines << gap_joined ( grid.headers, widths );
						lines << QString ();               // Blank line under the header.
					}

					for ( const QStringList& row : grid.rows )
					{
						lines << gap_joined ( row, widths );
					}

					break;
				}

				case TableStyle::Markdown:
				{
					// Markdown needs a header row; a headerless single column uses one empty header cell.

					const QStringList headers = grid.has_header () ? grid.headers : QStringList { QString () };

					lines << boxed_line ( headers, widths, QLatin1Char ( '|' ) );

					QStringList dashRow;

					for ( const int width : widths )
					{
						dashRow << rule_of ( QLatin1Char ( '-' ), width );
					}

					lines << boxed_line ( dashRow, widths, QLatin1Char ( '|' ) );

					for ( const QStringList& row : grid.rows )
					{
						lines << boxed_line ( row, widths, QLatin1Char ( '|' ) );
					}

					break;
				}

				case TableStyle::Csv:
				{
					if ( grid.has_header () )
					{
						QStringList fields;
						for ( const QString& header : grid.headers ) { fields << csv_field ( header ); }
						lines << fields.join ( QLatin1Char ( ',' ) );
					}

					for ( const QStringList& row : grid.rows )
					{
						QStringList fields;
						for ( const QString& cell : row ) { fields << csv_field ( cell ); }
						lines << fields.join ( QLatin1Char ( ',' ) );
					}

					break;
				}

				case TableStyle::Tsv:
				{
					if ( grid.has_header () )
					{
						lines << grid.headers.join ( QLatin1Char ( '\t' ) );
					}

					for ( const QStringList& row : grid.rows )
					{
						lines << row.join ( QLatin1Char ( '\t' ) );
					}

					break;
				}
			}

			return lines.join ( QLatin1Char ( '\n' ) );
		}

		//=============================================================================================================
		// Object (key-value listing) rendering
		//=============================================================================================================

		// The (key, value-text) rows of an object, honouring the include-object / include-array filters.

		std::vector<std::pair<QString, QString>> object_rows ( const JsonNode& object, const TextViewProfile& profile )
		{
			std::vector<std::pair<QString, QString>> rows;

			for ( int index = 0; index < object.member_count (); ++index )
			{
				const JsonNode* value = object.member_value ( index );

				if ( ( value->kind () == JsonKind::Object ) && !profile.includeObjectNames )
				{
					continue;
				}

				if ( ( value->kind () == JsonKind::Array ) && !profile.includeArrayNames )
				{
					continue;
				}

				rows.emplace_back ( object.member_key ( index ), cell_text ( *value ) );
			}

			return rows;
		}

		QString render_object ( const JsonNode& object, const TextViewProfile& profile )
		{
			const std::vector<std::pair<QString, QString>> rows = object_rows ( object, profile );

			QStringList lines;

			switch ( profile.markdownListStyle )
			{
				case MarkdownListStyle::List:
				{
					for ( const auto& [ key, value ] : rows )
					{
						lines << QStringLiteral ( "- **" ) + key + QStringLiteral ( "**: " ) + value;
					}

					break;
				}

				case MarkdownListStyle::Table:
				{
					int keyWidth   = static_cast<int> ( QStringLiteral ( "Key" ).length () );
					int valueWidth = static_cast<int> ( QStringLiteral ( "Value" ).length () );

					for ( const auto& [ key, value ] : rows )
					{
						keyWidth   = std::max ( keyWidth,   static_cast<int> ( key.length () ) );
						valueWidth = std::max ( valueWidth, static_cast<int> ( value.length () ) );
					}

					const auto pipe = [ & ] ( const QString& key, const QString& value )
					{
						return QStringLiteral ( "| " ) + pad_right ( key, keyWidth )
						     + QStringLiteral ( " | " ) + pad_right ( value, valueWidth ) + QStringLiteral ( " |" );
					};

					lines << pipe ( QStringLiteral ( "Key" ), QStringLiteral ( "Value" ) );
					lines << QStringLiteral ( "| " ) + rule_of ( QLatin1Char ( '-' ), keyWidth )
					       + QStringLiteral ( " | " ) + rule_of ( QLatin1Char ( '-' ), valueWidth ) + QStringLiteral ( " |" );

					for ( const auto& [ key, value ] : rows )
					{
						lines << pipe ( key, value );
					}

					break;
				}

				case MarkdownListStyle::None:
				{
					int keyWidth = 0;

					if ( profile.alignNameSeparators )
					{
						for ( const auto& [ key, value ] : rows )
						{
							keyWidth = std::max ( keyWidth, static_cast<int> ( key.length () ) );
						}
					}

					for ( const auto& [ key, value ] : rows )
					{
						lines << pad_right ( key, keyWidth ) + QLatin1Char ( ' ' )
						       + profile.nameSeparator + QLatin1Char ( ' ' ) + value;
					}

					break;
				}
			}

			return lines.join ( QLatin1Char ( '\n' ) );
		}
	}

	//=================================================================================================================
	// Public Interface
	//=================================================================================================================

	QString TextViewRenderer::render ( const JsonNode& node, const TextViewProfile& profile )
	{
		if ( node.kind () == JsonKind::Array )
		{
			return render_table ( build_grid ( node ), profile.tableStyle );
		}

		if ( node.kind () == JsonKind::Object )
		{
			return render_object ( node, profile );
		}

		return cell_text ( node );                         // A scalar selection: its textual form on one line.
	}
}
