//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   VersionCommand implementation -- see the header.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/commands/VersionCommand.hpp>

#include <vje_cli/CliContext.hpp>

#include <vje_core/version.hpp>

namespace vje::cli
{
	QCommandLineOption VersionCommand::trigger () const
	{
		return QCommandLineOption ( QStringList { QStringLiteral ( "v" ), QStringLiteral ( "version" ) } );
	}

	QString VersionCommand::summary () const
	{
		return QStringLiteral ( "Print the version." );
	}

	bool VersionCommand::informational () const
	{
		return true;
	}

	ExitCode VersionCommand::run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const
	{
		Q_UNUSED ( commandLine )
		Q_UNUSED ( registry )

		context.write_output ( version_line () + QLatin1Char ( '\n' ) );

		return ExitCode::Success;
	}
}
