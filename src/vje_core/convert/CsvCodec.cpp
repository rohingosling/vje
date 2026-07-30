//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CsvCodec implementation. Export picks one of two forms from the array's shape -- the table form (columns = union
//   of keys, first-encountered order) when every element is an object, the single-column form otherwise -- then emits
//   a header row and one record per element, quoting fields per RFC 4180 and separating records with CRLF (no trailing
//   CRLF -- the last record's line break is optional per the RFC, and omitting it keeps round-trips exact). Import runs
//   a character state machine that honours quoted fields, doubled quotes, and embedded separators/newlines, then maps
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
#include <vje_core/services/value_placeholders.hpp>

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
		// The exported text of a single cell: null -> the literal `null`, boolean -> true/false, number -> its raw
		// token, string -> its content, and a nested container -> the shared one-slot placeholder (the same text the
		// Form View's table shows, which is FILE-11's rule for it). Quoting is applied by csv_field; a MISSING member
		// is the caller's case, and writes an empty cell rather than reaching here.
		//
		// outPlaceholders counts the lossy cells rather than returning a flag, because the caller's question is how
		// many -- one placeholder in a hundred rows and a file that is nothing but placeholders deserve the same
		// warning worded with different numbers.
		//-------------------------------------------------------------------------------------------------------------

		QString cell_text ( const JsonNode& node, int& outPlaceholders )
		{
			switch ( node.kind () )
			{
				case JsonKind::Null:    return QStringLiteral ( "null" );
				case JsonKind::Boolean: return node.boolean_value () ? QStringLiteral ( "true" ) : QStringLiteral ( "false" );
				case JsonKind::Number:  return node.number_token ();
				case JsonKind::String:  return node.string_value ();

				case JsonKind::Array:
				{
					++outPlaceholders;

					return value_placeholders::ARRAY;
				}

				case JsonKind::Object:
				{
					++outPlaceholders;

					return value_placeholders::OBJECT;
				}
			}

			return QString ();
		}

		//-------------------------------------------------------------------------------------------------------------
		// The header for the single-column form: the array's own member key, or "value" where it has none. An array
		// that is a member of an object has a name the user chose and it belongs at the top of the column; a root array
		// or an array nested inside another array has only a position, and a column headed "0" would say less than
		// nothing.
        //
		// Derived from the node rather than passed in, so the codec's output is a function of the node alone and a
		// test of the codec can pin the header without a document or a pointer around it.
		//-------------------------------------------------------------------------------------------------------------

		QString single_column_header ( const JsonNode& node )
		{
			const JsonNode* const parent = node.parent ();

			if ( ( parent != nullptr ) && ( parent->kind () == JsonKind::Object ) )
			{
				const int index = node.index_in_parent ();

				if ( ( index >= 0 ) && ( index < parent->member_count () ) )
				{
					return parent->member_key ( index );
				}
			}

			return QStringLiteral ( "value" );
		}

		//-------------------------------------------------------------------------------------------------------------
		// Does every element of this array carry a name for each of its values? That is the only question separating
		// the two export forms: rows of named fields make a table, and anything else is a list.
		//-------------------------------------------------------------------------------------------------------------

		bool every_element_is_an_object ( const JsonNode& array )
		{
			for ( int elementIndex = 0; elementIndex < array.array_size (); ++elementIndex )
			{
				const JsonNode* const element = array.array_element ( elementIndex );

				if ( ( element == nullptr ) || ( element->kind () != JsonKind::Object ) )
				{
					return false;
				}
			}

			return true;
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
	// exportability / is_exportable -- a non-empty array, and nothing more.
	//
	// The rule used to demand an array of scalar-only objects with a uniform key set, and every part of that demand has
	// since become something the exporter simply HANDLES: a nested container writes a placeholder, a ragged array writes
	// the union of keys with empty cells, and an array that is not objects at all writes one column. What is left is the
	// only thing a CSV genuinely cannot be made from -- a thing with no rows.
	//-----------------------------------------------------------------------------------------------------------------

	CsvExportBlocker CsvCodec::exportability ( const JsonNode& node )
	{
		if ( node.kind () != JsonKind::Array )
		{
			return CsvExportBlocker::NotAnArray;
		}

		if ( node.array_size () == 0 )
		{
			return CsvExportBlocker::EmptyArray;
		}

		return CsvExportBlocker::None;
	}

	bool CsvCodec::is_exportable ( const JsonNode& node )
	{
		return exportability ( node ) == CsvExportBlocker::None;
	}

	//-----------------------------------------------------------------------------------------------------------------
	// export_array -- the table form when every element is an object, the single-column form otherwise.
	//-----------------------------------------------------------------------------------------------------------------

	CsvExportResult CsvCodec::export_array ( const JsonNode& node )
	{
		CsvExportResult result;

		switch ( exportability ( node ) )
		{
			case CsvExportBlocker::NotAnArray:
			{
				result.error = QStringLiteral ( "CSV export requires an array selection." );

				return result;
			}

			case CsvExportBlocker::EmptyArray:
			{
				result.error = QStringLiteral ( "Cannot export an empty array to CSV." );

				return result;
			}

			case CsvExportBlocker::None:
			{
				break;
			}
		}

		QStringList lines;

		if ( every_element_is_an_object ( node ) )
		{
			// The TABLE form. Columns are the union of member keys in first-encountered order -- the same rule the Form
			// View's array table uses for a ragged array and the same one EDIT-11 Normalize uses to repair one, so what
			// the file has for columns is what the screen had, and neither needs the document changed to produce it.

			QStringList columns;

			for ( int elementIndex = 0; elementIndex < node.array_size (); ++elementIndex )
			{
				const JsonNode* const element = node.array_element ( elementIndex );

				for ( int memberIndex = 0; memberIndex < element->member_count (); ++memberIndex )
				{
					const QString key = element->member_key ( memberIndex );

					if ( !columns.contains ( key ) )
					{
						columns.append ( key );
					}
				}
			}

			QStringList headerFields;

			for ( const QString& column : columns )
			{
				headerFields.append ( csv_field ( column ) );
			}

			lines.append ( headerFields.join ( QLatin1Char ( ',' ) ) );

			// One record per element. A member the element does not HAVE writes an empty cell, which is what keeps it
			// distinguishable from a member it has whose value is null -- the CSV can carry that distinction, so it does.

			for ( int elementIndex = 0; elementIndex < node.array_size (); ++elementIndex )
			{
				const JsonNode* const element = node.array_element ( elementIndex );
				QStringList           fields;

				for ( const QString& column : columns )
				{
					const JsonNode* const value = element->find_member ( column );

					fields.append
					(
						value == nullptr ? QString () : csv_field ( cell_text ( *value, result.placeholderCells ) )
					);
				}

				lines.append ( fields.join ( QLatin1Char ( ',' ) ) );
			}
		}
		else
		{
			// The SINGLE-COLUMN form: an array of scalars, of arrays, or of mixed kinds. There is no column set to derive
			// -- the elements have no names -- so the array's own name heads the one column there is.

			lines.append ( csv_field ( single_column_header ( node ) ) );

			for ( int elementIndex = 0; elementIndex < node.array_size (); ++elementIndex )
			{
				const JsonNode* const element = node.array_element ( elementIndex );

				lines.append ( element == nullptr ? QString () : csv_field ( cell_text ( *element, result.placeholderCells ) ) );
			}
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
