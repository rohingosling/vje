#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_app (Qt Widgets application)
# Version: 2.0.0
# Date:    2026
# Author:  Rohin Gosling
#
# Description:
#
#   The Qt Widgets executable and its composition root. Phase 5 stood up the application shell (MainWindow: menus,
#   toolbar, workspace splitter, status bar) and the application services -- SettingsStore (schema-versioned JSON
#   settings), ThemeService (Light/Dark/System), SelectionService, and StatusService -- injected into MainWindow by
#   main.cpp. Phase 6 adds the navigation tree: JsonTreeModel (the document projection) and TreeViewPane (the QTreeView
#   and its selection / expansion / context-menu behaviour). Phase 7 adds the editor pane: the pluggable view registry
#   (EditorPane / IEditorView), the Form View with its object form and array table (JsonFormModel, JsonTableModel, the
#   shared JsonCellDelegate, and FormGridController's keyboard model). Phase 8 completes the view set: the read-only
#   TextView over vje_core's TextViewRenderer, and the Code View subsystem (CodeEditor + its gutter, JsonHighlighter,
#   the pure indentation and pointer-index helpers, and CodeView's validation / commit lifecycle). The remaining command
#   handlers land in later phases.
#
#-----------------------------------------------------------------------------------------------------------------------

qt_add_executable ( vje_app
    main.cpp
    MainWindow.hpp
    MainWindow.cpp

    # The single place compile-time tunables live (layout metrics, look and feel, first-run defaults, limits).
    AppConfig.hpp

    # Application services (Phase 5).
    services/SettingsStore.hpp
    services/SettingsStore.cpp
    services/ThemeService.hpp
    services/ThemeService.cpp
    services/SelectionService.hpp
    services/SelectionService.cpp
    services/StatusService.hpp
    services/StatusService.cpp
    services/IconLibrary.hpp
    services/IconLibrary.cpp

    # The single reader turning stored settings into vje_core rendering profiles (SET-06 / SET-07).
    services/settings_profiles.hpp
    services/settings_profiles.cpp

    # Look and feel.
    style/tone.hpp
    style/tone.cpp
    style/fixed_font.hpp
    style/fixed_font.cpp
    style/tab_surface.hpp
    style/tab_surface.cpp
    style/FluentStyle.hpp
    style/FluentStyle.cpp
    style/FocusHighlight.hpp
    style/FocusHighlight.cpp
    views/PaneHeader.hpp
    views/PaneHeader.cpp
    views/ShadedTabBar.hpp
    views/ShadedTabBar.cpp
    views/PaneCycler.hpp
    views/PaneCycler.cpp
    views/WorkspaceSplitter.hpp
    views/WorkspaceSplitter.cpp

    # Tree view pane (Phase 6).
    models/JsonTreeModel.hpp
    models/JsonTreeModel.cpp
    views/TreeViewPane.hpp
    views/TreeViewPane.cpp

    # Editor pane, Form View, and the array table (Phase 7).
    models/IGridProjection.hpp
    models/cell_presentation.hpp
    models/cell_presentation.cpp
    models/JsonFormModel.hpp
    models/JsonFormModel.cpp
    models/JsonTableModel.hpp
    models/JsonTableModel.cpp
    views/grid_navigation.hpp
    views/grid_navigation.cpp
    views/node_presentation.hpp
    views/node_presentation.cpp
    views/NodeContextActions.hpp
    views/NodeContextActions.cpp
    views/JsonNumberValidator.hpp
    views/JsonNumberValidator.cpp
    views/JsonKeyValidator.hpp
    views/JsonKeyValidator.cpp
    views/JsonCellDelegate.hpp
    views/JsonCellDelegate.cpp
    views/FormGridController.hpp
    views/FormGridController.cpp
    views/IEditorView.hpp
    views/FormView.hpp
    views/FormView.cpp
    views/EditorPane.hpp
    views/EditorPane.cpp

    # Text View and Code View (Phase 8).
    views/TextView.hpp
    views/TextView.cpp
    style/CodeTokenPalette.hpp
    style/CodeTokenPalette.cpp
    views/code_indentation.hpp
    views/code_indentation.cpp
    views/json_text_index.hpp
    views/json_text_index.cpp
    views/JsonHighlighter.hpp
    views/JsonHighlighter.cpp
    views/CodeEditor.hpp
    views/CodeEditor.cpp
    views/CodeView.hpp
    views/CodeView.cpp
)

# Consumers include app headers relative to the vje_app root (e.g. "services/SettingsStore.hpp").

target_include_directories ( vje_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} )

#-----------------------------------------------------------------------------------------------------------------------
# The bundled icon set. The SVGs are committed assets, compiled into the
# binary so the app has no external asset directory to locate at run time. CONFIGURE_DEPENDS re-runs the glob when a
# file is added or removed, so regenerating the set does not need a manual reconfigure.
#
# VJE_ICON_FILES is consumed by tests/ as well (variables flow into add_subdirectory), so the icon-backed test target
# compiles the same resource rather than a duplicate list.
#-----------------------------------------------------------------------------------------------------------------------

set ( VJE_ICON_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/images/icons" )

file ( GLOB VJE_ICON_FILES CONFIGURE_DEPENDS "${VJE_ICON_DIRECTORY}/*.svg" )

if ( NOT VJE_ICON_FILES )
    message ( FATAL_ERROR "No icons found in ${VJE_ICON_DIRECTORY}. The committed SVG icon assets are missing." )
endif ()

qt_add_resources ( vje_app "vje_icons"
    PREFIX "/vje/icons"
    BASE   "${VJE_ICON_DIRECTORY}"
    FILES  ${VJE_ICON_FILES}
)

# The application icon (tools/generate_app_icon.py). Compiled in so QApplication::setWindowIcon can set the window and
# taskbar icon on both platforms at run time. Embedding a Windows .ico in the executable and installing the Linux
# hicolor PNG set are packaging concerns (Phase 16).

qt_add_resources ( vje_app "vje_app_icon"
    PREFIX "/vje/app"
    BASE   "${CMAKE_SOURCE_DIR}/assets/images/app"
    FILES  "${CMAKE_SOURCE_DIR}/assets/images/app/vje.svg"
)

target_link_libraries ( vje_app
    PRIVATE
        vje_core
        Qt6::Widgets
        Qt6::Svg
        vje_warnings
)

# Keep a console subsystem for now so `vje_app --version` prints to the terminal (the Phase 0 smoke check).
# The GUI (windowed, no-console) subsystem is selected at packaging time (Phase 16).

set_target_properties ( vje_app PROPERTIES WIN32_EXECUTABLE OFF )

# Headless Qt Test targets for the app-side services that carry no widget dependency (Phase 5).

if ( VJE_BUILD_TESTS )
    add_subdirectory ( tests )
endif ()
