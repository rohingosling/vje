//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   The Explorer pair (CLI-08): `vje --register-explorer-integration` and `vje --unregister-explorer-integration`.
//
//   THE ONE EXCEPTION TO "A COMMAND READS NO PREFERENCE" (CLI-03), and a narrow one. Each command changes the registry
//   and then records the result in SET-15, so the Settings dialog and Explorer's menus never disagree about whether VJE
//   is there. The store is not in CliContext -- it stays out of every other command's reach -- but in the target each
//   of these two is constructed with, which is how it is "handed its store explicitly".
//
//   BOTH ACT ON THE WINDOW PROGRAM, whichever executable ran them: Explorer must launch vje.exe, never the console twin,
//   or a console would open beside every file opened from the menu (CLI-07).
//
//   UNREGISTER IS WHAT AN UNINSTALLER RUNS (Phase 17), so it succeeds with nothing to remove, and when it has nothing to
//   record it writes nothing either: an absent setting already reads No, and an uninstaller creating a settings file
//   on the way out would leave behind exactly what it is there to remove.
//
//   NEITHER EXISTS WHERE THE PLATFORM DOES NOT OFFER THE FEATURE: offered_on_this_platform asks the platform layer, so
//   on Linux the pair is absent from the parser and the help alike (CLI-01).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_cli/ICliCommand.hpp>

#include <QString>

namespace vje::cli
{
	//-----------------------------------------------------------------------------------------------------------------
	// What the pair touches beyond the context. Every member empty means the real thing -- the user's settings file,
	// the live classes root, the window program beside this executable -- and a test fills in all three.
	//-----------------------------------------------------------------------------------------------------------------

	struct ExplorerCommandTarget
	{
		QString settingsFilePath;                          // Empty => SettingsStore::default_file_path ().
		QString classesRoot;                               // Empty => the live root, HKEY_CURRENT_USER\Software\Classes.
		QString windowProgramPath;                         // Empty => window_program_path ().
	};

	//*****************************************************************************************************************
	// Class: RegisterExplorerIntegrationCommand
	//*****************************************************************************************************************

	class RegisterExplorerIntegrationCommand : public ICliCommand
	{
	public:

		explicit RegisterExplorerIntegrationCommand ( ExplorerCommandTarget target = {} );

		QCommandLineOption trigger                  () const override;
		QString            summary                  () const override;
		bool               offered_on_this_platform () const override;
		QString            check_arguments          ( const ParsedCommandLine& commandLine ) const override;

		ExitCode run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const override;

	private:

		ExplorerCommandTarget target;
	};

	//*****************************************************************************************************************
	// Class: UnregisterExplorerIntegrationCommand
	//*****************************************************************************************************************

	class UnregisterExplorerIntegrationCommand : public ICliCommand
	{
	public:

		explicit UnregisterExplorerIntegrationCommand ( ExplorerCommandTarget target = {} );

		QCommandLineOption trigger                  () const override;
		QString            summary                  () const override;
		bool               offered_on_this_platform () const override;
		QString            check_arguments          ( const ParsedCommandLine& commandLine ) const override;

		ExitCode run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const override;

	private:

		ExplorerCommandTarget target;
	};
}
