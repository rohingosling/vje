//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   exit_codes -- what a command tells a script when it finishes (spec section 2.13).
//
//   grep's CONVENTION, because it is the one a script writer already knows: 0 is yes, 1 is a clean no, 2 is "you
//   called me wrongly", and 3 is "I could not find out". The distinction worth keeping is between 1 and 3 -- "this
//   file is not valid JSON" and "this file could not be read" call for different action from whoever reads the code,
//   and folding them together would make `vje --validate` useless as a gate in a build.
//
//   THE HIGHEST CODE WINS when files disagree, which is why the values are ordered by severity and worse_of is a max.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <algorithm>

namespace vje::cli
{
	enum class ExitCode : int
	{
		Success    = 0,                                    // Every file valid; the query matched; help or version printed.
		Negative   = 1,                                    // A file is invalid; the query matched nothing.
		UsageError = 2,                                    // The command line itself was wrong, a malformed query included.
		Failure    = 3                                     // A file could not be read, or an operation could not complete.
	};

	// The value a process returns.

	constexpr int exit_status ( ExitCode code )
	{
		return static_cast<int> ( code );
	}

	// The more severe of two outcomes -- the rule for a command that reports on several files.

	constexpr ExitCode worse_of ( ExitCode first, ExitCode second )
	{
		return static_cast<ExitCode> ( std::max ( exit_status ( first ), exit_status ( second ) ) );
	}
}
