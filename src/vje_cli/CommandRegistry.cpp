//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   CommandRegistry implementation -- see the header for the design.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <vje_cli/CommandRegistry.hpp>

#include <vje_cli/commands/ExplorerIntegrationCommands.hpp>
#include <vje_cli/commands/HelpCommand.hpp>
#include <vje_cli/commands/QueryCommand.hpp>
#include <vje_cli/commands/ValidateCommand.hpp>
#include <vje_cli/commands/VersionCommand.hpp>

namespace vje::cli
{
	const CommandRegistry& CommandRegistry::standard ()
	{
		// The order is the help's: the two commands a user came for first, then the Explorer pair (absent where the
		// platform does not offer it), then the two that describe VJE itself -- spec section 2.13's synopsis order.

		static const CommandRegistry registry = []
		{
			CommandRegistry commands;

			commands.add ( std::make_unique<ValidateCommand>                      () );
			commands.add ( std::make_unique<QueryCommand>                         () );
			commands.add ( std::make_unique<RegisterExplorerIntegrationCommand>   () );
			commands.add ( std::make_unique<UnregisterExplorerIntegrationCommand> () );
			commands.add ( std::make_unique<HelpCommand>                          () );
			commands.add ( std::make_unique<VersionCommand>                       () );

			return commands;
		} ();

		return registry;
	}

	void CommandRegistry::add ( std::unique_ptr<ICliCommand> command )
	{
		if ( command && command->offered_on_this_platform () )
		{
			registered.push_back ( std::move ( command ) );
		}
	}

	const std::vector<std::unique_ptr<ICliCommand>>& CommandRegistry::commands () const
	{
		return registered;
	}
}
