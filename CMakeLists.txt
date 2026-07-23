#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE (Versatile JSON Editor) 2.0
# Version: 2.0.0
# Date:    2026
# Author:  Rohin Gosling
#
# Description:
#
#   Top-level CMake build for VJE 2.0 -- a native, cross-platform (Windows + Linux) desktop JSON editor built in
#   C++20 / Qt 6 Widgets with CMake + Ninja and GCC 13+. One codebase, one src/ tree (no per-OS split): the UI-free
#   vje_core domain library (Qt Core only), the vje_app Qt Widgets executable, and their Qt Test / CTest targets.
#
#   Configure / build / test via the presets in CMakePresets.json (see docs/build.md):
#     cmake --preset <preset>  |  cmake --build --preset <preset>  |  ctest --preset <preset> --output-on-failure
#
#-----------------------------------------------------------------------------------------------------------------------

cmake_minimum_required ( VERSION 3.21 )

project ( vje
    VERSION      2.0.0
    DESCRIPTION  "VJE (Versatile JSON Editor) 2.0 -- a native cross-platform desktop JSON editor"
    LANGUAGES    CXX
)

#-----------------------------------------------------------------------------------------------------------------------
# Language standard: C++20 on every target (GCC 13+; enables std::format). No compiler extensions.
#-----------------------------------------------------------------------------------------------------------------------

set ( CMAKE_CXX_STANDARD          20 )
set ( CMAKE_CXX_STANDARD_REQUIRED ON )
set ( CMAKE_CXX_EXTENSIONS        OFF )

# Emit compile_commands.json for clangd / IntelliSense (docs/build.md section 4).

set ( CMAKE_EXPORT_COMPILE_COMMANDS ON )

#-----------------------------------------------------------------------------------------------------------------------
# Build options.
#-----------------------------------------------------------------------------------------------------------------------

option ( VJE_BUILD_TESTS        "Build the Qt Test / CTest targets"                  ON  )
option ( VJE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI default: on)" OFF )

#-----------------------------------------------------------------------------------------------------------------------
# Qt 6.5+ (6.5 introduced QStyleHints::colorScheme(), required by the System theme).
# vje_core uses Core + Gui (Gui for the undo framework ONLY -- Qt 6 moved QUndoStack/QUndoCommand out of Qt Core into
# Qt Gui; the classes are headless QObjects that run under a QCoreApplication, so vje_core stays display-free and
# unit-testable, and links NO Widgets). vje_app adds Widgets; PrintSupport backs printing (Phase 13);
# Svg rasterizes the bundled icon set (QSvgRenderer, src/vje_app/services/IconLibrary) -- without it the .svg assets
# fail to load SILENTLY, so it is a hard requirement, not an optional extra; Test backs the CTest targets.
#-----------------------------------------------------------------------------------------------------------------------

find_package ( Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Test PrintSupport )

# Qt code-generation tooling (moc / rcc / uic) is driven automatically per target.

set ( CMAKE_AUTOMOC ON )
set ( CMAKE_AUTORCC ON )
set ( CMAKE_AUTOUIC ON )

#-----------------------------------------------------------------------------------------------------------------------
# Shared warning flags. Targets that carry our own code link this INTERFACE library privately; third-party code
# fetched by Dependencies.cmake does not, so its warnings never fail our build.
#-----------------------------------------------------------------------------------------------------------------------

add_library ( vje_warnings INTERFACE )

target_compile_options ( vje_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
)

if ( VJE_WARNINGS_AS_ERRORS )
    target_compile_options ( vje_warnings INTERFACE -Werror )
endif ()

#-----------------------------------------------------------------------------------------------------------------------
# External dependencies (CMake FetchContent -- resolved decision, OQ-A4).
#-----------------------------------------------------------------------------------------------------------------------

include ( cmake/Dependencies.cmake )

#-----------------------------------------------------------------------------------------------------------------------
# Test infrastructure (Qt Test + CTest -- OQ-A3).
#-----------------------------------------------------------------------------------------------------------------------

if ( VJE_BUILD_TESTS )
    enable_testing ()
endif ()

#-----------------------------------------------------------------------------------------------------------------------
# Targets. The layering is the schedule: the UI-free core first, then the Qt Widgets application on top.
#-----------------------------------------------------------------------------------------------------------------------

add_subdirectory ( src/vje_core )
add_subdirectory ( src/vje_app )
