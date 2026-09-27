//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   ValidateCommand -- `vje --validate <file>...`: VAL-01 over each file, one report line each (CLI-04).
//
//   THE ERROR LINE IS "<file>:<line>:<column>: error: <message>" because that is the form compilers use, so editors
//   and CI log viewers already turn it into a link to the position. Duplicate keys follow as warnings in the same
//   form: RFC 8259 permits them and VJE loads them (FILE-04), so they are reported without failing the file.
//
//   It is the parser the window loads with, through Validator (VAL-01 / VAL-02), so a file that validates here opens
//   in the window, and a position reported here is the one FILE-06's error dialog shows.
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
	class ValidateCommand : public ICliCommand
	{
	public:

		QCommandLineOption trigger                () const override;
		QString            operands               () const override;
		QString            summary                () const override;
		bool               accepts_standard_input () const override;

		QString check_arguments ( const ParsedCommandLine& commandLine ) const override;

		ExitCode run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const override;
	};
}
