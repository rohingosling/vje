#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_cli (the command line)
# Version: 2.0.0
# Date:    2024
# Author:  Rohin Gosling
#
# Description:
#
#   The command line (architecture.md section 13.2, spec section 2.13, CLI-01..09): the one definition the parser and
#   the help are built from, the launch plan both entry points act on, the registry of commands that run without a
#   window, and those commands.
#
#   It links vje_settings for the Explorer pair alone (CLI-08), the one set of commands allowed to write a preference.
#   No other command can reach the store: CliContext does not carry it.
#
#   IT LINKS NO QT WIDGETS, and that is why it is a library of its own. Two entry points read the same command line --
#   vje_app's main and, on Windows, the console twin vje.com -- and the twin must not pull in the GUI. The same property
#   makes every command testable headlessly, with no process and no display. (vje_core links Qt Gui for the undo
#   framework, so the twin loads Qt6Gui.dll; it never constructs a QGuiApplication, so it needs no display either.)
#
#-----------------------------------------------------------------------------------------------------------------------

add_library ( vje_cli STATIC
    exit_codes.hpp
    CliContext.hpp
    ConsoleCliContext.hpp
    ConsoleCliContext.cpp
    ICliCommand.hpp
    CommandRegistry.hpp
    CommandRegistry.cpp
    command_definitions.hpp
    command_definitions.cpp
    launch_plan.hpp
    launch_plan.cpp
    application_identity.hpp
    application_identity.cpp
    window_program.hpp
    window_program.cpp

    # The commands, and the one reader of a file operand they share.
    commands/document_input.hpp
    commands/document_input.cpp
    commands/HelpCommand.hpp
    commands/HelpCommand.cpp
    commands/VersionCommand.hpp
    commands/VersionCommand.cpp
    commands/ValidateCommand.hpp
    commands/ValidateCommand.cpp
    commands/QueryCommand.hpp
    commands/QueryCommand.cpp

    # The Explorer pair (CLI-08, Phase 15j): registered only where the platform offers the feature.
    commands/ExplorerIntegrationCommands.hpp
    commands/ExplorerIntegrationCommands.cpp
)

# Consumers include headers as <vje_cli/...>, so the include root is the parent src/ directory -- vje_core's rule.

target_include_directories ( vje_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. )

target_link_libraries ( vje_cli
    PUBLIC  vje_core vje_settings vje_platform Qt6::Core
    PRIVATE vje_warnings
)
