//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   FileController implementation. See the header for the two-gate order and why the commands return bool.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/FileController.hpp"

#include "AppConfig.hpp"
#include "controllers/XmlImportController.hpp"
#include "services/BackgroundIo.hpp"
#include "services/IDialogService.hpp"
#include "services/SelectionService.hpp"
#include "services/SettingsStore.hpp"
#include "services/StatusService.hpp"
#include "services/settings_profiles.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/document/JsonNode.hpp>
#include <vje_core/document/JsonPointer.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/services/DocumentIo.hpp>

#include <QFile>
#include <QFileInfo>

#include <utility>

namespace vje
{
	namespace
	{
		// How long a file-command outcome stays in the status bar. Long enough to notice after the window has already
		// changed under it; the failures also raise a dialog, so the message is a confirmation and not the only report.

		constexpr int MESSAGE_TIMEOUT = 3000;
	}

	//=================================================================================================================
	// Free Functions
	//=================================================================================================================

	QString document_display_name ( const JsonDocument& document )
	{
		const QString& filePath = document.file_path ();

		return filePath.isEmpty () ? QObject::tr ( "Untitled" ) : QFileInfo ( filePath ).fileName ();
	}

	//=================================================================================================================
	// Constructors
	//=================================================================================================================

	FileController::FileController
	(
		JsonDocument*     document,
		UndoController*   undo,
		SettingsStore*    settings,
		SelectionService* selection,
		StatusService*    status,
		IDialogService*   dialogs,
		BackgroundIo*     io,
		QObject*          parent
	)
		: QObject   ( parent )
		, document  ( document )
		, undo      ( undo )
		, settings  ( settings )
		, selection ( selection )
		, status    ( status )
		, dialogs   ( dialogs )
		, io        ( io )
	{
	}

	//=================================================================================================================
	// Wiring
	//=================================================================================================================

	void FileController::set_view_departure_gate ( std::function<bool ()> gate )
	{
		viewDepartureGate = std::move ( gate );
	}

	//=================================================================================================================
	// Commands
	//=================================================================================================================

	bool FileController::new_document ()
	{
		// FILE-02. Both gates first, in that order (see the header): the pending edit reaches the document before the
		// dirty question is asked about it.

		if ( !leave_active_view () || !confirm_discard_changes () )
		{
			return false;
		}

		install_document ( DocumentIo::new_document (), QString () );

		status->show_message ( tr ( "New document" ), MESSAGE_TIMEOUT );

		return true;
	}

	bool FileController::open ()
	{
		// FILE-01. The gates run BEFORE the picker: a user who then cancels the picker has still been asked about their
		// unsaved work exactly once, and a user who cancels the SAVE prompt is not shown a picker at all.

		if ( !leave_active_view () || !confirm_discard_changes () )
		{
			return false;
		}

		const QString path = dialogs->choose_file_to_open ( tr ( "Open" ), file_filters::JSON, picker_start_path () );

		if ( path.isEmpty () )
		{
			return false;
		}

		return load_into_document ( path );
	}

	bool FileController::open_path ( const QString& path )
	{
		if ( path.isEmpty () )
		{
			return false;
		}

		if ( !leave_active_view () || !confirm_discard_changes () )
		{
			return false;
		}

		return load_into_document ( path );
	}

	bool FileController::save ()
	{
		// The view gate is what makes FILE-03's claim true for an uncommitted Code View edit: it commits first, so what
		// is written is what the view was showing (EDITOR-09).

		if ( !leave_active_view () )
		{
			return false;
		}

		if ( !document->has_root () )
		{
			return false;
		}

		// An untitled document has nowhere to save to yet, so Save IS Save As -- including its cancellation.

		if ( document->file_path ().isEmpty () )
		{
			return save_as ();
		}

		return write_document ( document->file_path () );
	}

	bool FileController::save_as ()
	{
		if ( !leave_active_view () )
		{
			return false;
		}

		if ( !document->has_root () )
		{
			return false;
		}

		const QString path = dialogs->choose_file_to_save ( tr ( "Save As" ), file_filters::JSON, picker_start_path () );

		if ( path.isEmpty () )
		{
			return false;
		}

		return write_document ( path );
	}

	bool FileController::close_document ()
	{
		if ( !leave_active_view () || !confirm_discard_changes () )
		{
			return false;
		}

		// No document at all, as distinct from an empty one: the title drops to "VJE", the tree empties, and the editor
		// pane has nothing to present (section 2.2).

		install_document ( nullptr, QString () );

		status->show_message ( tr ( "Closed" ), MESSAGE_TIMEOUT );

		return true;
	}

	//=================================================================================================================
	// Import / Export (FILE-11)
	//=================================================================================================================

	bool FileController::import_document ( const ConverterFormat& format )
	{
		if ( !format.supportsImport )
		{
			return false;
		}

		// An import replaces the document, so it answers to both gates exactly as Open does.

		if ( !leave_active_view () || !confirm_discard_changes () )
		{
			return false;
		}

		const QString path = dialogs->choose_file_to_open
		(
			tr ( "Import %1" ).arg ( format.displayName ),
			format.filters,
			picker_start_path ()
		);

		if ( path.isEmpty () )
		{
			return false;
		}

		if ( ioInProgress )
		{
			return false;
		}

		// The strategy and its toggle are read HERE, on the UI thread, so the conversion below touches no shared state
		// (SET-08, converters.hpp). For a format that prompts, this is the PRESELECTION rather than the answer.

		ImportOptions options = import_options ( settings );

		// READ AND CONVERT ARE TWO HAND-OFFS, not one, because FILE-13 puts a dialog between them: the strategy preview
		// needs the file's text, and the conversion needs the strategy. Every format takes the same route -- a format
		// with nothing to ask simply passes straight through the middle step -- rather than one route with a dialog and
		// one without, which is two orders for the read, the prompt, the conversion and the two failure reports.

		ioInProgress = true;

		QString text;
		QString readError;
		bool    read = false;

		io->run_and_wait ( [ &read, &text, &readError, &path ] ()
		{
			read = read_text_file ( path, &text, &readError );
		} );

		ioInProgress = false;

		if ( !read )
		{
			report_import_failure ( path, readError );

			return false;
		}

		// FILE-13: the strategy dialog, for the one format that has options. Cancel imports nothing -- and deliberately
		// leaves the settings alone, so a cancelled import cannot change what the next one preselects.

		if ( format.promptsForImportOptions )
		{
			XmlImportController prompt ( text, options, document_format_profile ( settings ) );

			if ( !dialogs->run_xml_import_dialog ( prompt, QFileInfo ( path ).fileName () ) )
			{
				return false;
			}

			options = prompt.options ();

			store_import_options ( settings, options );        // SET-08: preselected on the next import.
		}

		ioInProgress = true;

		ImportConversion conversion;

		io->run_and_wait ( [ &conversion, &text, &format, &options ] ()
		{
			conversion = import_text ( format, text, options );
		} );

		ioInProgress = false;

		if ( !conversion.ok )
		{
			report_import_failure ( path, conversion.error );

			return false;
		}

		// UNTITLED on purpose: the document came from a CSV / YAML / XML file, and Save must not write JSON over it. The
		// first Save therefore goes through Save As (FILE-03), and the source file stays out of Recent Files (FILE-05).

		install_document ( std::move ( conversion.root ), QString () );

		status->show_message ( tr ( "Imported %1" ).arg ( QFileInfo ( path ).fileName () ), MESSAGE_TIMEOUT );

		return true;
	}

	void FileController::report_import_failure ( const QString& path, const QString& reason )
	{
		const QString fileName = QFileInfo ( path ).fileName ();

		dialogs->show_error
		(
			tr ( "Import Failed" ),
			tr ( "Could not import \"%1\".\n\n%2" ).arg ( fileName, reason )
		);

		status->show_message ( tr ( "Failed to import %1" ).arg ( fileName ), MESSAGE_TIMEOUT );
	}

	bool FileController::export_document ( const ConverterFormat& format )
	{
		// The view gate only: an export neither replaces the document nor saves it, so there is nothing for FILE-08 to
		// ask about -- but a pending view edit must still reach the document first, or the export would write the
		// document as it was before it (EDITOR-09).

		if ( !leave_active_view () )
		{
			return false;
		}

		// Asked AFTER the view gate, and that order matters: a pending Code View commit can be the very edit that turns
		// the selection into an exportable array, so asking first would refuse an export that is about to be possible.

		const ExportSource source = export_source ( format );

		if ( source.blocker != ExportBlocker::None )
		{
			// The command was invoked and will do nothing, so it says why -- in both channels, because this is the one
			// outcome a user cannot see for themselves: the file they expected simply never appears (VAL-04, FILE-11).

			const QString reason = describe_export_blocker ( format, source.blocker );

			dialogs->show_error ( tr ( "Cannot Export %1" ).arg ( format.displayName ), reason );

			status->show_message ( reason, MESSAGE_TIMEOUT );

			return false;
		}

		const QString path = dialogs->choose_file_to_save
		(
			tr ( "Export %1" ).arg ( format.displayName ),
			format.filters,
			picker_start_path ()
		);

		if ( path.isEmpty () || ioInProgress )
		{
			return false;
		}

		ioInProgress = true;

		QString          writeError;
		bool             written = false;
		ExportConversion conversion;

		JsonNode* const sourceNode = source.node;

		io->run_and_wait ( [ &written, &writeError, &conversion, &path, &format, sourceNode ] ()
		{
			conversion = export_text ( format, *sourceNode );

			if ( conversion.ok )
			{
				written = write_text_file ( path, conversion.text, &writeError );
			}
		} );

		ioInProgress = false;

		if ( !conversion.ok || !written )
		{
			const QString reason = conversion.ok ? writeError : conversion.error;

			dialogs->show_error
			(
				tr ( "Export Failed" ),
				tr ( "Could not export to \"%1\".\n\n%2" ).arg ( QFileInfo ( path ).fileName (), reason )
			);

			status->show_message ( tr ( "Failed to export %1" ).arg ( QFileInfo ( path ).fileName () ), MESSAGE_TIMEOUT );

			return false;
		}

		// The document is unchanged by an export, so nothing about its dirty state or its path moves -- and an exported
		// file is not a JSON file the Recent Files list could reopen (FILE-05).
		//
		// The success carries the conversion's note when it has one -- what the format could not represent (FILE-11).
		// Appended to the same message rather than raised separately, because it is a property OF this export and a
		// second surface would make a completed command look like it had gone wrong.

		const QString exported = tr ( "Exported %1" ).arg ( QFileInfo ( path ).fileName () );

		status->show_message
		(
			conversion.note.isEmpty () ? exported : tr ( "%1 -- %2" ).arg ( exported, conversion.note ),
			MESSAGE_TIMEOUT
		);

		return true;
	}

	ExportSource FileController::export_source ( const ConverterFormat& format ) const
	{
		if ( !document->has_root () )
		{
			return { nullptr, ExportBlocker::NoDocument };
		}

		if ( !format.exportsSelection )
		{
			return { document->root (), node_export_blocker ( format, document->root () ) };
		}

		// CSV acts on the current tree selection (FILE-11), which may be nothing or may no longer resolve after an edit.
		// Both are the same thing to the user -- there is no array under the cursor -- so both answer NoSelection.

		if ( !selection->has_selection () )
		{
			return { nullptr, ExportBlocker::NoSelection };
		}

		JsonNode* const selected = document->resolve ( selection->selection () );

		if ( selected == nullptr )
		{
			return { nullptr, ExportBlocker::NoSelection };
		}

		const ExportBlocker blocker = node_export_blocker ( format, selected );

		// A node that cannot be exported is not a source: reporting it as one would let a caller act on a node the
		// blocker has already refused.

		return { ( blocker == ExportBlocker::None ) ? selected : nullptr, blocker };
	}

	bool FileController::confirm_discard_changes ()
	{
		if ( !document->has_root () || !document->is_dirty () )
		{
			return true;
		}

		switch ( dialogs->ask_save_changes ( document_display_name ( *document ) ) )
		{
			case SaveChangesAnswer::Save:
			{
				// A failed write, or a cancelled Save As, leaves the changes unsaved -- so the action that asked must be
				// abandoned rather than proceeding to discard them.

				return save ();
			}

			case SaveChangesAnswer::DontSave:
			{
				return true;
			}

			case SaveChangesAnswer::Cancel:
			{
				return false;
			}
		}

		return false;
	}

	//=================================================================================================================
	// Recent Files (FILE-05)
	//=================================================================================================================

	QStringList FileController::recent_files () const
	{
		return settings->value_string_list ( settings_keys::RECENT_FILES );
	}

	//=================================================================================================================
	// Helpers
	//=================================================================================================================

	bool FileController::leave_active_view ()
	{
		return !viewDepartureGate || viewDepartureGate ();
	}

	bool FileController::load_into_document ( const QString& path )
	{
		if ( ioInProgress )
		{
			return false;
		}

		ioInProgress = true;

		// The parse runs off the UI thread (NFR-04). Nothing is installed until it has finished and succeeded, so a
		// failure leaves the previous document exactly as it was (VAL-04).

		LoadResult result;

		io->run_and_wait ( [ &result, &path ] () { result = DocumentIo::load_file ( path ); } );

		ioInProgress = false;

		if ( !result.ok )
		{
			// Malformed or unreadable input is reported with its position and never crashes (FILE-06).

			dialogs->show_error
			(
				tr ( "Open Failed" ),
				tr ( "Could not open \"%1\".\n\n%2 (line %3, column %4)" )
					.arg ( QFileInfo ( path ).fileName (), result.error.message )
					.arg ( result.error.line )
					.arg ( result.error.column )
			);

			status->show_message ( tr ( "Failed to open %1" ).arg ( QFileInfo ( path ).fileName () ), MESSAGE_TIMEOUT );

			// A Recent Files entry that has since been moved or deleted would otherwise keep failing from the menu
			// (FILE-05). A file that exists but does not parse stays: the user may well be about to fix it.

			if ( !QFileInfo::exists ( path ) )
			{
				forget_recent_file ( path );
			}

			return false;
		}

		install_document ( std::move ( result.root ), path );

		// SET-03: duplicate keys are always kept; "Keep and warn" adds a non-blocking notice.

		if ( !result.duplicateKeys.empty () )
		{
			const QString onDuplicate = settings->value_string ( settings_keys::ON_DUPLICATE_KEYS, settings_values::ON_DUPLICATE_KEEP_SILENTLY );

			if ( onDuplicate == settings_values::ON_DUPLICATE_KEEP_AND_WARN )
			{
				status->show_message
				(
					tr ( "Loaded with %n duplicate key(s) preserved.", nullptr, static_cast<int> ( result.duplicateKeys.size () ) ),
					MESSAGE_TIMEOUT
				);

				return true;
			}
		}

		status->show_message ( tr ( "Opened %1" ).arg ( QFileInfo ( path ).fileName () ), MESSAGE_TIMEOUT );

		return true;
	}

	bool FileController::write_document ( const QString& path )
	{
		if ( ioInProgress || !document->has_root () )
		{
			return false;
		}

		// One profile for the Code View and for Save, read in one place, so "what the Code View shows is what saves" is
		// structural rather than a coincidence (FILE-03 / SET-07, settings_profiles.hpp).

		const FormatProfile profile = document_format_profile ( settings );

		ioInProgress = true;

		bool    written = false;
		QString errorMessage;

		io->run_and_wait ( [ this, &written, &errorMessage, &path, &profile ] ()
		{
			written = DocumentIo::save_file ( path, *document->root (), profile, &errorMessage );
		} );

		ioInProgress = false;

		if ( !written )
		{
			dialogs->show_error
			(
				tr ( "Save Failed" ),
				tr ( "Could not save \"%1\".\n\n%2" ).arg ( QFileInfo ( path ).fileName (), errorMessage )
			);

			status->show_message ( tr ( "Failed to save %1" ).arg ( QFileInfo ( path ).fileName () ), MESSAGE_TIMEOUT );

			return false;
		}

		// The saved state is the new undo baseline, so undoing back to it clears the modified indicator and redoing
		// away from it restores it (UNDO-04).

		document->set_file_path ( path );
		document->set_dirty ( false );
		undo->set_clean ();

		note_recent_file ( path );

		status->show_message ( tr ( "Saved %1" ).arg ( QFileInfo ( path ).fileName () ), MESSAGE_TIMEOUT );

		return true;
	}

	void FileController::install_document ( std::unique_ptr<JsonNode> root, const QString& path )
	{
		const bool hasRoot = ( root != nullptr );

		// set_root emits reset(), which is what re-projects the tree and refreshes the title and status panes.

		document->set_root ( std::move ( root ) );
		document->set_file_path ( path );
		document->set_dirty ( false );
		undo->clear ();

		if ( hasRoot )
		{
			selection->set_selection ( JsonPointer (), SelectionOrigin::Programmatic );
		}
		else
		{
			selection->clear ();
		}

		// Only a real file joins the list: an untitled New has no path, and an import's SOURCE file is not a JSON file
		// the list could reopen (FILE-05).

		if ( hasRoot && !path.isEmpty () )
		{
			note_recent_file ( path );
		}
	}

	void FileController::note_recent_file ( const QString& path )
	{
		const QString absolutePath = QFileInfo ( path ).absoluteFilePath ();

		QStringList recentFiles = settings->value_string_list ( settings_keys::RECENT_FILES );

		recentFiles.removeIf ( [ &absolutePath ] ( const QString& existing ) { return existing.compare ( absolutePath, Qt::CaseInsensitive ) == 0; } );
		recentFiles.prepend ( absolutePath );

		while ( recentFiles.size () > config::limits::RECENT_FILES )
		{
			recentFiles.removeLast ();
		}

		settings->set_string_list ( settings_keys::RECENT_FILES, recentFiles );

		emit recent_files_changed ();
	}

	void FileController::forget_recent_file ( const QString& path )
	{
		const QString absolutePath = QFileInfo ( path ).absoluteFilePath ();

		QStringList recentFiles = settings->value_string_list ( settings_keys::RECENT_FILES );

		if ( recentFiles.removeIf ( [ &absolutePath ] ( const QString& existing ) { return existing.compare ( absolutePath, Qt::CaseInsensitive ) == 0; } ) == 0 )
		{
			return;
		}

		settings->set_string_list ( settings_keys::RECENT_FILES, recentFiles );

		emit recent_files_changed ();
	}

	bool FileController::read_text_file ( const QString& path, QString* outText, QString* outError )
	{
		QFile file ( path );

		if ( !file.open ( QIODevice::ReadOnly ) )
		{
			*outError = file.errorString ();

			return false;
		}

		// UTF-8, BOM tolerated on input for the same reason JSON input is (FILE-07): the converters see text, not bytes.

		QString text = QString::fromUtf8 ( file.readAll () );

		if ( text.startsWith ( QChar ( 0xFEFF ) ) )
		{
			text.remove ( 0, 1 );
		}

		*outText = text;

		return true;
	}

	bool FileController::write_text_file ( const QString& path, const QString& text, QString* outError )
	{
		QFile file ( path );

		if ( !file.open ( QIODevice::WriteOnly | QIODevice::Truncate ) )
		{
			*outError = file.errorString ();

			return false;
		}

		// UTF-8 without a BOM, matching what Save writes (FILE-03). Line endings are the converter's own -- it produced
		// the text.

		const QByteArray bytes = text.toUtf8 ();

		if ( file.write ( bytes ) != bytes.size () )
		{
			*outError = file.errorString ();

			return false;
		}

		file.close ();

		return file.error () == QFile::NoError;
	}

	QString FileController::picker_start_path () const
	{
		// A file path opens the picker on that file's folder with the name filled in; a folder path just opens there.

		if ( !document->file_path ().isEmpty () )
		{
			return document->file_path ();
		}

		const QStringList recentFiles = recent_files ();

		if ( !recentFiles.isEmpty () )
		{
			return QFileInfo ( recentFiles.first () ).absolutePath ();
		}

		return QString ();
	}
}
