#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- platform tests (Qt Test + CTest)
# Version: 2.0.0
# Date:    2024
# Author:  Rohin Gosling
#
# Description:
#
#   The platform layer's suites (Phase 15j). Two, because they make different kinds of claim:
#
#     tst_explorer_registration_plan   HEADLESS and on every platform: the registration plan is pure data, so what the
#                                      entries run and which keys they touch is asserted without a registry.
#
#     tst_explorer_integration         WINDOWS ONLY: the plan applied to and removed from a scratch registry root, so
#                                      the calls are shown to do what the plan says.
#
#   console_output and process_arguments are covered through the command line's suites (src/vje_cli/tests), which is
#   where their behaviour is observable.
#
#-----------------------------------------------------------------------------------------------------------------------

function ( vje_platform_add_test test_name source_file )

    add_executable        ( ${test_name} ${source_file} )
    target_link_libraries ( ${test_name} PRIVATE vje_platform Qt6::Test vje_warnings )
    add_test              ( NAME vje_platform.${test_name} COMMAND ${test_name} )

endfunction ()

vje_platform_add_test ( tst_explorer_registration_plan tst_explorer_registration_plan.cpp )

if ( WIN32 )

    vje_platform_add_test ( tst_explorer_integration tst_explorer_integration.cpp )

    # The scratch-root cases set and restore a key's DACL, which is advapi32's.

    target_link_libraries ( tst_explorer_integration PRIVATE advapi32 )

endif ()
