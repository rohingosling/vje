#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_settings (the settings store)
# Version: 2.0.0
# Date:    2024
# Author:  Rohin Gosling
#
# Description:
#
#   SettingsStore: the typed accessors over VJE's one per-user settings file (NFR-06, architecture.md section 4.5),
#   and the keys and stored values every reader and writer of that file shares.
#
#   A LIBRARY OF ITS OWN SINCE PHASE 15j, moved out of vje_app/services. The Explorer pair (CLI-08) is the one set of
#   commands allowed to change a preference -- it keeps SET-15 in agreement with the registry -- so the command line has
#   to reach the file the window writes, and the Windows console twin links vje_cli and nothing that draws. The store
#   was already Qt Core only; what changed is only where it lives, so vje_cli can link it without linking vje_app.
#
#-----------------------------------------------------------------------------------------------------------------------

add_library ( vje_settings STATIC
    SettingsStore.hpp
    SettingsStore.cpp
)

# Consumers include headers as <vje_settings/...>, so the include root is the parent src/ directory -- vje_core's rule.

target_include_directories ( vje_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. )

target_link_libraries ( vje_settings
    PUBLIC  Qt6::Core
    PRIVATE vje_warnings
)

if ( VJE_BUILD_TESTS )
    add_subdirectory ( tests )
endif ()
