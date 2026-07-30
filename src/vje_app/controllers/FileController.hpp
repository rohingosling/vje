//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FileController -- the document lifecycle: New, Open, Save, Save As, Close, the FILE-08 unsaved-changes gate, the
//   Recent Files list (FILE-05), and (from the import/export step) the converter pipelines (FILE-11).
//
//   WHY IT IS NOT IN MainWindow. Every one of these commands is a short sequence of DECISIONS -- is a view holding an
//   uncommitted edit, is the document dirty, did the user cancel the prompt, did the parse fail, is there a path to save
//   to yet -- and the branches multiply: Save on an untitled document becomes Save As, whose cancellation must abort the
//   Save, which may itself be an answer to the gate that was asked because the user chose Exit. Left in the window those
//   paths are only reachable by hand through native modal dialogs. Behind IDialogService they are ordinary unit tests,
//   which is the whole reason the seam exists.
//
//   WHAT IT KNOWS. vje_core's DocumentIo, the document, the undo stack, the settings store, the selection and status
//   services, a dialog service, and a BackgroundIo to run the actual I/O away from the UI thread (NFR-04). It knows
//   nothing about widgets, menus, or the editor pane: the one thing it needs from the UI -- "may I leave the active view
//   without dropping an uncommitted edit?" (EDITOR-09) -- arrives as an injected predicate.
//
//   THE ORDER OF THE TWO GATES IS FIXED and is a correctness property, not a preference. The VIEW gate runs first: there
//   is no point asking whether to save a document whose latest edit has not reached it yet, and asking in the other
//   order would let a Save write the document as it was BEFORE the Code View's pending commit (FILE-03 / EDITOR-09).
//
//   EVERY COMMAND RETURNS bool: true when it ran to completion, false when it was refused -- a cancelled prompt, a
//   cancelled picker, a failed load or save. A caller acting on the strength of a command (the window close, Save
//   answering the dirty prompt) must treat false as "abort".
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "controllers/converters.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace vje
{
	class BackgroundIo;
	class IDialogService;
	class JsonDocument;
	class JsonNode;
	class SelectionService;
	class SettingsStore;
	class StatusService;
	class UndoController;

	//-----------------------------------------------------------------------------------------------------------------
	// Picker filters. JSON first with All Files available, because FILE-07 tolerates any extension the user picks.
	//-----------------------------------------------------------------------------------------------------------------

	namespace file_filters
	{
		inline const QString JSON = QStringLiteral ( "JSON Files (*.json);;All Files (*)" );
	}

	//-----------------------------------------------------------------------------------------------------------------
	// How the document is named in the UI: its file name, or "Untitled" before it has a path. Stated once because the
	// window title (section 2.2), the status bar's document pane (section 2.8), and the FILE-08 prompt must all name the
	// same document the same way -- three copies of the rule would drift the day one of them gained an elision or a
	// modified marker.
	//-----------------------------------------------------------------------------------------------------------------

	QString document_display_name ( const JsonDocument& document );

	//-----------------------------------------------------------------------------------------------------------------
	// What an export would act on, and why it cannot when it cannot (FILE-11). node is null exactly when blocker is not
	// None, so a caller may test either -- the blocker is the one that can be shown to the user.
	//-----------------------------------------------------------------------------------------------------------------

	struct ExportSource
	{
		JsonNode*     node    = nullptr;
		ExportBlocker blocker = ExportBlocker::NoDocument;
	};

	//*****************************************************************************************************************
	// Class: FileController
	//*****************************************************************************************************************

	class FileController : public QObject
	{
		Q_OBJECT

		//=============================================================================================================
		// Constructors
		//=============================================================================================================

	public:

		FileController
		(
			JsonDocument*     document,
			UndoController*   undo,
			SettingsStore*    settings,
			SelectionService* selection,
			StatusService*    status,
			IDialogService*   dialogs,
			BackgroundIo*     io,
			QObject*          parent = nullptr
		);

		//=============================================================================================================
		// Wiring
		//=============================================================================================================

	public:

		// The EDITOR-09 departure gate -- in the application, EditorPane::confirm_leaving_active_view. It commits a valid
		// pending edit and asks keep / discard when the edit is invalid; false means the user chose to keep editing and
		// the file command must abort. Left unset (the tests' default), there is nothing to commit and it passes.

		void set_view_departure_gate ( std::function<bool ()> gate );

		//=============================================================================================================
		// Commands
		//=============================================================================================================

	public:

		bool new_document ();                                      // FILE-02: a fresh empty object.
		bool open         ();                                      // FILE-01: pick a file, then open it.
		bool save         ();                                      // FILE-03: to the current path (Save As if untitled).
		bool save_as      ();                                      // FILE-03: pick a path, then save to it.
		bool close_document ();                                    // Discard the document (through the FILE-08 gate).

		// Open a specific path through the same gates as Open: the command line (FILE-01), a Recent Files entry
		// (FILE-05), and a dropped file (FILE-09) all arrive here.

		bool open_path ( const QString& path );

		//=============================================================================================================
		// Import / Export (FILE-11)
		//=============================================================================================================

	public:

		// Import runs the same gates as Open -- it replaces the document -- then converts the picked file into a NEW,
		// untitled document: the source is a CSV / YAML / XML file, so it is not a path this document can be saved back
		// to, and it is deliberately not added to Recent Files (FILE-05).

		bool import_document ( const ConverterFormat& format );

		// Export writes the document (or, for CSV, the current tree selection) to a picked path. It runs the EDITOR-09
		// view gate so what is exported includes a pending view edit, but never the FILE-08 gate: exporting neither
		// replaces nor saves the document.

		bool export_document ( const ConverterFormat& format );

		// What an export of this format would act on: the selection for CSV, the root otherwise -- and, when the answer
		// is "nothing", WHY (FILE-11). One function decides both, because the enablement, the refusal message and the
		// export itself must not each form their own opinion about whether an export is possible.

		ExportSource export_source ( const ConverterFormat& format ) const;

		//=============================================================================================================
		// The FILE-08 gate, for callers that are not file commands
		//=============================================================================================================

	public:

		// May the current document be discarded? Clean or absent, yes. Dirty, it asks Save / Don't Save / Cancel and
		// honours the answer (a Save that fails or is cancelled answers false). The window close asks this; so will
		// installing an update (Phase 15).

		bool confirm_discard_changes ();

		//=============================================================================================================
		// Recent Files (FILE-05)
		//=============================================================================================================

	public:

		// The persisted list, most recent first. Entries are absolute paths; the list is capped at
		// config::limits::RECENT_FILES and de-duplicated case-insensitively.

		QStringList recent_files () const;

		//=============================================================================================================
		// Signals
		//=============================================================================================================

	signals:

		void recent_files_changed ();                              // The menu rebuilds from recent_files().

		//=============================================================================================================
		// Helpers
		//=============================================================================================================

	private:

		bool leave_active_view ();                                 // The EDITOR-09 gate (true when there is none).

		// Load path into the document, with no gates -- the callers have already run them. Reports a parse or access
		// failure per FILE-06 and answers false.

		bool load_into_document ( const QString& path );

		// Write the document to path through the document format profile (FILE-03). Reports a write failure and answers
		// false; on success clears dirty, marks the undo baseline, and records the path in Recent Files.

		bool write_document ( const QString& path );

		// Replace the document wholesale (load, New, and -- later -- import): install the root, reset the undo history
		// and the dirty flag, and select the root (NAV-01). path may be empty, which is what makes a document untitled.

		void install_document ( std::unique_ptr<JsonNode> root, const QString& path );

		// The one import failure report -- a read that failed and a conversion that failed say the same thing about the
		// same file, and now happen at two points either side of the FILE-13 dialog.

		void report_import_failure ( const QString& path, const QString& reason );

		void note_recent_file   ( const QString& path );           // Prepend, de-duplicate, cap, persist, signal.
		void forget_recent_file ( const QString& path );           // Drop an entry that no longer resolves.

		// Read a whole text file as UTF-8 / write one as UTF-8 (the import and export halves of FILE-11). Both run inside
		// the BackgroundIo hand-off, so neither touches anything but its arguments.

		static bool read_text_file  ( const QString& path, QString* outText, QString* outError );
		static bool write_text_file ( const QString& path, const QString& text, QString* outError );

		// Where a picker should open: the current document's path, else the folder of the most recent file, else empty
		// (the platform's own default).

		QString picker_start_path () const;

		//=============================================================================================================
		// Data Members -- injected collaborators (non-owning).
		//=============================================================================================================

	private:

		JsonDocument*     document;
		UndoController*   undo;
		SettingsStore*    settings;
		SelectionService* selection;
		StatusService*    status;
		IDialogService*   dialogs;
		BackgroundIo*     io;

		std::function<bool ()> viewDepartureGate;

		// Refuses a second load or write while one is in flight. ThreadPoolIo already holds user input for the duration,
		// so this covers only a non-input path into a file command (a queued signal), where a half-installed document
		// would otherwise be reachable.

		bool ioInProgress = false;
	};
}
