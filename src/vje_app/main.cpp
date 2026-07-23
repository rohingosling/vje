//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   Application entry point and composition root. It:
//
//     - Answers --version / -v without constructing a QApplication, so the query works headlessly (no display) and
//       exits at once -- the Phase 0 smoke check and the groundwork for command-line handling (FILE-01).
//     - Otherwise constructs the QApplication, stamps its identity (so QStandardPaths resolves the config location),
//       instantiates the vje_core services (document, undo) and the vje_app services (settings, theme, selection,
//       status), applies the persisted theme, injects everything into MainWindow by constructor, and opens any file
//       passed on the command line.
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

#include "services/IconLibrary.hpp"
#include "services/SelectionService.hpp"
#include "services/SettingsStore.hpp"
#include "services/StatusService.hpp"
#include "services/ThemeService.hpp"

#include <vje_core/document/JsonDocument.hpp>
#include <vje_core/editing/UndoController.hpp>
#include <vje_core/version.hpp>

#include <QApplication>
#include <QIcon>
#include <QString>

#include <cstdio>

namespace
{
	// Command-line switches handled before the GUI starts.

	constexpr auto ARGUMENT_VERSION_LONG  = "--version";
	constexpr auto ARGUMENT_VERSION_SHORT = "-v";
}

int main ( int argc, char* argv [] )
{
	// Answer --version / -v ahead of QApplication so the query needs no display and returns at once. The first
	// non-option argument (if any) is remembered as the file to open (FILE-01).

	QString fileToOpen;

	for ( int i = 1; i < argc; ++i )
	{
		const QString argument = QString::fromLocal8Bit ( argv [ i ] );

		if ( ( argument == QLatin1String ( ARGUMENT_VERSION_LONG ) ) || ( argument == QLatin1String ( ARGUMENT_VERSION_SHORT ) ) )
		{
			std::printf ( "%s\n", vje::version_line ().toLocal8Bit ().constData () );

			return 0;
		}

		if ( fileToOpen.isEmpty () && !argument.startsWith ( QLatin1Char ( '-' ) ) )
		{
			fileToOpen = argument;
		}
	}

	// Construct the application and stamp its identity so QStandardPaths::AppConfigLocation resolves the per-user
	// settings location (NFR-06).

	QApplication application ( argc, argv );

	QApplication::setApplicationName    ( vje::application_name () );
	QApplication::setApplicationVersion ( vje::version_string () );
	QApplication::setOrganizationName   ( vje::application_name () );

	// The window and taskbar icon. Set from the compiled-in vector so both platforms match at run time; the Windows
	// executable icon and the Linux hicolor theme install are packaging steps.

	QApplication::setWindowIcon ( QIcon ( QStringLiteral ( ":/vje/app/vje.svg" ) ) );

	// Composition root: instantiate the services, apply the persisted theme, and inject everything into the window.

	vje::SettingsStore    settings  ( vje::SettingsStore::default_file_path () );
	vje::JsonDocument     document;
	vje::UndoController   undo       ( &document );
	vje::SelectionService selection;
	vje::StatusService    status;
	vje::ThemeService     theme      ( &settings );

	// Constructed before the first apply() so it observes every palette application and its tinted icons never go
	// stale (icons recolour with the theme).

	vje::IconLibrary icons ( &theme );

	theme.apply ();

	vje::MainWindow mainWindow ( &document, &undo, &settings, &theme, &selection, &status, &icons );
	mainWindow.show ();

	// Open the command-line file (if any) once the window exists, so a load error can present a dialog (FILE-06).

	if ( !fileToOpen.isEmpty () )
	{
		mainWindow.open_document_from_path ( fileToOpen );
	}

	return application.exec ();
}
