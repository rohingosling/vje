//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   HelpCommand implementation -- see the header.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/commands/HelpCommand.hpp>

#include <vje_cli/CliContext.hpp>
#include <vje_cli/command_definitions.hpp>

namespace vje::cli
{
	QCommandLineOption HelpCommand::trigger () const
	{
		return QCommandLineOption ( QStringList { QStringLiteral ( "h" ), QStringLiteral ( "help" ) } );
	}

	QString HelpCommand::summary () const
	{
		return QStringLiteral ( "Print this usage." );
	}

	bool HelpCommand::informational () const
	{
		return true;
	}

	ExitCode HelpCommand::run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const
	{
		Q_UNUSED ( commandLine )

		context.write_output ( help_text ( registry ) );

		return ExitCode::Success;
	}
}
