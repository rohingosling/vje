//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   converters implementation. See the header for why the table exists and why the conversions take no settings.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/converters.hpp"

#include "services/SettingsStore.hpp"

#include <vje_core/convert/CsvCodec.hpp>
#include <vje_core/convert/XmlExporter.hpp>
#include <vje_core/convert/YamlCodec.hpp>
#include <vje_core/document/JsonNode.hpp>

#include <QObject>

#include <utility>

namespace vje
{
	namespace
	{
		// Format ids. Stable strings rather than an enum, because they travel through a QAction's data and (from
		// Phase 12) may be named in the settings store.

		const QString CSV_ID  = QStringLiteral ( "csv" );
		const QString YAML_ID = QStringLiteral ( "yaml" );
		const QString XML_ID  = QStringLiteral ( "xml" );
	}

	//=================================================================================================================
	// The catalogue
	//=================================================================================================================

	const std::vector<ConverterFormat>& registered_converters ()
	{
		// Built once, in menu order. Every filter offers All Files as well, for the same reason FILE-07 does: the
		// extension is the user's business, not the format's.

		static const std::vector<ConverterFormat> formats =
		{
			{
				CSV_ID,
				QObject::tr ( "CSV" ),
				QObject::tr ( "CSV Files (*.csv);;All Files (*)" ),
				true,
				true,
				true                                           // Exports the tree SELECTION (FILE-11).
			},
			{
				YAML_ID,
				QObject::tr ( "YAML" ),
				QObject::tr ( "YAML Files (*.yaml *.yml);;All Files (*)" ),
				true,
				true,
				false
			},
			{
				XML_ID,
				QObject::tr ( "XML" ),
				QObject::tr ( "XML Files (*.xml);;All Files (*)" ),
				true,
				true,
				false,
				true                                           // Prompts for a strategy first (FILE-13).
			}
		};

		return formats;
	}

	const ConverterFormat* find_converter ( const QString& id )
	{
		for ( const ConverterFormat& format : registered_converters () )
		{
			if ( format.id == id )
			{
				return &format;
			}
		}

		return nullptr;
	}

	//=================================================================================================================
	// Conversions
	//=================================================================================================================

	ImportConversion import_text ( const ConverterFormat& format, const QString& text, const ImportOptions& options )
	{
		ImportConversion conversion;

		if ( format.id == CSV_ID )
		{
			CsvImportResult result = CsvCodec::import_text ( text );

			conversion.root  = std::move ( result.root );
			conversion.ok    = result.ok;
			conversion.error = result.error;

			return conversion;
		}

		if ( format.id == YAML_ID )
		{
			YamlCodec::ImportResult result = YamlCodec::from_yaml ( text );

			conversion.root  = std::move ( result.root );
			conversion.ok    = result.ok;
			conversion.error = result.error;

			return conversion;
		}

		if ( format.id == XML_ID )
		{
			// The strategy is whatever the FILE-13 dialog left in options (or, with no dialog in the way, the user's last
			// choice -- SET-08). Resolved on the UI thread, so this call touches nothing shared.

			const std::unique_ptr<IXmlImportStrategy> strategy = XmlImporter::make_strategy
			(
				options.xmlStrategy,
				options.xmlTextValueKey
			);

			XmlImporter::Result result = XmlImporter::import_text ( text, *strategy, options.xmlInferScalarTypes );

			conversion.root  = std::move ( result.root );
			conversion.ok    = result.ok;
			conversion.error = result.error;

			return conversion;
		}

		conversion.error = QObject::tr ( "Unknown import format." );

		return conversion;
	}

	ExportConversion export_text ( const ConverterFormat& format, const JsonNode& node )
	{
		ExportConversion conversion;

		if ( format.id == CSV_ID )
		{
			const CsvExportResult result = CsvCodec::export_array ( node );

			conversion.text  = result.csv;
			conversion.ok    = result.ok;
			conversion.error = result.error;

			// The lossy half of a successful export (FILE-11): a nested object or array was written as the placeholder
			// the Form View's table shows for it, which no reader can turn back into the value. Counted rather than
			// flagged, because "1 value" and "4,000 values" are the same warning read very differently.

			if ( result.ok && ( result.placeholderCells > 0 ) )
			{
				conversion.note = ( result.placeholderCells == 1 )
				    ? QObject::tr ( "1 nested value was written as a placeholder and cannot be read back." )
				    : QObject::tr ( "%1 nested values were written as placeholders and cannot be read back." )
				          .arg ( result.placeholderCells );
			}

			return conversion;
		}

		if ( format.id == YAML_ID )
		{
			conversion.text = YamlCodec::to_yaml ( node );
			conversion.ok   = true;

			return conversion;
		}

		if ( format.id == XML_ID )
		{
			conversion.text = XmlExporter::export_document ( node );
			conversion.ok   = true;

			return conversion;
		}

		conversion.error = QObject::tr ( "Unknown export format." );

		return conversion;
	}

	ExportBlocker node_export_blocker ( const ConverterFormat& format, const JsonNode* node )
	{
		if ( !format.supportsExport )
		{
			return ExportBlocker::NotAnArray;                  // Unreachable through the menu; no format is import-only.
		}

		if ( node == nullptr )
		{
			return ExportBlocker::NoDocument;
		}

		// CSV is the only format with a precondition, and it belongs to the codec that has to honour it -- a second
		// opinion here is how the menu and the export would come to disagree (FILE-11).

		if ( format.exportsSelection )
		{
			switch ( CsvCodec::exportability ( *node ) )
			{
				case CsvExportBlocker::NotAnArray: return ExportBlocker::NotAnArray;
				case CsvExportBlocker::EmptyArray: return ExportBlocker::EmptyArray;
				case CsvExportBlocker::None:       return ExportBlocker::None;
			}
		}

		return ExportBlocker::None;
	}

	QString describe_export_blocker ( const ConverterFormat& format, ExportBlocker blocker )
	{
		// Each says what is wrong AND what to do about it, because a user who invoked a command and got nothing is
		// asking the second question as much as the first.

		switch ( blocker )
		{
			case ExportBlocker::None:
			{
				return QString ();
			}

			case ExportBlocker::NoDocument:
			{
				return QObject::tr ( "There is no document to export. Open or create one first." );
			}

			case ExportBlocker::NoSelection:
			{
				return QObject::tr ( "%1 export writes the selected node. Select an array in the tree first." )
				           .arg ( format.displayName );
			}

			case ExportBlocker::NotAnArray:
			{
				return QObject::tr ( "%1 export needs an array: a CSV file is a list of records, and the selected node "
				                     "is not a list. Select an array in the tree." ).arg ( format.displayName );
			}

			case ExportBlocker::EmptyArray:
			{
				return QObject::tr ( "The selected array is empty, so there is nothing to export -- not even a header "
				                     "row, since the columns come from the elements." );
			}
		}

		return QString ();
	}

	bool can_export ( const ConverterFormat& format, const JsonNode* node )
	{
		return node_export_blocker ( format, node ) == ExportBlocker::None;
	}

	//=================================================================================================================
	// The settings reader (SET-08)
	//=================================================================================================================

	ImportOptions import_options ( const SettingsStore* settings )
	{
		ImportOptions options;

		if ( settings == nullptr )
		{
			return options;
		}

		const QString strategy = settings->value_string
		(
			settings_keys::XML_IMPORT_STRATEGY,
			settings_values::XML_STRATEGY_DIRECT_ATTRIBUTE_KEYS
		);

		if ( strategy == settings_values::XML_STRATEGY_BADGER_FISH )
		{
			options.xmlStrategy = XmlImportStrategyKind::BadgerFish;
		}
		else if ( strategy == settings_values::XML_STRATEGY_GROUPED_ATTRIBUTES )
		{
			options.xmlStrategy = XmlImportStrategyKind::GroupedAttributes;
		}
		else if ( strategy == settings_values::XML_STRATEGY_CUSTOM_FLATTENED )
		{
			options.xmlStrategy = XmlImportStrategyKind::CustomFlattened;
		}
		else
		{
			// Anything unrecognized (including the default spelling) is the recommended strategy -- a tolerant read, the
			// same rule the store itself follows.

			options.xmlStrategy = XmlImportStrategyKind::DirectAttributeKeys;
		}

		options.xmlInferScalarTypes = settings->value_bool ( settings_keys::XML_INFER_SCALAR_TYPES, false );
		options.xmlTextValueKey     = settings->value_string ( settings_keys::XML_TEXT_VALUE_KEY, QString () );

		return options;
	}

	void store_import_options ( SettingsStore* settings, const ImportOptions& options )
	{
		if ( settings == nullptr )
		{
			return;
		}

		QString strategy = settings_values::XML_STRATEGY_DIRECT_ATTRIBUTE_KEYS;

		switch ( options.xmlStrategy )
		{
			case XmlImportStrategyKind::BadgerFish:          strategy = settings_values::XML_STRATEGY_BADGER_FISH;           break;
			case XmlImportStrategyKind::DirectAttributeKeys: strategy = settings_values::XML_STRATEGY_DIRECT_ATTRIBUTE_KEYS; break;
			case XmlImportStrategyKind::GroupedAttributes:   strategy = settings_values::XML_STRATEGY_GROUPED_ATTRIBUTES;    break;
			case XmlImportStrategyKind::CustomFlattened:     strategy = settings_values::XML_STRATEGY_CUSTOM_FLATTENED;      break;
		}

		settings->set_string ( settings_keys::XML_IMPORT_STRATEGY,    strategy );
		settings->set_bool   ( settings_keys::XML_INFER_SCALAR_TYPES, options.xmlInferScalarTypes );
		settings->set_string ( settings_keys::XML_TEXT_VALUE_KEY,     options.xmlTextValueKey );
	}
}
