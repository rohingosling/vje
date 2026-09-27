//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   VersionCommand -- `vje -v | --version`: print "VJE <version>", the line the About dialog and the Phase 0 smoke
//   check have always used (vje::version_line).
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
	class VersionCommand : public ICliCommand
	{
	public:

		QCommandLineOption trigger       () const override;
		QString            summary       () const override;
		bool               informational () const override;

		ExitCode run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const override;
	};
}
