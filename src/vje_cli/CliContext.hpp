//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CliContext -- everything a running command may touch, and nothing else (architecture.md section 13.2).
//
//   WHAT IS NOT HERE IS THE POINT. There is no settings store, no theme, no recent-files list: CLI-03 says a command
//   reads no preference, so that a script's answer does not change because somebody changed a setting in the window.
//   Leaving the store out of the context makes that true by construction -- a command could only break the rule by
//   reaching for a global, which review sees -- rather than by each command remembering not to. The Explorer pair
//   (Phase 15j) is the one exception CLI-08 allows, and it is handed its store explicitly: each of the two is
//   constructed with an ExplorerCommandTarget naming the settings file (commands/ExplorerIntegrationCommands.hpp).
//
//   It is an interface for the reason IDialogService is one: the real context writes to the process's streams
//   (ConsoleCliContext), and a test's captures what was written, so every command is exercised headlessly with no
//   process and no terminal.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QByteArray>
#include <QString>

namespace vje::cli
{
	class CliContext
	{
	public:

		virtual ~CliContext () = default;

		// Reports and results: what a script reads (CLI-09).

		virtual void write_output ( const QString& text ) = 0;

		// Usage errors, and anything that is not a result -- so a query's output stays clean to pipe (CLI-09).

		virtual void write_error ( const QString& text ) = 0;

		// Everything on standard input, as bytes (CLI-06). A command reads it at most once; plan_launch refuses a
		// command line that names standard input twice.

		virtual QByteArray read_standard_input () = 0;
	};
}
