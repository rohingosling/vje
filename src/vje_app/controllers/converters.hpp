//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   converters -- the registered import/export formats behind File > Import / Export (FILE-11), and the pure text
//   conversions they perform.
//
//   WHAT THIS IS. One TABLE of format descriptors plus two pure functions over it. The File menu is built from the
//   table, the enablement is asked of the table, and the pipelines convert through it -- so "version 2.0 registers CSV,
//   YAML, and XML" is one list rather than three menus, three pickers, and three switch statements that have to agree.
//
//   WHY THE CONVERSIONS ARE PURE. They run on a worker thread (NFR-04), so they must not touch the settings store, the
//   document, or any QWidget. Everything variable -- the XML strategy and its Infer-scalar-types toggle (SET-08) -- is
//   read on the UI thread into ImportOptions and passed in.
//
//   WHAT LIVES ELSEWHERE. File reading and writing, the pickers, and the FILE-08 flow are FileController's; the
//   conversion algorithms themselves are vje_core's (CsvCodec, YamlCodec, XmlImporter, XmlExporter). This module is the
//   catalogue that joins them to the menu.
//
//   XML import runs the FILE-13 Import XML to JSON dialog between the picker and the conversion, and the dialog's
//   answer arrives here as an ImportOptions exactly as the persisted one does -- import_text() cannot tell which it was
//   given, which is what keeps the conversion pure.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_core/convert/XmlImporter.hpp>

#include <QString>

#include <memory>
#include <vector>

namespace vje
{
	class JsonNode;
	class SettingsStore;

	//-----------------------------------------------------------------------------------------------------------------
	// One registered format. exportsSelection is the CSV exception spelled as data rather than as a special case: CSV
	// exports the current tree SELECTION (any non-empty array -- spec section 2.12), while YAML and XML export the whole
	// document (FILE-11).
	//-----------------------------------------------------------------------------------------------------------------

	struct ConverterFormat
	{
		QString id;                                            // Stable identity; carried in the menu action's data.
		QString displayName;                                   // The menu item's text.
		QString filters;                                       // Picker filters.
		bool    supportsImport   = false;
		bool    supportsExport   = false;
		bool    exportsSelection = false;                      // CSV only.

		// Does an import of this format ask the user something before converting (FILE-13)? XML only: its four
		// strategies are a genuine choice with no defensible default beyond "the last one you picked", so the pipeline
		// stops between the picker and the conversion and runs the Import XML to JSON dialog.
		//
		// A BOOLEAN rather than the format id tested at the pipeline, for the reason the CSV selection rule is a field:
		// the pipeline asks the table what a format needs, it does not carry a list of format names. The seam it then
		// calls is XML-shaped, because XML is the only format with options -- a second one turns this into a kind.

		bool promptsForImportOptions = false;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The variable part of an import, read from the settings store on the UI thread so the conversion itself can run on
	// a worker thread without touching it (SET-08).
	//-----------------------------------------------------------------------------------------------------------------

	struct ImportOptions
	{
		XmlImportStrategyKind xmlStrategy         = XmlImportStrategyKind::DirectAttributeKeys;   // "(Recommended)".
		bool                  xmlInferScalarTypes = false;                                       // Default off.

		// Custom flattened's Text value key (section 2.11). Empty means the element's own name, which is the documented
		// default -- stated as emptiness rather than as a literal, because the default is not one key but a rule, and
		// there is no string that means "whatever this element is called".

		QString xmlTextValueKey;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The outcome of a conversion. Import carries the built root; export carries the rendered text. Both report a
	// human-readable reason on failure (VAL-04).
	//-----------------------------------------------------------------------------------------------------------------

	struct ImportConversion
	{
		std::unique_ptr<JsonNode> root;
		bool                      ok = false;
		QString                   error;
	};

	struct ExportConversion
	{
		QString text;
		bool    ok = false;
		QString error;

		// What the conversion could not carry across, when it succeeded anyway -- empty when there is nothing to say,
		// which is the usual case. A note is not an error: the user got the file they asked for, and this is the
		// sentence that stops them assuming it round-trips (CSV's container placeholders are the only source of one
		// in version 2.0).

		QString note;
	};

	//-----------------------------------------------------------------------------------------------------------------
	// Why an export cannot run right now (FILE-11). Reported as a REASON rather than as a bool, because a command the
	// user just invoked and that did nothing owes them an explanation -- and "there is no document", "nothing is
	// selected" and "the selection is an object" are three different problems with three different fixes.
	//
	// The first two are the application's to notice (a document and a selection are not things a codec can see); the
	// last two mirror CsvExportBlocker, which is the codec's own rule and stays the codec's.
	//-----------------------------------------------------------------------------------------------------------------

	enum class ExportBlocker
	{
		None,
		NoDocument,        // Nothing is open.
		NoSelection,       // A selection-scoped format (CSV) with nothing selected, or a selection that no longer resolves.
		NotAnArray,        // CSV: the selection has no rows to make records from.
		EmptyArray         // CSV: an array with nothing in it -- not even a header, since the columns come from the elements.
	};

	//-----------------------------------------------------------------------------------------------------------------
	// The catalogue.
	//-----------------------------------------------------------------------------------------------------------------

	// The registered formats, in menu order (CSV, YAML, XML).

	const std::vector<ConverterFormat>& registered_converters ();

	// The format with this id, or nullptr. The menu carries ids rather than pointers so an action's data survives any
	// future re-registration.

	const ConverterFormat* find_converter ( const QString& id );

	//-----------------------------------------------------------------------------------------------------------------
	// The conversions. Pure: no settings, no document, no widgets -- safe on a worker thread.
	//-----------------------------------------------------------------------------------------------------------------

	ImportConversion import_text ( const ConverterFormat& format, const QString& text, const ImportOptions& options );

	ExportConversion export_text ( const ConverterFormat& format, const JsonNode& node );

	// What stands between this node and an export in this format -- ExportBlocker::None when nothing does. The CSV
	// precondition is asked of CsvCodec rather than restated here, so the menu, the message and the export itself
	// cannot come to disagree about what CSV accepts; the whole-document formats accept any node.
	//
	// A null node answers NoDocument. The caller distinguishes that from NoSelection, because only it knows which was
	// the case (FileController::export_source does, and hands the answer down).

	ExportBlocker node_export_blocker ( const ConverterFormat& format, const JsonNode* node );

	// The sentence shown to the user for a blocker, in this format's terms. Written here rather than at either of the
	// two places it is shown, so the menu and the dialog say the same thing.

	QString describe_export_blocker ( const ConverterFormat& format, ExportBlocker blocker );

	// Is this node exportable in this format? Kept as its own name because that is the question the export itself asks
	// on the way in, after the blocker has already been reported.

	bool can_export ( const ConverterFormat& format, const JsonNode* node );

	//-----------------------------------------------------------------------------------------------------------------
	// The settings reader for the import options (SET-08). Deliberately NOT in settings_profiles: that module's job is
	// settings -> vje_core RENDERING profile, and putting an XmlImporter type in its header would drag the whole
	// converter into the include graph of every view that reads a profile. The stored spellings still live in
	// settings_values, so writer (the Phase 12 dialog) and reader cannot disagree about them.
	//-----------------------------------------------------------------------------------------------------------------

	ImportOptions import_options ( const SettingsStore* settings );

	// Remember what the user just chose, so the next import preselects it (SET-08). The inverse of import_options, and
	// beside it for the reason Copy JSON Pointer sits beside Go To: two spellings of the same three values drift the
	// moment they are written in different files.

	void store_import_options ( SettingsStore* settings, const ImportOptions& options );
}
