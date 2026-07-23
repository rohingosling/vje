//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   CsvCodec implementation. Export builds the column set (union of keys, first-encountered order), emits a header
//   row and one record per element, quoting fields per RFC 4180 and separating records with CRLF (no trailing CRLF --
//   the last record's line break is optional per the RFC, and omitting it keeps round-trips exact). Import runs a
//   character state machine that honours quoted fields, doubled quotes, and embedded separators/newlines, then maps
//   the header row to keys and infers each cell's JSON type.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_core/convert/CsvCodec.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/editing/edit_transforms.hpp>

#include <QStringList>

#include <vector>

namespace vje
{
	namespace
	{
		//-------------------------------------------------------------------------------------------------------------
		// RFC 4180 field quoting: wrap in double-quotes when the field contains a comma, double-quote, CR, or LF, and
		// double any embedded quote. Other fields pass through verbatim.
		//-------------------------------------------------------------------------------------------------------------

		QString csv_field ( const QString& text )
		{
			const bool mustQuote = text.contains ( QLatin1Char ( ',' ) )
			                    || text.contains ( QLatin1Char ( '"' ) )
			                    || text.contains ( QLatin1Char ( '\r' ) )
			                    || text.contains ( QLatin1Char ( '\n' ) );

			if ( !mustQuote )
			{
				return text;
			}

			QString escaped = text;
			escaped.replace ( QLatin1Char ( '"' ), QStringLiteral ( "\"\"" ) );

			return QLatin1Char ( '"' ) + escaped + QLatin1Char ( '"' );
		}

		//-------------------------------------------------------------------------------------------------------------
		// The exported text of a single scalar cell: null -> the literal `null`, boolean -> true/false, number -> its
		// raw token, string -> its content (quoting is applied by csv_field). A missing member is handled by the
		// caller (an empty cell).
		//-------------------------------------------------------------------------------------------------------------

		QString scalar_cell_text ( const JsonNode& node )
		{
			switch ( node.kind () )
			{
				case JsonKind::Null:    return QStringLiteral ( "null" );
				case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
				case JsonKind::Number:  return node.number_token ();
				case JsonKind::String:  return node.string_value ();
				default:                return QString ();                  // Containers never reach here (export validates flatness).
			}
		}

		//-------------------------------------------------------------------------------------------------------------
		// Infer a cell's JSON value from its text: null / true / false / a JSON number where unambiguous, else a string
		// (an empty cell is an empty string). CSV carries no types, so this is the documented lossy convention.
		//-------------------------------------------------------------------------------------------------------------

		std::unique_ptr<JsonNode> infer_cell ( const QString& text )
		{
			if ( text == QStringLiteral ( "null" ) )  return JsonNode::make_null ();
			if ( text == QStringLiteral ( "true" ) )  return JsonNode::make_boolean ( true );
			if ( text == QStringLiteral ( "false" ) ) return JsonNode::make_boolean ( false );

			if ( edit_transforms::is_json_number ( text ) )
			{
				return JsonNode::make_number ( text );
			}

			return JsonNode::make_string ( text );
		}

		//-------------------------------------------------------------------------------------------------------------
		// Parse CSV text into records of fields. Tolerates LF or CRLF record breaks, quoted fields, doubled quotes,
		// and separators/newlines embedded in quoted fields. A trailing record break yields no spurious empty record
		// (the final record is flushed only when a field was actually started).
		//-------------------------------------------------------------------------------------------------------------

		std::vector<QStringList> parse_records ( const QString& text )
		{
			std::vector<QStringList> records;
			QStringList              record;
			QString                  field;
			bool                     inQuotes    = false;
			bool                     fieldStarted = false;

			const int length = text.length ();
			int       index  = 0;

			auto end_record = [ & ] ()
			{
				record.append ( field );
				records.push_back ( record );
				record.clear ();
				field.clear ();
				fieldStarted = false;
			};

			while ( index < length )
			{
				const QChar character = text.at ( index );

				if ( inQuotes )
				{
					if ( character == QLatin1Char ( '"' ) )
					{
						if ( index + 1 < length && text.at ( index + 1 ) == QLatin1Char ( '"' ) )
						{
							field.append ( QLatin1Char ( '"' ) );        // Doubled quote -> a literal quote.
							index += 2;
						}
						else
						{
							inQuotes = false;                            // Closing quote.
							index += 1;
						}
					}
					else
					{
						field.append ( character );
						index += 1;
					}

					continue;
				}

				if ( character == QLatin1Char ( '"' ) )
				{
					inQuotes     = true;
					fieldStarted = true;
					index       += 1;
				}
				else if ( character == QLatin1Char ( ',' ) )
				{
					record.append ( field );
					field.clear ();
					fieldStarted = true;                                 // A comma implies another field follows.
					index       += 1;
				}
				else if ( character == QLatin1Char ( '\r' ) )
				{
					end_record ();
					index += ( index + 1 < length && text.at ( index + 1 ) == QLatin1Char ( '\n' ) ) ? 2 : 1;
				}
				else if ( character == QLatin1Char ( '\n' ) )
				{
					end_record ();
					index += 1;
				}
				else
				{
					field.append ( character );
					fieldStarted = true;
					index       += 1;
				}
			}

			if ( fieldStarted || !record.isEmpty () )
			{
				record.append ( field );
				records.push_back ( record );
			}

			return records;
		}
	}

	//-----------------------------------------------------------------------------------------------------------------
	// is_exportable -- a non-empty array of scalar-only objects sharing one key set.
	//-----------------------------------------------------------------------------------------------------------------

	bool CsvCodec::is_exportable ( const JsonNode& node )
	{
		if ( node.kind () != JsonKind::Array || node.array_size () == 0 )
		{
			return false;
		}

		// The reference key set is the first element's; every element must be a scalar-only object matching it.

		const JsonNode* first = node.array_element ( 0 );

		if ( first == nullptr || first->kind () != JsonKind::Object )
		{
			return false;
		}

		QStringList referenceKeys;

		for ( int memberIndex = 0; memberIndex < first->member_count (); ++memberIndex )
		{
			const JsonNode* value = first->member_value ( memberIndex );

			if ( value == nullptr || !value->is_scalar () )
			{
				return false;
			}

			referenceKeys.append ( first->member_key ( memberIndex ) );
		}

		for ( int elementIndex = 1; elementIndex < node.array_size (); ++elementIndex )
		{
			const JsonNode* element = node.array_element ( elementIndex );

			if ( element == nullptr || element->kind () != JsonKind::Object )
			{
				return false;
			}

			if ( element->member_count () != referenceKeys.size () )
			{
				return false;
			}

			for ( int memberIndex = 0; memberIndex < element->member_count (); ++memberIndex )
			{
				const JsonNode* value = element->member_value ( memberIndex );

				if ( value == nullptr || !value->is_scalar () )
				{
					return false;
				}

				if ( !referenceKeys.contains ( element->member_key ( memberIndex ) ) )
				{
					return false;
				}
			}
		}

		return true;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// export_array -- header row plus one record per element, over the union of keys.
	//-----------------------------------------------------------------------------------------------------------------

	CsvExportResult CsvCodec::export_array ( const JsonNode& node )
	{
		CsvExportResult result;

		if ( node.kind () != JsonKind::Array )
		{
			result.error = QStringLiteral ( "CSV export requires an array selection." );
			return result;
		}

		if ( node.array_size () == 0 )
		{
			result.error = QStringLiteral ( "Cannot export an empty array to CSV." );
			return result;
		}

		// Columns: the union of member keys across all elements, in first-encountered order. Validates flatness.

		QStringList columns;

		for ( int elementIndex = 0; elementIndex < node.array_size (); ++elementIndex )
		{
			const JsonNode* element = node.array_element ( elementIndex );

			if ( element == nullptr || element->kind () != JsonKind::Object )
			{
				result.error = QStringLiteral ( "CSV export requires an array of objects." );
				return result;
			}

			for ( int memberIndex = 0; memberIndex < element->member_count (); ++memberIndex )
			{
				const JsonNode* value = element->member_value ( memberIndex );

				if ( value == nullptr || !value->is_scalar () )
				{
					result.error = QStringLiteral ( "CSV export requires flat objects (scalar values only)." );
					return result;
				}

				const QString key = element->member_key ( memberIndex );

				if ( !columns.contains ( key ) )
				{
					columns.append ( key );
				}
			}
		}

		// Header row.

		QStringList lines;
		QStringList headerFields;

		for ( const QString& column : columns )
		{
			headerFields.append ( csv_field ( column ) );
		}

		lines.append ( headerFields.join ( QLatin1Char ( ',' ) ) );

		// One record per element; a member absent from an element writes an empty cell.

		for ( int elementIndex = 0; elementIndex < node.array_size (); ++elementIndex )
		{
			const JsonNode* element = node.array_element ( elementIndex );
			QStringList     fields;

			for ( const QString& column : columns )
			{
				const JsonNode* value = element->find_member ( column );
				fields.append ( value == nullptr ? QString () : csv_field ( scalar_cell_text ( *value ) ) );
			}

			lines.append ( fields.join ( QLatin1Char ( ',' ) ) );
		}

		result.csv = lines.join ( QStringLiteral ( "\r\n" ) );
		result.ok  = true;

		return result;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// import_text -- header record to keys, each later record to a flat object.
	//-----------------------------------------------------------------------------------------------------------------

	CsvImportResult CsvCodec::import_text ( const QString& text )
	{
		CsvImportResult result;

		const std::vector<QStringList> records = parse_records ( text );

		if ( records.empty () )
		{
			result.error = QStringLiteral ( "The CSV input is empty." );
			return result;
		}

		const QStringList& header = records.front ();

		std::unique_ptr<JsonNode> root = JsonNode::make_array ();

		for ( std::size_t recordIndex = 1; recordIndex < records.size (); ++recordIndex )
		{
			const QStringList&        cells   = records [ recordIndex ];
			std::unique_ptr<JsonNode> element = JsonNode::make_object ();

			for ( int columnIndex = 0; columnIndex < header.size (); ++columnIndex )
			{
				const QString cellText = columnIndex < cells.size () ? cells.at ( columnIndex ) : QString ();
				element->append_member ( header.at ( columnIndex ), infer_cell ( cellText ) );
			}

			root->append_element ( std::move ( element ) );
		}

		result.root = std::move ( root );
		result.ok   = true;

		return result;
	}
}
