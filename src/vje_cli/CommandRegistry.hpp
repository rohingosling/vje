//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CommandRegistry -- the commands this platform offers, in the order the help lists them (architecture.md section
//   13.2). The shape of the converter table (section 6) and the view registry (section 5): a command joins with one
//   line in standard (), and the parser, the help and the dispatch then all include it.
//
//   A COMMAND THE PLATFORM DOES NOT OFFER IS NEVER ADDED. add () asks offered_on_this_platform () and drops a command
//   that answers false, so there is no second place that has to remember to hide it -- CLI-01's "absent, not refused".
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <vje_cli/ICliCommand.hpp>

#include <memory>
#include <vector>

namespace vje::cli
{
	class CommandRegistry
	{
	public:

		// The commands VJE ships, for this platform. Built once.

		static const CommandRegistry& standard ();

		// Register a command, unless this platform does not offer it.

		void add ( std::unique_ptr<ICliCommand> command );

		// In registration order, which is the order --help lists them.

		const std::vector<std::unique_ptr<ICliCommand>>& commands () const;

	private:

		std::vector<std::unique_ptr<ICliCommand>> registered;
	};
}
