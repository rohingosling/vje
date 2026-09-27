#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_cli tests (Qt Test + CTest)
# Version: 2.0.0
# Date:    2024
# Author:  Rohin Gosling
#
# Description:
#
#   The command line's suites (Phase 15i). Two headless -- what an invocation means (tst_launch_plan) and what each
#   command prints and returns (tst_cli_commands), both with no process and no display -- and one that runs the BUILT
#   executables (tst_cli_process), for what only a real process can show: the command line the platform hands it, the
#   bytes it writes to a pipe, its exit code, and on Windows which subsystem each of the pair is linked for.
#
#   Added from the top-level CMakeLists AFTER every executable target, because tst_cli_process names them.
#
#-----------------------------------------------------------------------------------------------------------------------

function ( vje_cli_add_test test_name source_file )

    add_executable        ( ${test_name} ${source_file} )
    target_link_libraries ( ${test_name} PRIVATE vje_cli Qt6::Test vje_warnings )
    add_test              ( NAME vje_cli.${test_name} COMMAND ${test_name} )

endfunction ()

vje_cli_add_test ( tst_launch_plan  tst_launch_plan.cpp )
vje_cli_add_test ( tst_cli_commands tst_cli_commands.cpp )

# The Explorer pair's cases clear their scratch registry root with advapi32's RegDeleteTreeW.

if ( WIN32 )
    target_link_libraries ( tst_cli_commands PRIVATE advapi32 )
endif ()

# The built executables. Qt Core only -- it drives them through QProcess and reads their headers as bytes -- and it
# depends on them, so `cmake --build` of the suite brings them up to date first.

add_executable        ( tst_cli_process tst_cli_process.cpp )
target_link_libraries ( tst_cli_process PRIVATE Qt6::Core Qt6::Test vje_warnings )
add_dependencies      ( tst_cli_process vje_app )

target_compile_definitions ( tst_cli_process PRIVATE VJE_WINDOW_PROGRAM="$<TARGET_FILE:vje_app>" )

if ( WIN32 )

    # The stand-in for vje.exe that the twin's hand-off is observed through (handoff_probe.cpp). A window program with
    # no Qt, so what it records is exactly what Windows handed it.

    add_executable        ( vje_handoff_probe WIN32 handoff_probe.cpp )
    target_link_libraries ( vje_handoff_probe PRIVATE shell32 )

    add_dependencies           ( tst_cli_process vje_console vje_handoff_probe )
    target_compile_definitions ( tst_cli_process PRIVATE
        VJE_CONSOLE_PROGRAM="$<TARGET_FILE:vje_console>"
        VJE_HANDOFF_PROBE="$<TARGET_FILE:vje_handoff_probe>"
    )

endif ()

add_test ( NAME vje_cli.tst_cli_process COMMAND tst_cli_process )
