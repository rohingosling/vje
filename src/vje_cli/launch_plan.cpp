//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   launch_plan implementation -- see the header for the design.
//
//   THE ORDER OF THE CHECKS IS THE ORDER OF THE MESSAGES a user would want. The parser's own complaint (an unknown
//   option, a missing value) comes first because nothing after it can be trusted; then "two commands", because which
//   command's rules apply is undecided until that is settled; then the options that belong elsewhere; and only then
//   the chosen command's own rules about its operands.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/launch_plan.hpp>

#include <vje_cli/CliContext.hpp>
#include <vje_cli/CommandRegistry.hpp>
#include <vje_cli/command_definitions.hpp>

#include <QCommandLineParser>

#include <vector>

namespace vje::cli
{
	namespace
	{
		LaunchPlan usage_error ( const QString& problem )
		{
			LaunchPlan plan;
			plan.kind    = LaunchPlan::Kind::UsageError;
			plan.problem = problem;

			return plan;
		}

		QString spelled ( const QCommandLineOption& option )
		{
			return option_synopsis ( QCommandLineOption ( canonical_name ( option ) ) );
		}

		// The parser's answer as a value, keyed by long name. Only options the definition holds can be set, so walking
		// the definition is walking everything the user gave.

		ParsedCommandLine read_parser ( const QCommandLineParser& parser, const CommandRegistry& registry )
		{
			ParsedCommandLine commandLine;
			commandLine.positionals = parser.positionalArguments ();

			// A flag's value is recorded as empty WITHOUT asking the parser for it: QCommandLineParser::value () on an
			// option that takes none prints "option not expecting values" to standard error -- measured on Qt 6.8.3,
			// silent on 6.10.1 -- and a warning in the middle of `vje --version`'s output is a defect a script sees.

			auto record = [ & ] ( const QCommandLineOption& option )
			{
				if ( parser.isSet ( option ) )
				{
					commandLine.options.insert ( canonical_name ( option ), option.valueName ().isEmpty () ? QString () : parser.value ( option ) );
				}
			};

			for ( const auto& command : registry.commands () )
			{
				record ( command->trigger () );

				for ( const QCommandLineOption& modifier : command->modifiers () )
				{
					record ( modifier );
				}
			}

			return commandLine;
		}

		// The first toolkit option given, spelled as the user would recognise it, or empty.

		QString first_toolkit_option ( const QCommandLineParser& parser )
		{
			for ( const QCommandLineOption& option : toolkit_window_options () )
			{
				if ( parser.isSet ( option ) )
				{
					return QStringLiteral ( "-" ) + canonical_name ( option );
				}
			}

			return QString ();
		}
	}

	LaunchPlan plan_launch ( const QStringList& arguments, const CommandRegistry& registry )
	{
		QCommandLineParser parser;

		configure_parser ( parser, registry );

		if ( !parser.parse ( arguments ) )
		{
			return usage_error ( parser.errorText () );
		}

		const ParsedCommandLine commandLine = read_parser ( parser, registry );

		// Which commands were named. At most one may be (CLI-03).

		std::vector<const ICliCommand*> named;

		for ( const auto& command : registry.commands () )
		{
			if ( commandLine.is_set ( canonical_name ( command->trigger () ) ) )
			{
				named.push_back ( command.get () );
			}
		}

		if ( named.size () > 1 )
		{
			return usage_error ( QStringLiteral ( "Give one command at a time; %1 and %2 were both given." )
			                         .arg ( spelled ( named [ 0 ]->trigger () ), spelled ( named [ 1 ]->trigger () ) ) );
		}

		const ICliCommand* chosen = named.empty () ? nullptr : named.front ();

		// An option belonging to one command, given beside another command or with none. Refused rather than ignored,
		// because an ignored --values looks exactly like one that worked (CLI-01).

		for ( const auto& command : registry.commands () )
		{
			if ( command.get () == chosen )
			{
				continue;
			}

			for ( const QCommandLineOption& modifier : command->modifiers () )
			{
				if ( commandLine.is_set ( canonical_name ( modifier ) ) )
				{
					return usage_error ( QStringLiteral ( "%1 is used with %2." )
					                         .arg ( spelled ( modifier ), spelled ( command->trigger () ) ) );
				}
			}
		}

		if ( chosen != nullptr )
		{
			// The toolkit's options configure a window, and a command opens none.

			const QString toolkitOption = first_toolkit_option ( parser );

			if ( !toolkitOption.isEmpty () )
			{
				return usage_error ( QStringLiteral ( "%1 applies to the window, and %2 opens none." )
				                         .arg ( toolkitOption, spelled ( chosen->trigger () ) ) );
			}

			const QString problem = chosen->check_arguments ( commandLine );

			if ( !problem.isEmpty () )
			{
				return usage_error ( problem );
			}

			LaunchPlan plan;
			plan.kind        = LaunchPlan::Kind::RunCommand;
			plan.command     = chosen;
			plan.commandLine = commandLine;

			return plan;
		}

		// No command: the window. VJE holds one document, so it opens one file or none (CLI-02).

		const QStringList& files = commandLine.positionals;

		if ( files.size () > 1 )
		{
			return usage_error ( QStringLiteral ( "VJE opens one file at a time; %1 were given." ).arg ( files.size () ) );
		}

		if ( !files.isEmpty () )
		{
			if ( files.front ().isEmpty () )
			{
				return usage_error ( QStringLiteral ( "The file name is empty." ) );
			}

			if ( files.front () == QLatin1String ( STANDARD_INPUT_ARGUMENT ) )
			{
				return usage_error ( QStringLiteral ( "The window opens a named file; '%1' (standard input) is read only by a command." )
				                         .arg ( QString::fromLatin1 ( STANDARD_INPUT_ARGUMENT ) ) );
			}
		}

		LaunchPlan plan;
		plan.kind        = LaunchPlan::Kind::OpenWindow;
		plan.file        = files.isEmpty () ? QString () : files.front ();
		plan.commandLine = commandLine;

		return plan;
	}

	ExitCode execute ( const LaunchPlan& plan, const CommandRegistry& registry, CliContext& context )
	{
		switch ( plan.kind )
		{
			case LaunchPlan::Kind::UsageError:
			{
				context.write_error ( usage_error_text ( plan.problem ) );

				return ExitCode::UsageError;
			}

			case LaunchPlan::Kind::RunCommand:
			{
				return plan.command->run ( plan.commandLine, registry, context );
			}

			case LaunchPlan::Kind::OpenWindow:
			{
				break;
			}
		}

		// A window launch reached the no-window path: a defect in the caller, not something a user did. Reported
		// rather than asserted, so a release build still says something.

		context.write_error ( QStringLiteral ( "%1: internal error: a window launch cannot be run as a command.\n" )
		                          .arg ( QString::fromLatin1 ( PROGRAM_NAME ) ) );

		return ExitCode::Failure;
	}

	bool output_is_the_answer ( const LaunchPlan& plan )
	{
		switch ( plan.kind )
		{
			case LaunchPlan::Kind::UsageError: return true;
			case LaunchPlan::Kind::RunCommand: return ( plan.command != nullptr ) && plan.command->informational ();
			case LaunchPlan::Kind::OpenWindow: return false;
		}

		return false;
	}
}
