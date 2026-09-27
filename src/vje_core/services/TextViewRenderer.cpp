//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
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
#include <vje_core/services/value_placeholders.hpp>

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

		// Trailing whitespace never survives into the rendering: the Text View exists to be copied out of, and a line
		// that ends in spaces shows up in whatever it is pasted into.

		QString right_trimmed ( const QString& text )
		{
			int end = static_cast<int> ( text.size () );

			while ( ( end > 0 ) && ( text [ end - 1 ] == QLatin1Char ( ' ' ) ) )
			{
				--end;
			}

			return text.left ( end );
		}

		QString rule_of ( QChar glyph, int count )
		{
			return QString ( std::max ( 0, count ), glyph );
		}

		// The textual form shown in a cell / value column: scalars as their plain text, containers as {...} / [...].
		//
		// A STRING passes through the SET-03 display mode, and this is the only place it does -- the table grid, the
		// key-value listing and a lone scalar selection all arrive here, so the three cannot disagree about whether a
		// tab is a tab, a "\t", or nothing at all.

		QString cell_text ( const JsonNode& node, StringDisplay stringDisplay )
		{
			switch ( node.kind () )
			{
				case JsonKind::Null:    return QStringLiteral ( "null" );
				case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
				case JsonKind::Number:  return node.number_token ();
				case JsonKind::String:  return string_display_text ( node.string_value (), stringDisplay );
				case JsonKind::Array:   return value_placeholders::ARRAY;
				case JsonKind::Object:  return value_placeholders::OBJECT;
			}

			return QString ();
		}

		//-------------------------------------------------------------------------------------------------------------
		// Word wrap with a HANGING INDENT: the first line carries the caller's prefix and every continuation lines up
		// under where the value started. That indent is the whole reason wrapping is done here rather than left to the
		// widget -- QPlainTextEdit's own word wrap breaks to column 0 (architecture.md section 4.6).
		//
		// Returns the value's lines WITHOUT the prefix, so the caller composes the first line as it already did. Any
		// line breaks already in the value are honoured first and each resulting line wrapped independently, which is
		// what makes Decoded mode and wrapping compose rather than fight.
		//
		// THE TWO INDENTS ARE SEPARATE, and conflating them was a real defect (2026-07-28 review). The key-value
		// listing indents continuations to the full prefix, so both are the same there. The Markdown list does not: its
		// first line carries "- **key**: " while its continuations carry two spaces, because two is what Markdown reads
		// as a continuation of the item and more would start a code block. Measuring every line by the bullet left the
		// continuations short by the length of the key, and a long key drove the budget under MINIMUM_AVAILABLE and
		// silently disabled wrapping for that entry; measuring every line by the two-space indent would instead have
		// let the FIRST line overrun by the width of the bullet.
		//-------------------------------------------------------------------------------------------------------------

		QStringList wrap_value ( const QString& value, int firstIndentWidth, int continuationIndentWidth, int wrapColumns )
		{
			// Off, or an indent has eaten so much of the line that wrapping would produce a column of syllables. Below
			// this the honest answer is to leave the line long. Tested against the WIDER indent, so the decision is the
			// same for every line of the value.

			constexpr int MINIMUM_AVAILABLE = 8;

			// Tested against the CONTINUATION indent, because that is the one governing every line after the first. A
			// long Markdown key made the BULLET exceed the width and disabled wrapping for the whole entry, even though
			// the two-space continuations had ample room -- so the entry ran off the edge instead (2026-07-28 review).

			if ( ( wrapColumns <= 0 ) || ( ( wrapColumns - continuationIndentWidth ) < MINIMUM_AVAILABLE ) )
			{
				return { value };
			}

			QStringList wrapped;

			for ( const QString& paragraph : value.split ( QLatin1Char ( '\n' ) ) )
			{
				int start = 0;

				do
				{
					// Recomputed per emitted line: only the very first line of the whole value carries the caller's
					// prefix, so every line after it -- including the first line of a later paragraph -- is a
					// continuation.

					const int indent    = wrapped.isEmpty () ? firstIndentWidth : continuationIndentWidth;
					const int available = std::max ( MINIMUM_AVAILABLE, wrapColumns - indent );

					if ( paragraph.size () - start <= available )
					{
						wrapped << paragraph.mid ( start );

						break;
					}

					// Prefer the last space that still fits. A single word longer than the line has none, and is broken
					// at the margin rather than allowed to overrun it -- a URL must not push the whole column sideways.

					int breakAt = paragraph.lastIndexOf ( QLatin1Char ( ' ' ), start + available );

					if ( breakAt <= start )
					{
						breakAt = start + available;

						wrapped << paragraph.mid ( start, available );
					}
					else
					{
						// Right-trimmed, because breaking at the LAST space that fits leaves any run of spaces before
						// it on the emitted line -- "aaa  bbb" broken at the second space emits "aaa ". Real prose hits
						// that routinely (a double space after a full stop, anything pasted), and this file is careful
						// elsewhere to emit truly empty blank lines, so trailing whitespace here would be the same
						// discipline broken in the one place it was not stated (2026-07-28 review).

						QStringView line = QStringView { paragraph }.sliced ( start, breakAt - start );

						while ( !line.isEmpty () && line.endsWith ( QLatin1Char ( ' ' ) ) )
						{
							line.chop ( 1 );
						}

						wrapped << line.toString ();

						++breakAt;                                 // Consume the space the break was taken at.
					}

					start = breakAt;
				}
				while ( start < paragraph.size () );

				// An EMPTY paragraph is already handled by the loop above: `0 - 0 <= available` appends one empty line
				// and breaks. Appending another here doubled every empty line -- an empty value rendered as the key
				// prefix plus a line of pure indent whitespace, and a value containing a blank line rendered with two
				// (2026-07-28 review). The loop runs at least once for every paragraph, so no case needs a fallback.
			}

			return wrapped;
		}

		// The composed rows for one wrapped entry: prefix + first line, then the rest under a hanging indent.

		void append_wrapped ( QStringList& lines, const QString& prefix, const QString& value, int wrapColumns )
		{
			const int         indentWidth = static_cast<int> ( prefix.length () );
			const QStringList valueLines  = wrap_value ( value, indentWidth, indentWidth, wrapColumns );

			// Right-trimmed, both here and below. A line that ends in whitespace survives into whatever the user pastes
			// the rendering into, and an EMPTY continuation padded to the hanging indent is a line of pure whitespace
			// where the reader sees a blank (2026-07-28 review). For a non-empty value both trims are no-ops, since
			// wrap_value already trims its own break points.

			lines << right_trimmed ( prefix + valueLines.first () );

			for ( int index = 1; index < valueLines.size (); ++index )
			{
				lines << right_trimmed ( spaces ( indentWidth ) + valueLines [ index ] );
			}
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

		TableGrid build_grid ( const JsonNode& array, StringDisplay stringDisplay )
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
						row << ( ( value != nullptr ) ? cell_text ( *value, stringDisplay ) : QString () );   // Missing => empty cell.
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
					grid.rows.push_back ( QStringList { cell_text ( *array.array_element ( index ), stringDisplay ) } );
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

				rows.emplace_back ( object.member_key ( index ), cell_text ( *value, profile.stringDisplay ) );
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
						// Continuations indent under the bullet's TEXT rather than under the value -- two spaces is what
						// Markdown reads as a continuation of the item, and any deeper indent would start a code block.

						const QString bullet = QStringLiteral ( "- **" ) + key + QStringLiteral ( "**: " );

						if ( profile.wrapColumns <= 0 )
						{
							lines << bullet + value;

							continue;
						}

						constexpr int MARKDOWN_CONTINUATION_INDENT = 2;

						// Wrapped against the CONTINUATION indent, not the bullet's full width. Measuring by the bullet
						// asked for a width the continuations never use, so every line after the first stopped short by
						// the length of the key -- visibly ragged beside the key-value listing rendered from the same
						// column -- and a long key pushed the available width under MINIMUM_AVAILABLE, which disabled
						// wrapping for that entry entirely (2026-07-28 review).
						//
						// The first line is shorter than the rest by design here: it carries the bullet, and Markdown
						// does not care. Passing the first line's own budget separately would be a second parameter for
						// two columns of raggedness at the top of an item.

						const QStringList valueLines = wrap_value
						(
							value,
							static_cast<int> ( bullet.length () ),
							MARKDOWN_CONTINUATION_INDENT,
							profile.wrapColumns
						);

						lines << bullet + valueLines.first ();

						for ( int index = 1; index < valueLines.size (); ++index )
						{
							lines << spaces ( MARKDOWN_CONTINUATION_INDENT ) + valueLines [ index ];
						}
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
						// SET-06's blank lines, inserted BETWEEN entries: never before the first, which is also what
						// leaves no trailing blank at the end. An empty string here IS a blank line once the list is
						// joined, and it is a TRULY empty one -- trailing spaces would survive into whatever the user
						// pastes this into.
						//
						// Placed per ENTRY rather than per line, which is what makes it compose with wrapping: the gap
						// falls between one field and the next, never between a field's own wrapped lines.

						if ( !lines.isEmpty () )
						{
							for ( int blank = 0; blank < profile.blankLinesBetweenFields; ++blank )
							{
								lines << QString ();
							}
						}

						// The prefix IS the hanging indent: everything left of the value, so a continuation line starts
						// exactly under where the value started (SET-06). With wrapColumns 0 this is the original one-liner.

						append_wrapped
						(
							lines,
							pad_right ( key, keyWidth ) + QLatin1Char ( ' ' ) + profile.nameSeparator + QLatin1Char ( ' ' ),
							value,
							profile.wrapColumns
						);
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
			return render_table ( build_grid ( node, profile.stringDisplay ), profile.tableStyle );
		}

		if ( node.kind () == JsonKind::Object )
		{
			return render_object ( node, profile );
		}

		return cell_text ( node, profile.stringDisplay );   // A scalar selection: its textual form on one line.
	}

	bool TextViewRenderer::wraps_long_values ( const JsonNode& node, const TextViewProfile& profile )
	{
		// Deliberately mirrors render()'s dispatch above, branch for branch, so the two are read together and a new
		// rendering cannot be added to one without the omission being obvious in the other.

		if ( node.kind () == JsonKind::Array )
		{
			// Every one of the eight table styles is out: CSV and TSV are machine formats, the Markdown table is
			// source, and the five box styles draw borders that a wrap would break.

			return false;
		}

		if ( node.kind () == JsonKind::Object )
		{
			// The key-value listing and the Markdown LIST both wrap; the Markdown Key/Value TABLE is a table like any
			// other.

			return profile.markdownListStyle != MarkdownListStyle::Table;
		}

		// A scalar renders as one line with no prefix, so there is no hanging indent to lose -- breaking it at the page
		// width costs nothing and clipping it would cost the value.

		return true;
	}
}
