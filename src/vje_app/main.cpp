//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   Application entry point and composition root. It:
//
//     - Reads the command line FIRST, before any application object exists (architecture.md section 13.2): the
//       arguments as Unicode (platform/process_arguments, CLI-02), and one launch_plan over them saying whether this
//       invocation runs a command, opens the window, or is a usage error. vje.com, the Windows console twin, asks the
//       same plan, so a terminal and a double click cannot disagree about a line.
//     - A command or a usage error runs WITHOUT A WINDOW, under QCoreApplication -- no display needed, so it works over
//       SSH and in CI (CLI-03) -- and main returns its exit code (section 2.13).
//     - Otherwise constructs the QApplication, stamps its identity (so QStandardPaths resolves the config location),
//       instantiates the vje_core services (document, undo) and the vje_app services (settings, theme, selection,
//       status), applies the persisted theme, injects everything into MainWindow by constructor, brings Windows
//       Explorer's entries into line with SET-15 (FILE-15), and opens the file the plan names through FILE-01's path.
//
//   There is no service locator: dependencies are passed explicitly, keeping the window and future controllers
//   testable with fakes. The services are stack-owned here and outlive the window (which is constructed last and
//   destroyed first).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include "MainWindow.hpp"

#include "dialogs/MessageBox.hpp"
#include "services/BackgroundIo.hpp"
#include "services/ClipboardService.hpp"
#include "services/ExplorerIntegrationSync.hpp"
#include "services/IconLibrary.hpp"
#include "services/SelectionService.hpp"
#include "services/settings_profiles.hpp"
#include "services/StatusService.hpp"
#include "services/ThemeService.hpp"
#include "services/TitleBarSync.hpp"

#include <vje_cli/CommandRegistry.hpp>
#include <vje_cli/ConsoleCliContext.hpp>
#include <vje_cli/application_identity.hpp>
#include <vje_cli/commands/VersionCommand.hpp>
#include <vje_cli/launch_plan.hpp>
#include <vje_cli/window_program.hpp>

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/version.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <platform/process_arguments.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QString>

namespace
{
	// The compiled-in application icon (src/vje_app/CMakeLists.txt, prefix /vje/app).

	constexpr auto APPLICATION_ICON_DIRECTORY = ":/vje/app";

	//-----------------------------------------------------------------------------------------------------------------
	// The window and taskbar icon, built from EVERY master in the resource directory rather than from a named file.
	//
	// The artwork is hand-drawn raster (2026-08-13), so one file answers one size, while an application icon is asked
	// for at sizes nobody here chooses -- a title bar, a taskbar, a file listing, an Alt-Tab switcher. QIcon picks the
	// nearest of whatever it holds and scales, so reading the directory means drawing a second size is a drop-in: no
	// call site learns how many masters there are. Today there is one.
	//
	// It is never re-tinted and belongs to neither icon family (IconLibrary): the OS composites it as-is against an
	// arbitrary desktop background, so it commits to its own colours.
	//-----------------------------------------------------------------------------------------------------------------

	QIcon application_icon ()
	{
		QIcon icon;

		const QDir directory ( QString::fromLatin1 ( APPLICATION_ICON_DIRECTORY ) );

		for ( const QString& fileName : directory.entryList ( QDir::Files, QDir::Name ) )
		{
			icon.addFile ( directory.filePath ( fileName ) );
		}

		return icon;
	}

#if defined ( Q_OS_WIN )

	//-----------------------------------------------------------------------------------------------------------------
	// A command's answer, shown in a message box: vje.exe's route when it has nowhere to print (CLI-07).
	//
	// Help is laid out in aligned columns, so the text is set preformatted -- in a proportional font the synopsis
	// column would wander. A usage error is a Warning and help or the version Information, which is the one thing a
	// glance at the box should tell.
	//
	// It is a MessageBox like every other (STYLE-18 (6)), under the application's look with its DEFAULTS -- Fluent,
	// Light -- because a command reads no preference (CLI-03), the theme included. Without the look it would be Qt's
	// native Windows style, which has neither FluentStyle's button order nor the application's palette.
	//-----------------------------------------------------------------------------------------------------------------

	void show_answer_in_message_box ( const vje::cli::LaunchPlan& plan, const QString& text )
	{
		vje::ThemeService::apply_look ( vje::config::appearance::DEFAULT_INTERFACE_STYLE, false );

		// Its title bar too (STYLE-19): a step darker than the box. Light already, because Qt takes a window's dark or
		// light title bar from its palette, and the palette just applied is light.

		vje::TitleBarSync titleBars;

		const vje::MessageKind kind = ( plan.kind == vje::cli::LaunchPlan::Kind::UsageError )
		                            ? vje::MessageKind::Warning
		                            : vje::MessageKind::Information;

		vje::MessageBox box ( kind, vje::application_name (), QString (), QMessageBox::Ok, QMessageBox::Ok, QMessageBox::Ok );

		// The version is centred both ways (STYLE-18 (2)): a single line that IS the whole box, and reads as a caption
		// rather than as a report. Help and a usage error keep the default top-left -- help is aligned columns, and
		// centring would centre each line on its own and break them.

		if ( dynamic_cast<const vje::cli::VersionCommand*> ( plan.command ) != nullptr )
		{
			box.set_text_alignment ( vje::HorizontalTextAlignment::Centre, vje::VerticalTextAlignment::Centre );
		}

		box.setTextFormat ( Qt::RichText );
		box.setText       ( QStringLiteral ( "<pre>" ) + text.trimmed ().toHtmlEscaped () + QStringLiteral ( "</pre>" ) );
		box.exec ();
	}

#endif

	//-----------------------------------------------------------------------------------------------------------------
	// Run a command, or report a usage error, with no window (CLI-03). Returns the process exit code.
	//
	// ON WINDOWS vje.exe IS A WINDOW PROGRAM, and launched from Explorer or a shortcut it has no standard handles to
	// write through. It still runs the command and returns the code -- an installer can call either executable -- and
	// what it could not print is kept by the context rather than dropped. If that text IS the answer (help, the
	// version, a usage error) it is shown in a message box; a report a script would read is discarded, since a box
	// listing a hundred files helps nobody who launched vje.exe without a terminal. Wherever the write DOES arrive -- a
	// console, a pipe, a file -- nothing is undelivered and no box appears, so `vje.exe --version > out.txt` works and
	// a test driving vje.exe through pipes never meets a modal it cannot dismiss.
	//
	// THE APPLICATION OBJECT IS CHOSEN BY THE KIND OF OUTPUT, before anything is written. A box needs QApplication
	// while a command otherwise runs under QCoreApplication, and a process gets one of the two -- so every plan whose
	// output could end up in a box takes QApplication (Windows always has a display), and the reports take
	// QCoreApplication. Whether the box actually appears is then decided by the WRITE, after the fact, rather than by
	// guessing beforehand whether a handle leads anywhere (platform/console_output.hpp). On Linux the whole branch is
	// absent: a write there always has somewhere to go.
	//-----------------------------------------------------------------------------------------------------------------

	int run_without_window ( int& argc, char* argv [], const vje::cli::LaunchPlan& plan, const vje::cli::CommandRegistry& registry )
	{
#if defined ( Q_OS_WIN )

		if ( vje::cli::output_is_the_answer ( plan ) )
		{
			QApplication application ( argc, argv );

			vje::cli::apply_application_identity ();

			QApplication::setWindowIcon ( application_icon () );

			vje::cli::ConsoleCliContext context;

			const vje::cli::ExitCode code = vje::cli::execute ( plan, registry, context );

			if ( !context.undelivered_text ().isEmpty () )
			{
				show_answer_in_message_box ( plan, context.undelivered_text () );
			}

			return vje::cli::exit_status ( code );
		}

#endif

		QCoreApplication application ( argc, argv );

		vje::cli::apply_application_identity ();

		vje::cli::ConsoleCliContext context;

		return vje::cli::exit_status ( vje::cli::execute ( plan, registry, context ) );
	}
}

int main ( int argc, char* argv [] )
{
	// THE PLAN FIRST, before any application object -- which object to construct is the first thing it decides. The
	// arguments are read as Unicode, so the file a window launch is handed is the file the user named (CLI-02).

	const QStringList                arguments = vje::platform::command_line_arguments ( argc, argv );
	const vje::cli::CommandRegistry& registry  = vje::cli::CommandRegistry::standard ();
	const vje::cli::LaunchPlan       plan      = vje::cli::plan_launch ( arguments, registry );

	if ( plan.kind != vje::cli::LaunchPlan::Kind::OpenWindow )
	{
		return run_without_window ( argc, argv, plan, registry );
	}

	// The window. Construct the application and stamp its identity so QStandardPaths::AppConfigLocation resolves the
	// per-user settings location (NFR-06). The toolkit's own options ("-platform", "-style") are still in argv, and
	// QApplication consumes them here; the plan accepted them for exactly this.

	QApplication application ( argc, argv );

	vje::cli::apply_application_identity ();

	// The window and taskbar icon. Set from the compiled-in artwork so both platforms match at run time; the Windows
	// executable icon and the Linux hicolor theme install are packaging steps.

	QApplication::setWindowIcon ( application_icon () );

	// Composition root: instantiate the services, apply the persisted theme, and inject everything into the window.

	vje::SettingsStore    settings  ( vje::SettingsStore::default_file_path () );

	// SET-12's one-time move off the superseded "Rounded pane corners" key, run BEFORE anything reads a setting. The
	// reader answers correctly either way (settings_profiles.hpp), so this is housekeeping rather than a precondition:
	// what it buys is that the legacy key stops being consulted, and stops sitting in the user's settings file.

	vje::migrate_interface_style ( &settings );

	// Settings this build no longer reads. Separate from the migration above because it is a different statement --
	// that one moves a value, this one drops keys whose reader is gone (SET-13 retired Debug > Icon image format).

	vje::migrate_retired_settings ( &settings );

	// SET-15 / FILE-15: Windows Explorer's menus follow the setting from here on -- the Settings dialog's OK reaches it
	// through the store's signal. Root-scoped, and constructed here only: no window harness may own one, since a test's
	// OK would then reach the developer's real registry (ExplorerIntegrationSync.hpp).

	vje::ExplorerIntegrationSync explorerIntegration ( &settings, vje::cli::window_program_path () );

	vje::JsonDocument     document;
	vje::UndoController   undo       ( &document );
	vje::SelectionService selection;
	vje::StatusService    status;
	vje::ThemeService     theme      ( &settings );
	vje::ClipboardService clipboard  ( QApplication::clipboard () );

	// Where a load or a save runs (NFR-04). The window's file commands wait on it, so the parse is off the UI thread
	// while the command sequence stays linear (BackgroundIo.hpp).

	vje::ThreadPoolIo     backgroundIo;

	// Constructed before the first apply() so it observes every palette application and its tinted icons never go
	// stale (icons recolour with the theme).

	vje::IconLibrary icons ( &theme );

	// SET-12 / SET-13, read ONCE here and handed to three consumers -- the style object via ThemeService, the two
	// cards via MainWindow, and the icon family via IconLibrary. Set BEFORE the first apply() so the very first paint
	// already carries the user's choice, and set on the library while its cache is still empty so the family costs no
	// rebuild.

	const vje::config::appearance::InterfaceStyle interfaceStyle = vje::interface_style ( &settings );

	icons.set_interface_style ( interfaceStyle );
	theme.set_interface_style ( interfaceStyle );

	theme.apply ();

	// STYLE-19: every title bar a step darker than its window. (Dark or light is already VJE's: Qt takes it from the
	// palette apply () set.) Before the window, so the main window is coloured by its first show rather than after it;
	// re-applied on every theme application, since the colour is cut from the palette.

	vje::TitleBarSync titleBars;

	QObject::connect ( &theme, &vje::ThemeService::applied, &titleBars, &vje::TitleBarSync::refresh );

	vje::MainWindow mainWindow ( &document, &undo, &settings, &theme, &selection, &status, &icons, &clipboard, &backgroundIo );
	mainWindow.show ();

	// While SET-15 is Yes, make sure Explorer's entries name THIS copy of VJE -- a portable folder moved, an update
	// installed elsewhere. A failure is shown once the event loop runs, over the window rather than before it.

	QObject::connect ( &explorerIntegration, &vje::ExplorerIntegrationSync::failed,
	                   &mainWindow,          &vje::MainWindow::report_explorer_integration_failure, Qt::QueuedConnection );

	explorerIntegration.refresh ();

	// Open the command-line file (if any) once the window exists, so a load error can present a dialog (FILE-06).

	if ( !plan.file.isEmpty () )
	{
		mainWindow.open_document_from_path ( plan.file );
	}

	return application.exec ();
}
