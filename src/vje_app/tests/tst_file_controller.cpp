//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Coverage for FileController -- the document lifecycle (FILE-01..09). This is the suite the IDialogService seam
//   exists for: every case here is a DECISION the user makes in a modal dialog, and behind the seam a fake answers it,
//   so the branches that would otherwise be reachable only by hand are ordinary assertions.
//
//   What is pinned:
//
//     - New / Open / Save / Save As / Close on a document, including Save-on-untitled becoming Save As and inheriting
//       its cancellation (FILE-02 / FILE-01 / FILE-03).
//     - The FILE-08 gate in all three answers -- Save (which must succeed before the discard proceeds), Don't Save, and
//       Cancel (which must ABANDON the command) -- and its silence on a clean document.
//     - The order of the two gates: the EDITOR-09 view gate runs first, so a view holding an uncommitted edit refuses
//       the command before the user is asked anything at all.
//     - FILE-06: a malformed file reports its position and leaves the open document untouched.
//     - FILE-05: recording, case-insensitive de-duplication, the cap, and dropping an entry whose file has gone.
//     - FILE-03's byte claim: what Save writes is DocumentIo's rendering through the document format profile, so the
//       Code View's text and the saved file cannot part company.
//     - FILE-13: the strategy dialog's place in the import pipeline -- preselected from the persisted choice, its
//       answer beating that preselection, its Cancel changing neither the document nor the setting, the read happening
//       BEFORE it (the preview needs the text), and a format with no options never seeing it at all.
//
//   Headless: the controller is Qt Core only, the dialogs are a fake, and the I/O runs through ImmediateIo.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "controllers/FileController.hpp"

#include "AppConfig.hpp"
#include "controllers/XmlImportController.hpp"
#include "controllers/converters.hpp"
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

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>
#include <optional>

using namespace vje;

namespace
{
	//-----------------------------------------------------------------------------------------------------------------
	// The scripted dialog service. Each prompt returns whatever the case set and records that it was asked -- the count
	// matters as much as the answer, since "asked exactly once" is part of what FILE-08 promises.
	//-----------------------------------------------------------------------------------------------------------------

	class FakeDialogService : public IDialogService
	{
	public:

		QString choose_file_to_open ( const QString&, const QString&, const QString& ) override
		{
			++openPickerCount;

			return openPickerAnswer;
		}

		QString choose_file_to_save ( const QString&, const QString&, const QString& ) override
		{
			++savePickerCount;

			return savePickerAnswer;
		}

		QString choose_folder ( const QString&, const QString& ) override
		{
			return folderPickerAnswer;
		}

		SaveChangesAnswer ask_save_changes ( const QString& documentName ) override
		{
			++saveChangesCount;

			lastDocumentName = documentName;

			return saveChangesAnswer;
		}

		void show_error ( const QString&, const QString& message ) override
		{
			++errorCount;

			lastErrorMessage = message;
		}

		void show_information ( const QString&, const QString& ) override
		{
			++informationCount;
		}

		bool confirm ( const QString&, const QString& ) override
		{
			++confirmCount;

			return confirmAnswer;
		}

		// FILE-13. The fake stands in for the whole dialog by doing what a user does in it: choose a strategy and
		// press Import or Cancel. That it can -- with no widget, no preview and no modal loop -- is the point of the
		// seam taking the CONTROLLER rather than a bag of values.

		bool run_xml_import_dialog ( XmlImportController& controller, const QString& fileName ) override
		{
			++xmlDialogCount;

			lastXmlFileName          = fileName;
			lastXmlPreselectedIndex  = controller.selected_index ();
			lastXmlPreselectedInfer  = controller.infer_scalar_types ();

			if ( xmlDialogChoice.has_value () )
			{
				controller.set_strategy           ( xmlDialogChoice->xmlStrategy );
				controller.set_infer_scalar_types ( xmlDialogChoice->xmlInferScalarTypes );
				controller.set_text_value_key     ( xmlDialogChoice->xmlTextValueKey );
			}

			return xmlDialogAccepts;
		}

		// FILE-12. Nothing in this suite prints, so both refuse -- but they are on the seam, so every implementation
		// of it has to answer them, and a fake that silently accepted would be inviting a file command to open a
		// print dialog no test could close.

		bool run_page_setup_dialog ( QPrinter& ) override
		{
			return false;
		}

		bool run_print_dialog ( QPrinter& ) override
		{
			return false;
		}

		// -- Scripted answers.

		QString           openPickerAnswer;
		QString           savePickerAnswer;
		QString           folderPickerAnswer;
		SaveChangesAnswer saveChangesAnswer = SaveChangesAnswer::Cancel;
		bool              confirmAnswer     = false;

		bool                         xmlDialogAccepts = false;
		std::optional<ImportOptions> xmlDialogChoice;               // Unset: leave the preselection alone.

		// -- Recorded traffic.

		int openPickerCount  = 0;
		int savePickerCount  = 0;
		int saveChangesCount = 0;
		int errorCount       = 0;
		int informationCount = 0;
		int confirmCount     = 0;
		int xmlDialogCount   = 0;

		QString lastErrorMessage;
		QString lastDocumentName;
		QString lastXmlFileName;
		int     lastXmlPreselectedIndex = -1;
		bool    lastXmlPreselectedInfer = false;
	};
}

class TestFileController : public QObject
{
	Q_OBJECT

private slots:

	void init ();
	void cleanup ();

	void a_new_document_is_a_clean_empty_object ();
	void opening_a_file_installs_it_and_records_it ();
	void a_malformed_file_is_reported_and_the_document_is_left_alone ();
	void a_recent_entry_whose_file_has_gone_is_dropped ();
	void a_clean_document_is_discarded_without_a_prompt ();
	void cancelling_the_dirty_prompt_abandons_the_command ();
	void answering_save_writes_before_discarding ();
	void answering_save_on_an_untitled_document_inherits_save_as_cancellation ();
	void answering_dont_save_discards_the_changes ();
	void save_writes_the_format_profile_bytes ();
	void save_as_adopts_the_new_path_and_clears_dirty ();
	void closing_empties_the_document_and_the_selection ();
	void the_view_gate_refuses_before_anything_is_asked ();
	void the_recent_list_is_deduplicated_case_insensitively_and_capped ();

	void an_import_produces_an_untitled_document_and_leaves_recent_files_alone ();
	void a_failed_import_reports_the_reason_and_keeps_the_document ();
	void an_export_writes_the_converted_text_without_touching_the_document ();
	void csv_export_acts_on_the_selection_and_reports_its_blockers ();
	void a_blocked_export_reports_the_reason_and_opens_no_picker ();
	void an_export_with_nested_values_says_it_was_lossy ();
	void the_persisted_xml_strategy_drives_an_xml_import ();

	void the_xml_dialog_is_preselected_from_settings_and_its_answer_wins ();
	void cancelling_the_xml_dialog_imports_nothing_and_forgets_nothing ();
	void a_format_with_no_options_is_never_prompted ();
	void an_unreadable_file_is_reported_before_the_dialog_opens ();

private:

	QString settings_path () const;
	QString sample_path ( const QString& fileName ) const;
	QString write_file ( const QString& fileName, const QString& contents ) const;

	// A document with a root and an edit on the stack, so it is genuinely dirty (rather than flagged so by hand).

	void load_dirty_document ( const QString& path );

	QTemporaryDir temporaryDirectory;

	std::unique_ptr<SettingsStore>    settings;
	std::unique_ptr<JsonDocument>     document;
	std::unique_ptr<UndoController>   undo;
	std::unique_ptr<SelectionService> selection;
	std::unique_ptr<StatusService>    status;
	std::unique_ptr<FakeDialogService> dialogs;
	std::unique_ptr<ImmediateIo>      io;
	std::unique_ptr<FileController>   controller;

	// Every message the controller posted, in order. VAL-04 makes the status bar half of what a command outcome IS, so
	// it is captured rather than ignored -- an assertion on the text is the only way to tell "reported the reason" from
	// "returned false and said nothing".

	QStringList statusMessages;
};

//---------------------------------------------------------------------------------------------------------------------
// Fixture
//---------------------------------------------------------------------------------------------------------------------

QString TestFileController::settings_path () const
{
	return temporaryDirectory.path () + QStringLiteral ( "/settings.json" );
}

QString TestFileController::sample_path ( const QString& fileName ) const
{
	return temporaryDirectory.path () + QLatin1Char ( '/' ) + fileName;
}

QString TestFileController::write_file ( const QString& fileName, const QString& contents ) const
{
	const QString path = sample_path ( fileName );

	QFile file ( path );

	if ( !file.open ( QIODevice::WriteOnly | QIODevice::Truncate ) )
	{
		return QString ();
	}

	file.write ( contents.toUtf8 () );
	file.close ();

	return path;
}

void TestFileController::init ()
{
	QVERIFY ( temporaryDirectory.isValid () );

	QFile::remove ( settings_path () );

	settings   = std::make_unique<SettingsStore> ( settings_path () );
	document   = std::make_unique<JsonDocument> ();
	undo       = std::make_unique<UndoController> ( document.get () );
	selection  = std::make_unique<SelectionService> ();
	status     = std::make_unique<StatusService> ();
	dialogs    = std::make_unique<FakeDialogService> ();
	io         = std::make_unique<ImmediateIo> ();

	statusMessages.clear ();

	QObject::connect
	(
		status.get (), &StatusService::message_posted,
		[ this ] ( const QString& text, int ) { statusMessages.append ( text ); }
	);

	controller = std::make_unique<FileController>
	(
		document.get (),
		undo.get (),
		settings.get (),
		selection.get (),
		status.get (),
		dialogs.get (),
		io.get ()
	);
}

void TestFileController::cleanup ()
{
	// Tear down in the REVERSE of construction order, and do it here rather than by letting init() reassign each member:
	// an undo stack outliving its document destroys commands that still hold nodes from it, which is a use-after-free
	// (and was a segfault on Linux before this existed).

	controller.reset ();
	io.reset ();
	dialogs.reset ();
	status.reset ();
	selection.reset ();
	undo.reset ();
	document.reset ();
	settings.reset ();
}

void TestFileController::load_dirty_document ( const QString& path )
{
	QVERIFY ( controller->open_path ( path ) );

	// A real edit, so the dirty flag comes from the undo stack's clean state as it does in the application (UNDO-04).

	QCOMPARE ( static_cast<int> ( undo->set_string ( JsonPointer::parse ( QStringLiteral ( "/name" ) ), QStringLiteral ( "edited" ) ) ),
	           static_cast<int> ( EditOutcome::Applied ) );

	QVERIFY ( document->is_dirty () );
}

//---------------------------------------------------------------------------------------------------------------------
// New / Open
//---------------------------------------------------------------------------------------------------------------------

void TestFileController::a_new_document_is_a_clean_empty_object ()
{
	QVERIFY ( controller->new_document () );

	QVERIFY  ( document->has_root () );
	QCOMPARE ( static_cast<int> ( document->root ()->kind () ), static_cast<int> ( JsonKind::Object ) );
	QCOMPARE ( document->root ()->member_count (), 0 );
	QVERIFY  ( document->file_path ().isEmpty () );
	QVERIFY  ( !document->is_dirty () );

	// The root is selected, so the panes have something to present (NAV-01).

	QVERIFY ( selection->has_selection () );
	QVERIFY ( selection->selection ().is_root () );

	// An untitled document has no path to remember (FILE-05).

	QVERIFY ( controller->recent_files ().isEmpty () );
}

void TestFileController::opening_a_file_installs_it_and_records_it ()
{
	const QString path = write_file ( QStringLiteral ( "doc.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	dialogs->openPickerAnswer = path;

	QVERIFY ( controller->open () );

	QCOMPARE ( dialogs->openPickerCount, 1 );
	QVERIFY  ( document->has_root () );
	QCOMPARE ( document->file_path (), path );
	QVERIFY  ( !document->is_dirty () );
	QVERIFY  ( !undo->can_undo () );

	QCOMPARE ( controller->recent_files ().size (), 1 );
	QCOMPARE ( QFileInfo ( controller->recent_files ().first () ).canonicalFilePath (), QFileInfo ( path ).canonicalFilePath () );
}

void TestFileController::a_malformed_file_is_reported_and_the_document_is_left_alone ()
{
	const QString good = write_file ( QStringLiteral ( "good.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );
	const QString bad  = write_file ( QStringLiteral ( "bad.json" ),  QStringLiteral ( "{ \"name\": }" ) );

	QVERIFY ( controller->open_path ( good ) );

	QVERIFY  ( !controller->open_path ( bad ) );
	QCOMPARE ( dialogs->errorCount, 1 );

	// FILE-06: the message names a position. And nothing was installed -- the good document is still open.

	QVERIFY  ( dialogs->lastErrorMessage.contains ( QStringLiteral ( "line" ) ) );
	QCOMPARE ( document->file_path (), good );
	QVERIFY  ( document->has_root () );

	// A file that exists but does not parse STAYS in the list: the user may be about to fix it.

	QCOMPARE ( controller->recent_files ().size (), 1 );
}

void TestFileController::a_recent_entry_whose_file_has_gone_is_dropped ()
{
	const QString path = write_file ( QStringLiteral ( "gone.json" ), QStringLiteral ( "{}" ) );

	QVERIFY  ( controller->open_path ( path ) );
	QCOMPARE ( controller->recent_files ().size (), 1 );

	QVERIFY ( QFile::remove ( path ) );

	QSignalSpy recentChanged ( controller.get (), &FileController::recent_files_changed );

	QVERIFY  ( !controller->open_path ( path ) );
	QCOMPARE ( dialogs->errorCount, 1 );

	// FILE-05: an entry that keeps failing from the menu is no longer a recent file.

	QVERIFY  ( controller->recent_files ().isEmpty () );
	QCOMPARE ( recentChanged.count (), 1 );
}

//---------------------------------------------------------------------------------------------------------------------
// The FILE-08 gate
//---------------------------------------------------------------------------------------------------------------------

void TestFileController::a_clean_document_is_discarded_without_a_prompt ()
{
	const QString path = write_file ( QStringLiteral ( "clean.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	QVERIFY ( controller->open_path ( path ) );
	QVERIFY ( controller->new_document () );

	QCOMPARE ( dialogs->saveChangesCount, 0 );
	QVERIFY  ( document->file_path ().isEmpty () );
}

void TestFileController::cancelling_the_dirty_prompt_abandons_the_command ()
{
	const QString path = write_file ( QStringLiteral ( "dirty.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	load_dirty_document ( path );

	dialogs->saveChangesAnswer = SaveChangesAnswer::Cancel;

	QVERIFY  ( !controller->new_document () );
	QCOMPARE ( dialogs->saveChangesCount, 1 );

	// Cancel means "abandon what you were doing", not "discard": the edited document is still open and still dirty.

	QCOMPARE ( document->file_path (), path );
	QVERIFY  ( document->is_dirty () );
	QCOMPARE ( dialogs->lastDocumentName, QStringLiteral ( "dirty.json" ) );

	// Every other discarding command answers the same way.

	QVERIFY ( !controller->open_path ( path ) );
	QVERIFY ( !controller->close_document () );
	QVERIFY ( document->has_root () );
}

void TestFileController::answering_save_writes_before_discarding ()
{
	const QString path = write_file ( QStringLiteral ( "save-first.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	load_dirty_document ( path );

	dialogs->saveChangesAnswer = SaveChangesAnswer::Save;

	QVERIFY ( controller->new_document () );

	// The edit reached the file before the document was replaced, and no picker was needed (it had a path).

	QCOMPARE ( dialogs->savePickerCount, 0 );

	const LoadResult reloaded = DocumentIo::load_file ( path );

	QVERIFY  ( reloaded.ok );
	QCOMPARE ( reloaded.root->find_member ( QStringLiteral ( "name" ) )->string_value (), QStringLiteral ( "edited" ) );

	// And the command it was gating actually happened.

	QVERIFY ( document->file_path ().isEmpty () );
	QVERIFY ( !document->is_dirty () );
}

void TestFileController::answering_save_on_an_untitled_document_inherits_save_as_cancellation ()
{
	QVERIFY ( controller->new_document () );

	QCOMPARE ( static_cast<int> ( undo->add_node ( JsonPointer (), JsonKind::String, QStringLiteral ( "greeting" ) ) ),
	           static_cast<int> ( EditOutcome::Applied ) );

	QVERIFY ( document->is_dirty () );

	dialogs->saveChangesAnswer = SaveChangesAnswer::Save;
	dialogs->savePickerAnswer  = QString ();                   // The user cancels the Save As picker.

	QVERIFY  ( !controller->close_document () );
	QCOMPARE ( dialogs->savePickerCount, 1 );

	// A cancelled Save As leaves the changes unsaved, so the close it was gating must not proceed.

	QVERIFY ( document->has_root () );
	QVERIFY ( document->is_dirty () );
}

void TestFileController::answering_dont_save_discards_the_changes ()
{
	const QString path = write_file ( QStringLiteral ( "discard.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	load_dirty_document ( path );

	dialogs->saveChangesAnswer = SaveChangesAnswer::DontSave;

	QVERIFY ( controller->close_document () );

	// The file still holds what it held before the edit.

	const LoadResult reloaded = DocumentIo::load_file ( path );

	QVERIFY  ( reloaded.ok );
	QCOMPARE ( reloaded.root->find_member ( QStringLiteral ( "name" ) )->string_value (), QStringLiteral ( "alex" ) );

	QVERIFY ( !document->has_root () );
}

//---------------------------------------------------------------------------------------------------------------------
// Save / Save As / Close
//---------------------------------------------------------------------------------------------------------------------

void TestFileController::save_writes_the_format_profile_bytes ()
{
	const QString path = write_file ( QStringLiteral ( "profile.json" ), QStringLiteral ( "{ \"a\": { \"b\": 1 } }" ) );

	QVERIFY ( controller->open_path ( path ) );

	// A non-default profile, so the assertion cannot pass by coincidence (SET-07).

	settings->set_string ( settings_keys::CODE_INDENT_KIND,  settings_values::INDENT_TABS );
	settings->set_string ( settings_keys::CODE_BRACE_STYLE,  settings_values::BRACE_STYLE_K_AND_R );

	QVERIFY ( controller->save () );

	QFile saved ( path );

	QVERIFY ( saved.open ( QIODevice::ReadOnly ) );

	const QByteArray onDisk = saved.readAll ();

	// FILE-03 / EDITOR-07: the bytes are the document rendered through the ONE format profile the Code View also uses.

	QCOMPARE ( onDisk, DocumentIo::save_bytes ( *document->root (), document_format_profile ( settings.get () ) ) );
}

void TestFileController::save_as_adopts_the_new_path_and_clears_dirty ()
{
	const QString original = write_file ( QStringLiteral ( "original.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	load_dirty_document ( original );

	const QString destination = sample_path ( QStringLiteral ( "copy.json" ) );

	dialogs->savePickerAnswer = destination;

	QVERIFY ( controller->save_as () );

	QCOMPARE ( document->file_path (), destination );
	QVERIFY  ( !document->is_dirty () );
	QVERIFY  ( undo->is_clean () );

	// Both files are in the recent list, the destination first (FILE-05).

	const QStringList recentFiles = controller->recent_files ();

	QCOMPARE ( recentFiles.size (), 2 );
	QCOMPARE ( QFileInfo ( recentFiles.first () ).fileName (), QStringLiteral ( "copy.json" ) );

	// The original is untouched: Save As redirects the document rather than moving it.

	const LoadResult reloaded = DocumentIo::load_file ( original );

	QVERIFY  ( reloaded.ok );
	QCOMPARE ( reloaded.root->find_member ( QStringLiteral ( "name" ) )->string_value (), QStringLiteral ( "alex" ) );
}

void TestFileController::closing_empties_the_document_and_the_selection ()
{
	const QString path = write_file ( QStringLiteral ( "close.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	QVERIFY ( controller->open_path ( path ) );
	QVERIFY ( controller->close_document () );

	QVERIFY ( !document->has_root () );
	QVERIFY ( document->file_path ().isEmpty () );
	QVERIFY ( !document->is_dirty () );
	QVERIFY ( !selection->has_selection () );
	QVERIFY ( !undo->can_undo () );

	// With no document there is nothing to save.

	QVERIFY ( !controller->save () );
	QVERIFY ( !controller->save_as () );
}

//---------------------------------------------------------------------------------------------------------------------
// The gate order, and the recent list's shape
//---------------------------------------------------------------------------------------------------------------------

void TestFileController::the_view_gate_refuses_before_anything_is_asked ()
{
	const QString path = write_file ( QStringLiteral ( "gated.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	load_dirty_document ( path );

	// EDITOR-09: the user chose to keep editing an invalid Code View edit.

	controller->set_view_departure_gate ( [] () { return false; } );

	QVERIFY ( !controller->new_document () );
	QVERIFY ( !controller->open () );
	QVERIFY ( !controller->open_path ( path ) );
	QVERIFY ( !controller->save () );
	QVERIFY ( !controller->save_as () );
	QVERIFY ( !controller->close_document () );

	// Nothing was asked and no picker was opened: the view gate is FIRST, so the dirty question about a document whose
	// latest edit has not reached it yet is never put.

	QCOMPARE ( dialogs->saveChangesCount, 0 );
	QCOMPARE ( dialogs->openPickerCount, 0 );
	QCOMPARE ( dialogs->savePickerCount, 0 );

	QVERIFY ( document->is_dirty () );
}

void TestFileController::the_recent_list_is_deduplicated_case_insensitively_and_capped ()
{
	// One more file than the cap, so the oldest is pushed off the end.

	const int fileCount = config::limits::RECENT_FILES + 1;

	for ( int index = 0; index < fileCount; ++index )
	{
		const QString path = write_file ( QStringLiteral ( "file-%1.json" ).arg ( index ), QStringLiteral ( "{}" ) );

		QVERIFY ( controller->open_path ( path ) );
	}

	QStringList recentFiles = controller->recent_files ();

	QCOMPARE ( recentFiles.size (), config::limits::RECENT_FILES );
	QCOMPARE ( QFileInfo ( recentFiles.first () ).fileName (), QStringLiteral ( "file-%1.json" ).arg ( fileCount - 1 ) );
	QVERIFY  ( !recentFiles.contains ( QFileInfo ( sample_path ( QStringLiteral ( "file-0.json" ) ) ).absoluteFilePath () ) );

	// De-duplication ignores case, which is what a Windows path demands and is harmless elsewhere. The stored entry is
	// upper-cased in place rather than opened through an upper-cased path, so the case-insensitive COMPARISON is what is
	// under test and not the filesystem's own case rules (a Linux one would simply fail to find the file).

	const QString reopened = sample_path ( QStringLiteral ( "file-1.json" ) );
	const QString stored   = QFileInfo ( reopened ).absoluteFilePath ();

	recentFiles.replaceInStrings ( stored, stored.toUpper () );

	settings->set_string_list ( settings_keys::RECENT_FILES, recentFiles );

	QVERIFY ( controller->open_path ( reopened ) );

	recentFiles = controller->recent_files ();

	QCOMPARE ( recentFiles.size (), config::limits::RECENT_FILES );
	QCOMPARE ( recentFiles.first (), stored );
	QVERIFY  ( !recentFiles.contains ( stored.toUpper () ) );
}

//---------------------------------------------------------------------------------------------------------------------
// Import / Export (FILE-11)
//---------------------------------------------------------------------------------------------------------------------

void TestFileController::an_import_produces_an_untitled_document_and_leaves_recent_files_alone ()
{
	const QString path = write_file ( QStringLiteral ( "people.csv" ), QStringLiteral ( "name,age\nalex,7\nblake,9\n" ) );

	dialogs->openPickerAnswer = path;

	const ConverterFormat* const csv = find_converter ( QStringLiteral ( "csv" ) );

	QVERIFY ( csv != nullptr );
	QVERIFY ( controller->import_document ( *csv ) );

	// The CSV became a root array of flat objects, and the document is UNTITLED -- Save must not write JSON over a .csv,
	// so it has no path to save to yet (FILE-11 / FILE-03).

	QVERIFY  ( document->has_root () );
	QCOMPARE ( static_cast<int> ( document->root ()->kind () ), static_cast<int> ( JsonKind::Array ) );
	QCOMPARE ( document->root ()->array_size (), 2 );
	QVERIFY  ( document->file_path ().isEmpty () );
	QVERIFY  ( !document->is_dirty () );

	// FILE-05: an import SOURCE is not a JSON file the list could reopen.

	QVERIFY ( controller->recent_files ().isEmpty () );
}

void TestFileController::a_failed_import_reports_the_reason_and_keeps_the_document ()
{
	const QString json = write_file ( QStringLiteral ( "kept.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	QVERIFY ( controller->open_path ( json ) );

	// An empty CSV has no header row, which the codec refuses.

	dialogs->openPickerAnswer = write_file ( QStringLiteral ( "empty.csv" ), QString () );

	const ConverterFormat* const csv = find_converter ( QStringLiteral ( "csv" ) );

	QVERIFY   ( csv != nullptr );
	QVERIFY   ( !controller->import_document ( *csv ) );
	QCOMPARE  ( dialogs->errorCount, 1 );

	// Nothing was installed: the open document survives a failed import untouched (VAL-04).

	QCOMPARE ( document->file_path (), json );
	QVERIFY  ( document->has_root () );
}

void TestFileController::an_export_writes_the_converted_text_without_touching_the_document ()
{
	const QString path = write_file ( QStringLiteral ( "export-me.json" ), QStringLiteral ( "{ \"name\": \"alex\" }" ) );

	load_dirty_document ( path );

	const QString destination = sample_path ( QStringLiteral ( "out.yaml" ) );

	dialogs->savePickerAnswer = destination;

	const ConverterFormat* const yaml = find_converter ( QStringLiteral ( "yaml" ) );

	QVERIFY ( yaml != nullptr );
	QVERIFY ( controller->export_document ( *yaml ) );

	QFile written ( destination );

	QVERIFY ( written.open ( QIODevice::ReadOnly ) );

	const QString text = QString::fromUtf8 ( written.readAll () );

	QCOMPARE ( text, export_text ( *yaml, *document->root () ).text );

	// An export is not a save: the document keeps its path and its unsaved changes, and the exported file is not a
	// recent file (FILE-05).

	QCOMPARE ( document->file_path (), path );
	QVERIFY  ( document->is_dirty () );
	QCOMPARE ( controller->recent_files ().size (), 1 );
	QCOMPARE ( dialogs->saveChangesCount, 0 );
}

void TestFileController::csv_export_acts_on_the_selection_and_reports_its_blockers ()
{
	// Four selections on one document: a root object (no rows to make records from), an empty array (nothing to write),
	// a ragged array and a scalar array -- the last two exportable since FILE-11 was relaxed.

	const QString path = write_file
	(
		QStringLiteral ( "tables.json" ),
		QStringLiteral ( "{ \"uniform\": [ { \"a\": 1, \"b\": 2 }, { \"a\": 3, \"b\": 4 } ],"
		                 "  \"ragged\":  [ { \"a\": 1 }, { \"b\": 2 } ],"
		                 "  \"tags\":    [ \"red\", \"green\" ],"
		                 "  \"empty\":   [] }" )
	);

	QVERIFY ( controller->open_path ( path ) );

	const ConverterFormat* const csv = find_converter ( QStringLiteral ( "csv" ) );

	QVERIFY ( csv != nullptr );

	// The root object has no rows, and the blocker says so rather than the command simply not being there.

	QCOMPARE ( static_cast<int> ( controller->export_source ( *csv ).blocker ),
	           static_cast<int> ( ExportBlocker::NotAnArray ) );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/empty" ) ), SelectionOrigin::Programmatic );

	QCOMPARE ( static_cast<int> ( controller->export_source ( *csv ).blocker ),
	           static_cast<int> ( ExportBlocker::EmptyArray ) );

	// Both of the relaxed shapes are now sources rather than blockers.

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/ragged" ) ), SelectionOrigin::Programmatic );

	QCOMPARE ( static_cast<int> ( controller->export_source ( *csv ).blocker ), static_cast<int> ( ExportBlocker::None ) );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/tags" ) ), SelectionOrigin::Programmatic );

	QCOMPARE ( static_cast<int> ( controller->export_source ( *csv ).blocker ), static_cast<int> ( ExportBlocker::None ) );

	// And the export writes THAT array rather than the document -- here the scalar one, headed by its own key.

	const QString destination = sample_path ( QStringLiteral ( "tags.csv" ) );

	dialogs->savePickerAnswer = destination;

	QVERIFY ( controller->export_document ( *csv ) );

	QFile written ( destination );

	QVERIFY  ( written.open ( QIODevice::ReadOnly ) );
	QCOMPARE ( QString::fromUtf8 ( written.readAll () ), QStringLiteral ( "tags\r\nred\r\ngreen" ) );
}

void TestFileController::a_blocked_export_reports_the_reason_and_opens_no_picker ()
{
	const QString path = write_file
	(
		QStringLiteral ( "doc.json" ),
		QStringLiteral ( "{ \"name\": \"alex\" }" )
	);

	QVERIFY ( controller->open_path ( path ) );

	const ConverterFormat* const csv = find_converter ( QStringLiteral ( "csv" ) );

	QVERIFY ( csv != nullptr );

	// The root object is selected by the load (NAV-01), so CSV has nothing to make records from.

	const int pickerCount = dialogs->savePickerCount;

	QVERIFY ( !controller->export_document ( *csv ) );

	// The picker never opens -- there is nothing to write, so asking where to write it would be a wasted question.

	QCOMPARE ( dialogs->savePickerCount, pickerCount );

	// BOTH channels carry the reason (VAL-04): the dialog, because the user invoked a command that then did nothing at
	// all, and the status bar, because that is where every other file outcome lands and where the diagnostic log taps.

	QCOMPARE ( dialogs->errorCount, 1 );
	QCOMPARE ( dialogs->lastErrorMessage, describe_export_blocker ( *csv, ExportBlocker::NotAnArray ) );
	QCOMPARE ( statusMessages.last (), describe_export_blocker ( *csv, ExportBlocker::NotAnArray ) );

	// The reason names the fix, not just the fault.

	QVERIFY ( dialogs->lastErrorMessage.contains ( QStringLiteral ( "Select an array" ) ) );
}

void TestFileController::an_export_with_nested_values_says_it_was_lossy ()
{
	const QString path = write_file
	(
		QStringLiteral ( "nested.json" ),
		QStringLiteral ( "{ \"rows\": [ { \"id\": 1, \"tags\": [ \"a\" ] }, { \"id\": 2, \"tags\": { \"x\": 1 } } ] }" )
	);

	QVERIFY ( controller->open_path ( path ) );

	const ConverterFormat* const csv = find_converter ( QStringLiteral ( "csv" ) );

	QVERIFY ( csv != nullptr );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/rows" ) ), SelectionOrigin::Programmatic );

	const QString destination = sample_path ( QStringLiteral ( "nested.csv" ) );

	dialogs->savePickerAnswer = destination;

	QVERIFY ( controller->export_document ( *csv ) );

	// The export SUCCEEDS -- the user asked for a CSV of a nested array and got one -- and the success message carries
	// what it cost, on the same line rather than as a second surface that would read as a failure.

	QFile written ( destination );

	QVERIFY  ( written.open ( QIODevice::ReadOnly ) );
	QCOMPARE ( QString::fromUtf8 ( written.readAll () ), QStringLiteral ( "id,tags\r\n1,[...]\r\n2,{...}" ) );

	QCOMPARE ( dialogs->errorCount, 0 );
	QVERIFY  ( statusMessages.last ().contains ( QStringLiteral ( "Exported nested.csv" ) ) );
	QVERIFY  ( statusMessages.last ().contains ( QStringLiteral ( "2 nested values" ) ) );

	// A clean export says nothing extra, so the note stays a signal rather than boilerplate.

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/rows/0/id" ) ), SelectionOrigin::Programmatic );
	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/rows" ) ), SelectionOrigin::Programmatic );

	const QString flatPath = write_file ( QStringLiteral ( "flat.json" ), QStringLiteral ( "{ \"rows\": [ { \"id\": 1 } ] }" ) );

	QVERIFY ( controller->open_path ( flatPath ) );

	selection->set_selection ( JsonPointer::parse ( QStringLiteral ( "/rows" ) ), SelectionOrigin::Programmatic );

	dialogs->savePickerAnswer = sample_path ( QStringLiteral ( "flat.csv" ) );

	QVERIFY  ( controller->export_document ( *csv ) );
	QCOMPARE ( statusMessages.last (), QStringLiteral ( "Exported flat.csv" ) );
}

void TestFileController::the_persisted_xml_strategy_drives_an_xml_import ()
{
	const QString path = write_file
	(
		QStringLiteral ( "doc.xml" ),
		QStringLiteral ( "<root version=\"2\"><name>alex</name></root>" )
	);

	dialogs->openPickerAnswer = path;

	// FILE-13 now puts the strategy dialog between the picker and the conversion. This case is about what the
	// PERSISTED value does, so the dialog is answered Import with the preselection left exactly as it arrived.

	dialogs->xmlDialogAccepts = true;

	const ConverterFormat* const xml = find_converter ( QStringLiteral ( "xml" ) );

	QVERIFY ( xml != nullptr );

	// The default (SET-08's "(Recommended)"): attributes become plain members.

	QVERIFY ( controller->import_document ( *xml ) );

	const JsonNode* const defaultRoot = document->root ()->find_member ( QStringLiteral ( "root" ) );

	QVERIFY ( defaultRoot != nullptr );
	QVERIFY ( defaultRoot->find_member ( QStringLiteral ( "version" ) ) != nullptr );

	// The persisted choice is honoured on the next import: BadgerFish prefixes attributes with "@".

	settings->set_string ( settings_keys::XML_IMPORT_STRATEGY, settings_values::XML_STRATEGY_BADGER_FISH );

	QVERIFY ( controller->import_document ( *xml ) );

	const JsonNode* const badgerFishRoot = document->root ()->find_member ( QStringLiteral ( "root" ) );

	QVERIFY ( badgerFishRoot != nullptr );
	QVERIFY ( badgerFishRoot->find_member ( QStringLiteral ( "@version" ) ) != nullptr );
	QVERIFY ( badgerFishRoot->find_member ( QStringLiteral ( "version" ) ) == nullptr );

	// Infer scalar types is off by default, so an integer attribute is still a string (FILE-13).

	QCOMPARE ( static_cast<int> ( badgerFishRoot->find_member ( QStringLiteral ( "@version" ) )->kind () ),
	           static_cast<int> ( JsonKind::String ) );
}

//---------------------------------------------------------------------------------------------------------------------
// FILE-13 -- the strategy dialog in the pipeline
//---------------------------------------------------------------------------------------------------------------------

void TestFileController::the_xml_dialog_is_preselected_from_settings_and_its_answer_wins ()
{
	const QString path = write_file
	(
		QStringLiteral ( "doc.xml" ),
		QStringLiteral ( "<root version=\"2\"><name>alex</name></root>" )
	);

	dialogs->openPickerAnswer = path;

	// What the user last chose is what the dialog opens on (SET-08) ...

	settings->set_string ( settings_keys::XML_IMPORT_STRATEGY,    settings_values::XML_STRATEGY_GROUPED_ATTRIBUTES );
	settings->set_bool   ( settings_keys::XML_INFER_SCALAR_TYPES, true );

	// ... and what they choose IN it is what the import uses, which is the whole point of the dialog existing.

	ImportOptions chosen;

	chosen.xmlStrategy         = XmlImportStrategyKind::BadgerFish;
	chosen.xmlInferScalarTypes = true;

	dialogs->xmlDialogChoice  = chosen;
	dialogs->xmlDialogAccepts = true;

	const ConverterFormat* const xml = find_converter ( QStringLiteral ( "xml" ) );

	QVERIFY ( xml != nullptr );
	QVERIFY ( controller->import_document ( *xml ) );

	QCOMPARE ( dialogs->xmlDialogCount, 1 );
	QCOMPARE ( dialogs->lastXmlFileName, QStringLiteral ( "doc.xml" ) );

	QCOMPARE ( dialogs->lastXmlPreselectedIndex,
	           XmlImportController::index_of_strategy ( XmlImportStrategyKind::GroupedAttributes ) );

	QCOMPARE ( dialogs->lastXmlPreselectedInfer, true );

	const JsonNode* const root = document->root ()->find_member ( QStringLiteral ( "root" ) );

	QVERIFY ( root != nullptr );
	QVERIFY ( root->find_member ( QStringLiteral ( "@version" ) ) != nullptr );   // BadgerFish, not the preselection.

	// Infer scalar types travelled with the choice: "2" is a JSON integer.

	QCOMPARE ( static_cast<int> ( root->find_member ( QStringLiteral ( "@version" ) )->kind () ),
	           static_cast<int> ( JsonKind::Number ) );

	// And the choice is now the preselection for next time (SET-08).

	QCOMPARE ( settings->value_string ( settings_keys::XML_IMPORT_STRATEGY, QString () ),
	           settings_values::XML_STRATEGY_BADGER_FISH );
}

void TestFileController::cancelling_the_xml_dialog_imports_nothing_and_forgets_nothing ()
{
	const QString jsonPath = write_file ( QStringLiteral ( "kept.json" ), QStringLiteral ( "{\"kept\":true}" ) );

	dialogs->openPickerAnswer = jsonPath;

	QVERIFY ( controller->open () );

	const QString xmlPath = write_file ( QStringLiteral ( "doc.xml" ), QStringLiteral ( "<root/>" ) );

	dialogs->openPickerAnswer = xmlPath;

	settings->set_string ( settings_keys::XML_IMPORT_STRATEGY, settings_values::XML_STRATEGY_CUSTOM_FLATTENED );

	ImportOptions abandoned;

	abandoned.xmlStrategy = XmlImportStrategyKind::BadgerFish;

	dialogs->xmlDialogChoice  = abandoned;
	dialogs->xmlDialogAccepts = false;                         // Cancel.

	const ConverterFormat* const xml = find_converter ( QStringLiteral ( "xml" ) );

	QVERIFY ( xml != nullptr );
	QVERIFY ( !controller->import_document ( *xml ) );

	QCOMPARE ( dialogs->xmlDialogCount, 1 );
	QCOMPARE ( dialogs->errorCount, 0 );                       // A Cancel is not a failure.

	// The document the user was working on is untouched ...

	QVERIFY ( document->root () != nullptr );
	QVERIFY ( document->root ()->find_member ( QStringLiteral ( "kept" ) ) != nullptr );

	// ... and so is the persisted preselection: a cancelled import must not change what the next one opens on, even
	// though the controller it was handed did have a different strategy set on it.

	QCOMPARE ( settings->value_string ( settings_keys::XML_IMPORT_STRATEGY, QString () ),
	           settings_values::XML_STRATEGY_CUSTOM_FLATTENED );
}

void TestFileController::a_format_with_no_options_is_never_prompted ()
{
	const QString path = write_file ( QStringLiteral ( "doc.yaml" ), QStringLiteral ( "name: alex\n" ) );

	dialogs->openPickerAnswer = path;
	dialogs->xmlDialogAccepts = true;                          // Would accept, if it were ever asked.

	const ConverterFormat* const yaml = find_converter ( QStringLiteral ( "yaml" ) );

	QVERIFY ( yaml != nullptr );
	QVERIFY ( controller->import_document ( *yaml ) );

	QCOMPARE ( dialogs->xmlDialogCount, 0 );
}

void TestFileController::an_unreadable_file_is_reported_before_the_dialog_opens ()
{
	// The read comes first because the preview needs the file's text; a file that cannot be read has nothing to
	// preview, so the failure is reported and the dialog never appears.

	dialogs->openPickerAnswer = sample_path ( QStringLiteral ( "absent.xml" ) );
	dialogs->xmlDialogAccepts = true;

	const ConverterFormat* const xml = find_converter ( QStringLiteral ( "xml" ) );

	QVERIFY ( xml != nullptr );
	QVERIFY ( !controller->import_document ( *xml ) );

	QCOMPARE ( dialogs->xmlDialogCount, 0 );
	QCOMPARE ( dialogs->errorCount, 1 );
}

QTEST_MAIN ( TestFileController )

#include "tst_file_controller.moc"
