//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   launch_plan -- what an invocation of VJE means, decided in one pure function before anything else happens
//   (architecture.md section 13.2).
//
//   THREE ANSWERS, AND BOTH ENTRY POINTS ACT ON THE SAME ONE. An argument list goes in; out comes "run this command",
//   "open the window with this file (or none)", or "this usage error". vje_app's main and the Windows console twin
//   both ask it and neither decides anything itself, so a terminal and a double click cannot come to different
//   conclusions about the same line.
//
//   IT RUNS BEFORE ANY APPLICATION OBJECT EXISTS, and that is forced rather than chosen. A command runs under
//   QCoreApplication, which needs no display, so `vje --validate` works over SSH and in CI (CLI-03); the window needs
//   QApplication; a process gets one of the two. The plan is what says which. The arguments it reads come from
//   platform/process_arguments, which is Unicode on every platform, so the file a window launch is handed is the file
//   the user named (CLI-02) -- nothing is re-read from argv afterwards.
//
//   The architecture first planned this as two passes -- a scan of option NAMES over the raw argv to pick the object,
//   then a full parse over QCoreApplication::arguments () -- on the grounds that the raw argv's encoding is wrong for
//   file names but safe for ASCII option names. Reading the Unicode arguments up front removes the reason for the
//   second pass, and with it the case the architecture flagged as needing measurement: QCoreApplication::arguments ()
//   falls back to the ANSI argv whenever QApplication has removed an option of its own.
//
//   A USAGE ERROR NEEDS NO WINDOW. `vje --bogus` and `vje a.json b.json` are answered before QApplication would have
//   been constructed, so on Linux they report and exit even with no display server.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_cli/ICliCommand.hpp>
#include <vje_cli/exit_codes.hpp>

#include <QString>
#include <QStringList>

namespace vje::cli
{
	class CliContext;
	class CommandRegistry;

	struct LaunchPlan
	{
		enum class Kind
		{
			OpenWindow,                                    // `file` is what to load; empty for none.
			RunCommand,                                    // `command` runs over `commandLine`.
			UsageError                                     // `problem` says what was wrong.
		};

		Kind               kind    = Kind::OpenWindow;
		QString            file;
		const ICliCommand* command = nullptr;              // Owned by the registry the plan was made from.
		ParsedCommandLine  commandLine;
		QString            problem;                        // One sentence, with no program-name prefix.
	};

	// What `arguments` asks for. The list is the whole command line, the program name first, as
	// platform::command_line_arguments returns it.

	LaunchPlan plan_launch ( const QStringList& arguments, const CommandRegistry& registry );

	// Carry out a plan that does not open the window: print a usage error, or run the command. Returns the exit code.

	ExitCode execute ( const LaunchPlan& plan, const CommandRegistry& registry, CliContext& context );

	// Whether a plan's output IS the answer -- help, the version, or a usage error -- as opposed to a report a script
	// reads. The window program shows the first kind in a message box when it has nowhere to print it, and discards
	// the second (CLI-07).

	bool output_is_the_answer ( const LaunchPlan& plan );
}
