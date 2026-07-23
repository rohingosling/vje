//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
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

		inline const QString FORM_EDIT_ON       = QStringLiteral ( "formView.editOn" );                        // SingleClick / DoubleClick (SET-05, EDITOR-04).

		// -- Text View group (SET-06, EDITOR-06). The rendering options of TextViewProfile, one key each.

		inline const QString TEXT_ALIGN_SEPARATORS  = QStringLiteral ( "textView.alignNameSeparators" );      // Yes / No (default Yes).
		inline const QString TEXT_NAME_SEPARATOR    = QStringLiteral ( "textView.nameSeparator" );            // 1-3 characters (default ":").
		inline const QString TEXT_INCLUDE_OBJECTS   = QStringLiteral ( "textView.includeObjectNames" );       // The "{...}" rows (default Yes).
		inline const QString TEXT_INCLUDE_ARRAYS    = QStringLiteral ( "textView.includeArrayNames" );        // The "[...]" rows (default Yes).
		inline const QString TEXT_MARKDOWN_STYLE    = QStringLiteral ( "textView.markdownListStyle" );        // None / List / Table.
		inline const QString TEXT_TABLE_STYLE       = QStringLiteral ( "textView.tableStyle" );               // Academic..Tsv (default Compact).

		// -- Code Editor group (SET-07, EDITOR-07). The first four are the DOCUMENT FORMAT PROFILE, which is shared
		//    verbatim between the Code View's displayed text and File > Save (FILE-03) -- one profile, so what the Code
		//    View shows is byte-for-byte what saves. Built in exactly one place: settings_profiles.hpp.

		inline const QString CODE_INDENT_KIND       = QStringLiteral ( "codeView.indentation" );              // Spaces / Tabs.
		inline const QString CODE_INDENT_SIZE       = QStringLiteral ( "codeView.indentSize" );               // 1-8 (default 2).
		inline const QString CODE_BRACE_STYLE       = QStringLiteral ( "codeView.braceStyle" );               // KAndR / Allman (default Allman).
		inline const QString CODE_ALIGN_SEPARATORS  = QStringLiteral ( "codeView.alignNameSeparators" );      // Yes / No (default No).

		inline const QString CODE_SYNTAX_HIGHLIGHTING = QStringLiteral ( "codeView.syntaxHighlighting" );     // Yes / No (default Yes).
		inline const QString CODE_EDIT_ON             = QStringLiteral ( "codeView.editOn" );                 // SingleClick / DoubleClick (SET-07).
	}

	//-----------------------------------------------------------------------------------------------------------------
	// Stored values for FORM_EDIT_ON. Spelled out here rather than at the read site so the writer (the Settings dialog,
	// Phase 10) and the reader (the Form View) cannot disagree about the string.
	//-----------------------------------------------------------------------------------------------------------------

	namespace settings_values
	{
		inline const QString EDIT_ON_SINGLE_CLICK = QStringLiteral ( "SingleClick" );
		inline const QString EDIT_ON_DOUBLE_CLICK = QStringLiteral ( "DoubleClick" );   // The default (SET-05 / SET-07).

		// -- Text View (SET-06). Spelled to match the MarkdownListStyle / TableStyle enumerators one-for-one, so the
		//    mapping in settings_profiles.cpp is a lookup rather than a translation.

		inline const QString MARKDOWN_STYLE_NONE  = QStringLiteral ( "None" );          // The default.
		inline const QString MARKDOWN_STYLE_LIST  = QStringLiteral ( "List" );
		inline const QString MARKDOWN_STYLE_TABLE = QStringLiteral ( "Table" );

		inline const QString TABLE_STYLE_ACADEMIC    = QStringLiteral ( "Academic" );
		inline const QString TABLE_STYLE_COMPACT     = QStringLiteral ( "Compact" );    // The default.
		inline const QString TABLE_STYLE_COLUMNAR    = QStringLiteral ( "Columnar" );
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
