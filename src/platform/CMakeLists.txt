#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- platform (the isolated per-OS layer)
# Version: 2.0.0
# Date:    2024
# Author:  Rohin Gosling
#
# Description:
#
#   The small layer that holds VJE's genuine platform differences behind one interface each, with CMake choosing the
#   implementation (architecture.md section 12). It is a LIBRARY rather than a folder inside vje_app because the
#   command line (vje_cli) needs it and must not link Qt Widgets -- the Windows console twin links vje_cli and nothing
#   that draws.
#
#   Its residents: console_output, the one writer every command prints through (CLI-09), and process_arguments, the
#   command line as Unicode (CLI-02), both Phase 15i; and explorer_integration, VJE's entries in Windows Explorer's
#   menus (FILE-15, Phase 15j) -- the layer's first piece that exists on one platform and not the other.
#
#-----------------------------------------------------------------------------------------------------------------------

add_library ( vje_platform STATIC
    console_output.hpp
    process_arguments.hpp
    explorer_integration.hpp
    title_bar.hpp

    # The registration plan is pure and compiled everywhere, so its suite runs headlessly on Linux too.
    explorer_integration.cpp
)

# ONE FILE PER PLATFORM PER PIECE, chosen here, rather than #ifdef blocks inside one file. The Windows halves are the
# larger ones by far, and a reader of either implementation should not have to read around the other.

if ( WIN32 )

    target_sources ( vje_platform PRIVATE
        console_output_windows.cpp
        process_arguments_windows.cpp
        explorer_integration_windows.cpp
        title_bar_windows.cpp
    )

    # CommandLineToArgvW and SHChangeNotify live in shell32; the registry calls in advapi32; the title bar's colour
    # and dark flag (STYLE-19) are DWM window attributes, in dwmapi.
    target_link_libraries ( vje_platform PRIVATE shell32 advapi32 dwmapi )

else ()

    target_sources ( vje_platform PRIVATE
        console_output_posix.cpp
        process_arguments_posix.cpp
        explorer_integration_posix.cpp
        title_bar_posix.cpp
    )

endif ()

# Consumers include headers as <platform/...>, so the include root is the parent src/ directory -- vje_core's rule.

target_include_directories ( vje_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. )

target_link_libraries ( vje_platform
    PUBLIC  Qt6::Core
    PRIVATE vje_warnings
)

if ( VJE_BUILD_TESTS )
    add_subdirectory ( tests )
endif ()
