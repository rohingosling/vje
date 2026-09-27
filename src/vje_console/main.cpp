//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   vje.com -- the Windows console twin's entry point (CLI-07, architecture.md section 13.2).
//
//   It asks the same launch_plan vje.exe asks, and acts on the answer in the one way a console program can:
//
//     a command or a usage error   run here, with real standard streams and the real exit code
//     the window                   start the vje.exe beside this file with the same arguments, and return at once
//
//   THE HAND-OFF PASSES THE ARGUMENTS THROUGH UNCHANGED -- the toolkit's own options included -- because vje.exe
//   re-plans from them and is the one that constructs the QApplication those options are for. The twin decides only
//   that the answer is "the window"; what the window then does with the line is vje.exe's.
//
//   THE WINDOW PROGRAM IS FOUND BY THIS FILE'S OWN NAME: vje.com looks for vje.exe (vje_cli/window_program, which the
//   Explorer pair asks too). A copy renamed as a pair still finds its partner, and tst_cli_process relies on exactly
//   that, running a copy of the twin beside a stand-in.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/CommandRegistry.hpp>
#include <vje_cli/ConsoleCliContext.hpp>
#include <vje_cli/application_identity.hpp>
#include <vje_cli/command_definitions.hpp>
#include <vje_cli/launch_plan.hpp>
#include <vje_cli/window_program.hpp>

#include <platform/process_arguments.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

int main ( int argc, char* argv [] )
{
	const QStringList                arguments = vje::platform::command_line_arguments ( argc, argv );
	const vje::cli::CommandRegistry& registry  = vje::cli::CommandRegistry::standard ();
	const vje::cli::LaunchPlan       plan      = vje::cli::plan_launch ( arguments, registry );

	QCoreApplication application ( argc, argv );

	vje::cli::apply_application_identity ();

	vje::cli::ConsoleCliContext context;

	if ( plan.kind != vje::cli::LaunchPlan::Kind::OpenWindow )
	{
		return vje::cli::exit_status ( vje::cli::execute ( plan, registry, context ) );
	}

	// The window. Hand it to the window program and return: a terminal that typed `vje file.json` gets its prompt back
	// at once, which is what launching a window program from a terminal does on Windows.

	const QFileInfo self          ( QCoreApplication::applicationFilePath () );
	const QString   windowProgram = vje::cli::window_program_path ();

	if ( !QFileInfo::exists ( windowProgram ) )
	{
		context.write_error ( QStringLiteral ( "%1: cannot open the window: %2 is not beside %3.\n" )
		                          .arg ( QString::fromLatin1 ( vje::cli::PROGRAM_NAME ),
		                                 QDir::toNativeSeparators ( windowProgram ),
		                                 self.fileName () ) );

		return vje::cli::exit_status ( vje::cli::ExitCode::Failure );
	}

	if ( !QProcess::startDetached ( windowProgram, arguments.mid ( 1 ) ) )
	{
		context.write_error ( QStringLiteral ( "%1: cannot open the window: %2 could not be started.\n" )
		                          .arg ( QString::fromLatin1 ( vje::cli::PROGRAM_NAME ), QDir::toNativeSeparators ( windowProgram ) ) );

		return vje::cli::exit_status ( vje::cli::ExitCode::Failure );
	}

	return vje::cli::exit_status ( vje::cli::ExitCode::Success );
}
