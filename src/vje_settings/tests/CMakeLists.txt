#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_settings tests (Qt Test + CTest)
# Version: 2.0.0
# Date:    2024
# Author:  Rohin Gosling
#
# Description:
#
#   The settings store's suite. HEADLESS: the store depends on Qt Core only, and every case runs against a file in a
#   temporary directory. It moved here from vje_app/tests with the store itself (Phase 15j); its cases did not change.
#
#-----------------------------------------------------------------------------------------------------------------------

add_executable        ( tst_settings_store tst_settings_store.cpp )
target_link_libraries ( tst_settings_store PRIVATE vje_settings Qt6::Test vje_warnings )
add_test              ( NAME vje_settings.tst_settings_store COMMAND tst_settings_store )
