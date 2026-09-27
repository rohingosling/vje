//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ICliCommand -- one command that runs without a window, and everything the rest of the command line needs to know
//   about it (architecture.md section 13.2).
//
//   A COMMAND DESCRIBES ITSELF, AND THAT IS HOW CLI-01 HOLDS. The parser, the help and the dispatch all read these
//   answers -- the option that names the command, the options only it reads, its operands, its one-line summary -- so
//   adding a command is one class and one line in CommandRegistry, and it cannot be accepted without appearing in
//   --help, or appear there without being accepted.
//
//   A COMMAND IS AN OPTION, NOT A WORD. `vje --validate a.json` rather than `vje validate a.json`: VJE's one positional
//   argument already means the file to open, and a subcommand word cannot be told apart from a file of that name.
//
//   ParsedCommandLine is the parser's answer as a VALUE -- QCommandLineParser cannot be copied -- keyed by each
//   option's long name, so a command asks is_set ( "values" ) whether the user typed --values or -values.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_cli/exit_codes.hpp>

#include <QCommandLineOption>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace vje::cli
{
	class CliContext;
	class CommandRegistry;

	//-----------------------------------------------------------------------------------------------------------------
	// What the parser found: the positional arguments, and the options that were given with their values.
	//-----------------------------------------------------------------------------------------------------------------

	struct ParsedCommandLine
	{
		QStringList             positionals;               // In the order given; "-" is standard input.
		QHash<QString, QString> options;                   // Long name -> value; a flag's value is empty.

		bool is_set ( const QString& name ) const
		{
			return options.contains ( name );
		}

		QString value ( const QString& name ) const
		{
			return options.value ( name );
		}
	};

	// An option's canonical name: its LAST name, which by this project's convention is the long one ({ "h", "help" }).

	inline QString canonical_name ( const QCommandLineOption& option )
	{
		return option.names ().constLast ();
	}

	//-----------------------------------------------------------------------------------------------------------------
	// The command.
	//-----------------------------------------------------------------------------------------------------------------

	class ICliCommand
	{
	public:

		virtual ~ICliCommand () = default;

		// The option that names the command, with the value name when it takes one ("--query <expression>"). The
		// command IS this option being present.

		virtual QCommandLineOption trigger () const = 0;

		// Options only this command reads (--values for --query). Given beside any other command, or with none, they
		// are a usage error rather than being ignored (CLI-01).

		virtual QList<QCommandLineOption> modifiers () const
		{
			return {};
		}

		// The operands after the options in the help's synopsis ("<file>..."), or empty.

		virtual QString operands () const
		{
			return {};
		}

		// One sentence for the help: what the command does.

		virtual QString summary () const = 0;

		// Whether this platform offers the command. One that answers false is never registered, so it is absent from
		// the parser AND the help rather than present and refused (CLI-01). The Explorer pair (CLI-08) answers what
		// the platform layer says, so it is absent on Linux; every other command answers true.

		virtual bool offered_on_this_platform () const
		{
			return true;
		}

		// Whether a file operand of "-" means standard input (CLI-06). The help's note on "-" lists exactly the
		// commands that answer true, so it is derived rather than restated.

		virtual bool accepts_standard_input () const
		{
			return false;
		}

		// Whether the output IS the answer, as it is for help and the version. The window program, which has no
		// console, shows such output in a message box rather than discarding it (CLI-07).

		virtual bool informational () const
		{
			return false;
		}

		// A usage error in the arguments this command was given, as one sentence, or empty when they are fine. Asked
		// by plan_launch, so a wrong count of files is decided before anything is read.

		virtual QString check_arguments ( const ParsedCommandLine& commandLine ) const
		{
			Q_UNUSED ( commandLine )

			return {};
		}

		// Do the work. `registry` is what the help renders; no other command needs it.

		virtual ExitCode run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const = 0;
	};
}
