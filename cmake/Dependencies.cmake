#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- External dependency acquisition
# Version: 2.0.0
# Date:    2026
# Author:  Rohin Gosling
#
# Description:
#
#   Third-party dependency acquisition via CMake FetchContent (resolved decision, OQ-A4). Every library
#   VJE does not vendor is pulled in-tree from an explicit, pinned git tag, so the same versions build on Windows and
#   Linux (and in CI) with no external package manager to install.
#
#   The dependency surface is deliberately tiny. JSON uses VJE's own order- and number-token-preserving model and the
#   shared JsonLexer; CSV is a hand-rolled codec; XML uses Qt's built-in QXmlStreamReader -- none of these add a
#   dependency. The ONLY external library is yaml-cpp, for YAML import/export (Phase 4).
#
#   yaml-cpp is fetched only when VJE_ENABLE_YAML is ON, which keeps the Phase 0 configure fully offline and fast.
#   Phase 4 flips VJE_ENABLE_YAML ON (in the preset or on the command line) and links the convert module against it.
#
#-----------------------------------------------------------------------------------------------------------------------

include ( FetchContent )

option ( VJE_ENABLE_YAML "Fetch yaml-cpp and build YAML import/export" OFF )

if ( VJE_ENABLE_YAML )

    FetchContent_Declare ( yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
    )

    # Build only the library -- not yaml-cpp's own tests, tools, or install rules.

    set ( YAML_CPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE )
    set ( YAML_CPP_BUILD_TOOLS   OFF CACHE BOOL "" FORCE )
    set ( YAML_CPP_INSTALL       OFF CACHE BOOL "" FORCE )

    # yaml-cpp 0.8.0 declares cmake_minimum_required(VERSION 3.4), which CMake 4.0+ rejects (compatibility with
    # CMake < 3.5 was removed). Pin the policy floor to 3.5 for this dependency's subtree so it configures under a
    # modern CMake without vendoring a patched copy or moving off the OQ-A4-locked 0.8.0 tag.

    set ( CMAKE_POLICY_VERSION_MINIMUM 3.5 )

    FetchContent_MakeAvailable ( yaml-cpp )

endif ()
