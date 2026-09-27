//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   QueryCommand -- `vje --query <expression> [--values] <file>`: the query view's engine without the view (CLI-05).
//
//   THE SAME ENGINE, SO THE SAME ANSWERS. JsonPathQuery is what the JSONPath view runs, so the grammar is exactly
//   QUERY-02..04, a malformed query is refused with the view's own message and column (QUERY-05), and the results
//   come back in document order with no duplicates (QUERY-06). The default output is each result's JSON Pointer, one
//   per line, exactly as Copy JSONPath Result copies them (QUERY-08) -- so the root is an empty line and every line
//   is a Go To input. --values prints each result's value as one line of compact JSON instead, number tokens as
//   written (FILE-10).
//
//   STANDARD OUTPUT CARRIES RESULTS AND NOTHING ELSE. A malformed expression, an unreadable file and a document that
//   is not JSON are all reported on standard error, so `vje --query ... | xargs ...` never feeds a complaint to the
//   next program as though it were a path.
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
	class QueryCommand : public ICliCommand
	{
	public:

		QCommandLineOption        trigger                () const override;
		QList<QCommandLineOption> modifiers              () const override;
		QString                   operands               () const override;
		QString                   summary                () const override;
		bool                      accepts_standard_input () const override;

		QString check_arguments ( const ParsedCommandLine& commandLine ) const override;

		ExitCode run ( const ParsedCommandLine& commandLine, const CommandRegistry& registry, CliContext& context ) const override;
	};
}
