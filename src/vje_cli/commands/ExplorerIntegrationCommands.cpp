//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   The Explorer pair's implementation -- see the header for the design.
//
//   THE SETTING RECORDS WHAT THE REGISTRY HOLDS AFTERWARDS, not what was asked for. Each command finishes by asking
//   the registry and writing that answer, so a registration that failed part-way -- and was rolled back -- leaves the
//   setting No rather than claiming a Yes that is not in Explorer's menus.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/commands/ExplorerIntegrationCommands.hpp>

#include <vje_cli/CliContext.hpp>
#include <vje_cli/command_definitions.hpp>
#include <vje_cli/window_program.hpp>

#include <vje_settings/SettingsStore.hpp>

#include <platform/explorer_integration.hpp>

#include <QDir>
#include <QFileInfo>

namespace vje::cli
{
	namespace explorer = platform::explorer_integration;

	namespace
	{
		QString settings_file ( const ExplorerCommandTarget& target )
		{
			return target.settingsFilePath.isEmpty () ? SettingsStore::default_file_path () : target.settingsFilePath;
		}

		QString classes_root ( const ExplorerCommandTarget& target )
		{
			return target.classesRoot.isEmpty () ? explorer::LIVE_CLASSES_ROOT : target.classesRoot;
		}

		QString window_program ( const ExplorerCommandTarget& target )
		{
			return target.windowProgramPath.isEmpty () ? window_program_path () : target.windowProgramPath;
		}

		// SET-15, kept in agreement with the registry. A Yes is always written. A No is written only over a stored
		// value: an absent setting already reads No (lesson D8), and the uninstaller's run must not create a settings
		// file on its way out.

		void record ( const ExplorerCommandTarget& target, bool registered )
		{
			SettingsStore settings ( settings_file ( target ) );

			if ( registered || settings.contains ( settings_keys::EXPLORER_INTEGRATION ) )
			{
				settings.set_bool ( settings_keys::EXPLORER_INTEGRATION, registered );
			}
		}

		// The pair acts on the window program, never on a document, so a file beside either is a mistake rather than
		// something to ignore (CLI-01).

		QString refuse_files ( const QCommandLineOption& trigger, const ParsedCommandLine& commandLine )
		{
			if ( commandLine.positionals.isEmpty () )
			{
				return {};
			}

			return QStringLiteral ( "--%1 takes no file; it acts on the VJE it is run from." ).arg ( canonical_name ( trigger ) );
		}

		ExitCode fail ( CliContext& context, const QString& action, const QString& problem )
		{
			context.write_error ( QStringLiteral ( "%1: %2: %3\n" ).arg ( QString::fromLatin1 ( PROGRAM_NAME ), action, problem ) );

			return ExitCode::Failure;
		}
	}

	//=================================================================================================================
	// RegisterExplorerIntegrationCommand
	//=================================================================================================================

	RegisterExplorerIntegrationCommand::RegisterExplorerIntegrationCommand ( ExplorerCommandTarget target )
		: target ( std::move ( target ) )
	{
	}

	QCommandLineOption RegisterExplorerIntegrationCommand::trigger () const
	{
		return QCommandLineOption ( QStringLiteral ( "register-explorer-integration" ) );
	}

	QString RegisterExplorerIntegrationCommand::summary () const
	{
		return QStringLiteral ( "Add VJE to Explorer's menus for .json files." );
	}

	bool RegisterExplorerIntegrationCommand::offered_on_this_platform () const
	{
		return explorer::is_supported ();
	}

	QString RegisterExplorerIntegrationCommand::check_arguments ( const ParsedCommandLine& commandLine ) const
	{
		return refuse_files ( trigger (), commandLine );
	}

	ExitCode RegisterExplorerIntegrationCommand::run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const
	{
		Q_UNUSED ( commandLine )
		Q_UNUSED ( registry )

		const QString action        = QStringLiteral ( "cannot register with Windows Explorer" );
		const QString windowProgram = window_program ( target );
		const QString root          = classes_root ( target );

		// Entries naming a program that is not there would put a menu item in Explorer that does nothing. Checked
		// before anything is written, so neither the registry nor the setting changes.

		if ( !QFileInfo ( windowProgram ).isFile () )
		{
			return fail ( context, action, QStringLiteral ( "%1 does not exist." ).arg ( QDir::toNativeSeparators ( windowProgram ) ) );
		}

		const explorer::Outcome registered = explorer::register_for ( windowProgram, root );

		record ( target, explorer::is_registered_for ( windowProgram, root ) );

		if ( !registered.succeeded )
		{
			return fail ( context, action, registered.problem );
		}

		context.write_output ( QStringLiteral ( "Registered %1 with Windows Explorer for .json files.\n" )
		                           .arg ( QDir::toNativeSeparators ( windowProgram ) ) );

		return ExitCode::Success;
	}

	//=================================================================================================================
	// UnregisterExplorerIntegrationCommand
	//=================================================================================================================

	UnregisterExplorerIntegrationCommand::UnregisterExplorerIntegrationCommand ( ExplorerCommandTarget target )
		: target ( std::move ( target ) )
	{
	}

	QCommandLineOption UnregisterExplorerIntegrationCommand::trigger () const
	{
		return QCommandLineOption ( QStringLiteral ( "unregister-explorer-integration" ) );
	}

	QString UnregisterExplorerIntegrationCommand::summary () const
	{
		return QStringLiteral ( "Remove exactly what --register-explorer-integration added." );
	}

	bool UnregisterExplorerIntegrationCommand::offered_on_this_platform () const
	{
		return explorer::is_supported ();
	}

	QString UnregisterExplorerIntegrationCommand::check_arguments ( const ParsedCommandLine& commandLine ) const
	{
		return refuse_files ( trigger (), commandLine );
	}

	ExitCode UnregisterExplorerIntegrationCommand::run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const
	{
		Q_UNUSED ( commandLine )
		Q_UNUSED ( registry )

		const QString root          = classes_root ( target );
		const bool    wasRegistered = explorer::is_registered ( root );

		const explorer::Outcome removed = explorer::unregister ( root );

		// Whatever could not be removed is still there, and while it is, the setting says so -- which is also what
		// lets the next launch, or the next attempt, finish the job.

		record ( target, explorer::is_registered ( root ) );

		if ( !removed.succeeded )
		{
			return fail ( context, QStringLiteral ( "cannot remove the Windows Explorer entries" ), removed.problem );
		}

		context.write_output ( wasRegistered ? QStringLiteral ( "Removed VJE's Windows Explorer entries.\n" )
		                                     : QStringLiteral ( "VJE has no Windows Explorer entries to remove.\n" ) );

		return ExitCode::Success;
	}
}
