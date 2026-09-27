//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   HelpCommand -- `vje -h | --help`: print the usage, rendered from the registry it is run against (CLI-01).
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_cli/ICliCommand.hpp>

namespace vje::cli
{
	class HelpCommand : public ICliCommand
	{
	public:

		QCommandLineOption trigger       () const override;
		QString            summary       () const override;
		bool               informational () const override;

		ExitCode run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const override;
	};
}
