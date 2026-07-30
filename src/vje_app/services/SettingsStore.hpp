//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   SettingsStore -- the typed, grouped accessor layer over a schema-versioned, human-readable JSON settings file at
//   the platform's standard per-user config location (OQ-A6, NFR-06). It is the single
//   home for every persisted preference: the Settings-dialog values (SET-01..09, wired in later phases) plus UI state
//   not surfaced in the dialog -- theme choice, window/splitter geometry, recent files, and the last-used XML import
//   strategy (SET-08).
//
//   Storage is one flat JSON object of DOT-NAMESPACED keys (e.g. "general.theme") plus a "schemaVersion" member, so
//   the file is trivially inspectable and diff-friendly. Reads are TOLERANT: a missing key, or a stored value of the
//   wrong JSON type, yields the caller-supplied default rather than an error (so a hand-edited, partial, or
//   older-schema file always loads). Writes are IMMEDIATE: each set updates memory and rewrites the whole (tiny) file
//   at once; a no-op write (same value) neither rewrites nor signals.
//
//   The store depends on Qt Core ONLY (no Gui/Widgets), so it is headlessly unit-testable; the constructor takes an
//   explicit file path (tests inject a temp file, the app injects default_file_path()).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// Well-known setting keys. Dot-namespaced and shared by every reader/writer so the string literals live in one
	// place (constants per the coding standard). Phase 5 uses the shell/persistence subset; later phases add the
	// Settings-dialog keys against the same store.
	//-----------------------------------------------------------------------------------------------------------------

	namespace settings_keys
	{
		inline const QString THEME              = QStringLiteral ( "general.theme" );                          // Light / Dark / System (SET-03, section 2.9).
		inline const QString CHECK_UPDATES      = QStringLiteral ( "general.checkForUpdatesAutomatically" );   // Yes / No (SET-03, UPD-02).
		inline const QString ON_DUPLICATE_KEYS  = QStringLiteral ( "general.onDuplicateKeysWhenLoading" );     // KeepSilently / KeepAndWarn (SET-03).
		inline const QString STRING_DISPLAY     = QStringLiteral ( "general.stringDisplay" );                  // Escaped / Decoded / Flattened (SET-03).
		inline const QString ROUNDED_PANE_CORNERS = QStringLiteral ( "general.roundedPaneCorners" );           // Yes / No, default Yes (SET-03, STYLE-02).

		inline const QString WINDOW_GEOMETRY    = QStringLiteral ( "window.geometry" );                        // QMainWindow::saveGeometry (NFR-06).
		inline const QString WINDOW_STATE       = QStringLiteral ( "window.state" );                           // QMainWindow::saveState (toolbar/docks).

		// The splitter's pane WIDTHS, as a list -- deliberately not QSplitter::saveState, which also persists the
		// handle width, the collapsible flag and the resize mode. Those are design constants, and a saved copy of them
		// silently outranks the code that sets them: changing SPLITTER_HANDLE_WIDTH did nothing for anyone with an
		// existing settings file until this changed.

		inline const QString SPLITTER_SIZES     = QStringLiteral ( "workspace.splitterSizes" );                // Pane widths (section 2.5).

		// Superseded by SPLITTER_SIZES. Named only so the stale value can be cleared from existing settings files.

		inline const QString LEGACY_SPLITTER_STATE = QStringLiteral ( "workspace.splitterState" );

		inline const QString RECENT_FILES       = QStringLiteral ( "files.recent" );                           // Recent Files list (FILE-05).

		// The last-used XML import choices (SET-08 / FILE-13). Persisted UI state with no Settings-dialog surface: the
		// Import XML to JSON dialog writes all three when the user presses Import, and the import pipeline reads them so
		// the next import opens on the arrangement they left. The text value key is Custom flattened's alone, and empty
		// means "the element's own name" -- so it is stored empty rather than absent once the user has been through the
		// dialog, which is the D8 distinction between a value and a first run.

		inline const QString XML_IMPORT_STRATEGY      = QStringLiteral ( "import.xmlStrategy" );
		inline const QString XML_INFER_SCALAR_TYPES   = QStringLiteral ( "import.xmlInferScalarTypes" );
		inline const QString XML_TEXT_VALUE_KEY       = QStringLiteral ( "import.xmlTextValueKey" );

		inline const QString FORM_EDIT_ON       = QStringLiteral ( "formView.editOn" );                        // SingleClick / DoubleClick (SET-05, EDITOR-04).
		inline const QString FORM_ALLOW_JAGGED_PASTE = QStringLiteral ( "formView.allowJaggedArrayPaste" );   // Yes / No, default No (SET-05, EDITOR-11).
		inline const QString FORM_ALLOW_KEY_EDITING  = QStringLiteral ( "formView.allowKeyEditing" );         // Yes / No, default Yes (SET-05, EDIT-02).
		inline const QString FORM_WRAP_STRINGS       = QStringLiteral ( "formView.wrapStrings" );            // Yes / No, default No (SET-05).

		// -- Text View group (SET-06, EDITOR-06). The rendering options of TextViewProfile, one key each.

		inline const QString TEXT_WRAP_STRINGS      = QStringLiteral ( "textView.wrapStrings" );             // Yes / No, default No (SET-06).
		inline const QString TEXT_BLANK_LINES       = QStringLiteral ( "textView.blankLinesBetweenFields" ); // 0-10, default 0 (SET-06).
		inline const QString TEXT_ALIGN_SEPARATORS  = QStringLiteral ( "textView.alignNameSeparators" );      // Yes / No (default Yes).
		inline const QString TEXT_NAME_SEPARATOR    = QStringLiteral ( "textView.nameSeparator" );            // 1-3 characters (default ":").
		inline const QString TEXT_INCLUDE_OBJECTS   = QStringLiteral ( "textView.includeObjectNames" );       // The "{...}" rows (default Yes).
		inline const QString TEXT_INCLUDE_ARRAYS    = QStringLiteral ( "textView.includeArrayNames" );        // The "[...]" rows (default Yes).
		inline const QString TEXT_MARKDOWN_STYLE    = QStringLiteral ( "textView.markdownListStyle" );        // None / List / Table.
		inline const QString TEXT_TABLE_STYLE       = QStringLiteral ( "textView.tableStyle" );               // Academic..Tsv (default Columnar).

		// -- Code Editor group (SET-07, EDITOR-07). The first four are the DOCUMENT FORMAT PROFILE, which is shared
		//    verbatim between the Code View's displayed text and File > Save (FILE-03) -- one profile, so what the Code
		//    View shows is byte-for-byte what saves. Built in exactly one place: settings_profiles.hpp.

		inline const QString CODE_INDENT_KIND       = QStringLiteral ( "codeView.indentation" );              // Spaces / Tabs.
		inline const QString CODE_INDENT_SIZE       = QStringLiteral ( "codeView.indentSize" );               // 1-8 (default 2).
		inline const QString CODE_BRACE_STYLE       = QStringLiteral ( "codeView.braceStyle" );               // KAndR / Allman (default Allman).
		inline const QString CODE_ALIGN_SEPARATORS  = QStringLiteral ( "codeView.alignNameSeparators" );      // Yes / No (default No).

		inline const QString CODE_SYNTAX_HIGHLIGHTING = QStringLiteral ( "codeView.syntaxHighlighting" );     // Yes / No (default Yes).
		inline const QString CODE_EDIT_ON             = QStringLiteral ( "codeView.editOn" );                 // SingleClick / DoubleClick (SET-07).

		// -- Printing group (SET-10, FILE-12). What the printed page carries besides the content itself.

		inline const QString PRINT_PAGE_RULES = QStringLiteral ( "printing.pageRules" );                     // Yes / No, default Yes.

		// -- System group (SET-09). Opt-in diagnostic logging; the folder and file name are inert while it is off.

		inline const QString DIAGNOSTIC_LOGGING = QStringLiteral ( "system.diagnosticLogging" );             // Yes / No, default No.
		inline const QString LOG_FOLDER         = QStringLiteral ( "system.logFolder" );                     // Empty => the default folder.
		inline const QString LOG_FILE_NAME      = QStringLiteral ( "system.logFileName" );                   // Empty => the default name.

		// -- Toolbar group (SET-04). ONE key: the toolbar's layout as an ordered list of entry names (views/
		//    toolbar_catalogue), which carries membership and order together. An absent key means the default layout;
		//    a stored EMPTY list means a deliberately empty toolbar, so readers must ask contains() rather than
		//    isEmpty().

		inline const QString TOOLBAR_LAYOUT = QStringLiteral ( "toolbar.layout" );

		// Superseded 2026-07-27 by TOOLBAR_LAYOUT: one boolean per button, which could say whether a button appeared
		// but nothing about where. Retained solely so toolbar_catalogue can read a pre-existing settings file once and
		// then delete these keys -- nothing writes them any more.

		inline const QString TOOLBAR_VISIBLE_PREFIX = QStringLiteral ( "toolbar.visible." );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Stored values for FORM_EDIT_ON. Spelled out here rather than at the read site so the writer (the Settings dialog,
	// Phase 10) and the reader (the Form View) cannot disagree about the string.
	//-----------------------------------------------------------------------------------------------------------------

	namespace settings_values
	{
		// -- General (SET-03). The persisted forms of the three themes. Read and written by ThemeService, and offered by
		//    the Settings dialog's General group, which is why the spelling lives here rather than in either of them.

		inline const QString THEME_LIGHT  = QStringLiteral ( "Light" );   // The default.
		inline const QString THEME_DARK   = QStringLiteral ( "Dark" );
		inline const QString THEME_SYSTEM = QStringLiteral ( "System" );

		// The load-time duplicate-key policy; both KEEP, and only the warning differs (VAL-02).

		inline const QString ON_DUPLICATE_KEEP_SILENTLY = QStringLiteral ( "KeepSilently" );   // The default.
		inline const QString ON_DUPLICATE_KEEP_AND_WARN = QStringLiteral ( "KeepAndWarn" );

		inline const QString EDIT_ON_SINGLE_CLICK = QStringLiteral ( "SingleClick" );
		inline const QString EDIT_ON_DOUBLE_CLICK = QStringLiteral ( "DoubleClick" );   // The default (SET-05 / SET-07).

		// -- String display (SET-03). Spelled to match the StringDisplay enumerators one-for-one, for the same reason
		//    the Text View values match theirs: the mapping is then a lookup rather than a translation.

		inline const QString STRING_DISPLAY_ESCAPED   = QStringLiteral ( "Escaped" );   // The default (SET-03).
		inline const QString STRING_DISPLAY_DECODED   = QStringLiteral ( "Decoded" );
		inline const QString STRING_DISPLAY_FLATTENED = QStringLiteral ( "Flattened" );

		// -- Text View (SET-06). Spelled to match the MarkdownListStyle / TableStyle enumerators one-for-one, so the
		//    mapping in settings_profiles.cpp is a lookup rather than a translation.

		inline const QString MARKDOWN_STYLE_NONE  = QStringLiteral ( "None" );          // The default.
		inline const QString MARKDOWN_STYLE_LIST  = QStringLiteral ( "List" );
		inline const QString MARKDOWN_STYLE_TABLE = QStringLiteral ( "Table" );

		inline const QString TABLE_STYLE_ACADEMIC    = QStringLiteral ( "Academic" );
		inline const QString TABLE_STYLE_COMPACT     = QStringLiteral ( "Compact" );
		inline const QString TABLE_STYLE_COLUMNAR    = QStringLiteral ( "Columnar" );   // The default (SET-06).
		inline const QString TABLE_STYLE_SPREADSHEET = QStringLiteral ( "Spreadsheet" );
		inline const QString TABLE_STYLE_MINIMAL     = QStringLiteral ( "Minimal" );
		inline const QString TABLE_STYLE_MARKDOWN    = QStringLiteral ( "Markdown" );
		inline const QString TABLE_STYLE_CSV         = QStringLiteral ( "CSV" );
		inline const QString TABLE_STYLE_TSV         = QStringLiteral ( "TSV" );

		// -- Code Editor (SET-07).

		inline const QString INDENT_SPACES = QStringLiteral ( "Spaces" );               // The default.
		inline const QString INDENT_TABS   = QStringLiteral ( "Tabs" );

		inline const QString BRACE_STYLE_K_AND_R = QStringLiteral ( "KAndR" );
		inline const QString BRACE_STYLE_ALLMAN  = QStringLiteral ( "Allman" );         // The default.

		// -- XML import (SET-08 / FILE-13). Spelled to match the XmlImportStrategyKind enumerators one-for-one, so the
		//    mapping is a lookup rather than a translation.

		inline const QString XML_STRATEGY_BADGER_FISH           = QStringLiteral ( "BadgerFish" );
		inline const QString XML_STRATEGY_DIRECT_ATTRIBUTE_KEYS = QStringLiteral ( "DirectAttributeKeys" );   // The default.
		inline const QString XML_STRATEGY_GROUPED_ATTRIBUTES    = QStringLiteral ( "GroupedAttributes" );
		inline const QString XML_STRATEGY_CUSTOM_FLATTENED      = QStringLiteral ( "CustomFlattened" );

		// -- System (SET-09). The diagnostic log's default file name, offered by the dialog and used by the log itself
		//    when the setting names none.

		inline const QString DEFAULT_LOG_FILE_NAME = QStringLiteral ( "vje.log" );
	}

	//*****************************************************************************************************************
	// Class: SettingsStore
	//*****************************************************************************************************************

	class SettingsStore : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constants
		//=============================================================================================================

	public:

		static constexpr int SCHEMA_VERSION = 1;                   // Bumped when the on-disk shape changes incompatibly.

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		// Construct over an explicit file path and load it immediately (a missing or malformed file is tolerated:
		// the store starts empty and stamps the current schema version).

		explicit SettingsStore ( const QString& filePath, QObject* parent = nullptr );

		// The default per-user config path: QStandardPaths::AppConfigLocation + "/settings.json".

		static QString default_file_path ();

		//=============================================================================================================
		// Value Accessors -- tolerant typed reads (wrong-type or missing -> defaultValue).
		//=============================================================================================================

	public:

		const QString& file_path () const;
		bool           contains  ( const QString& key ) const;

		bool        value_bool        ( const QString& key, bool defaultValue ) const;
		int         value_int         ( const QString& key, int defaultValue ) const;
		QString     value_string      ( const QString& key, const QString& defaultValue ) const;
		QStringList value_string_list ( const QString& key ) const;   // Empty list if absent or not an array of strings.
		QByteArray  value_bytes       ( const QString& key ) const;   // Base64-decoded; empty if absent or not a string.

		//=============================================================================================================
		// Mutators -- immediate writes. Each returns true when the stored value actually changed (and was persisted).
		//=============================================================================================================

	public:

		bool set_bool        ( const QString& key, bool value );
		bool set_int         ( const QString& key, int value );
		bool set_string      ( const QString& key, const QString& value );
		bool set_string_list ( const QString& key, const QStringList& value );
		bool set_bytes       ( const QString& key, const QByteArray& value );   // Stored Base64 for readability.

		void remove ( const QString& key );

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		bool save () const;                                        // Persist now (also invoked by each setter).

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		void changed ( const QString& key );                       // A value was written (or removed).

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		void load        ();                                       // (Re)read the file; tolerant of missing/malformed.
		bool store_value ( const QString& key, const QJsonValue& value );   // Shared setter core: dedupe, write, signal.

		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		QString     storeFilePath;                                 // The backing file.
		QJsonObject values;                                        // Flat: dot-namespaced key -> value, plus schemaVersion.
	};
}
