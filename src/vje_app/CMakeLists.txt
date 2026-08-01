#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_app (Qt Widgets application)
# Version: 2.0.0
# Date:    2024
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
#   the pure indentation and pointer-index helpers, and CodeView's validation / commit lifecycle). Phase 10 adds the file
#   lifecycle and the configuration UI: the IDialogService modal seam and its Qt implementation, BackgroundIo (where a
#   load or save actually runs, NFR-04), FileController (New / Open / Save / Save As / Close, the FILE-08 gate, Recent
#   Files, and the import / export pipelines over the registered converter table), the master-detail Settings dialog over
#   a declarative schema, and the opt-in DiagnosticLog. Phase 11 adds search and pointer navigation: FindController (the
#   FIND-01..04 policy over vje_core's SearchService), the FindBar docked above the editor pane, and the GoToDialog. The
#   remaining command handlers land in later phases.
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
    services/ClipboardService.hpp
    services/ClipboardService.cpp

    # The file lifecycle (Phase 10): the modal seam, where I/O runs, and the controller that sequences the commands.
    services/IDialogService.hpp
    services/DialogService.hpp
    services/DialogService.cpp
    services/BackgroundIo.hpp
    services/BackgroundIo.cpp
    controllers/converters.hpp
    controllers/converters.cpp
    controllers/FileController.hpp
    controllers/FileController.cpp

    # Import XML to JSON (FILE-13, Phase 12): what the dialog means, then the dialog.
    controllers/XmlImportController.hpp
    controllers/XmlImportController.cpp
    dialogs/XmlImportDialog.hpp
    dialogs/XmlImportDialog.cpp

    # Printing (FILE-12, Phase 13): what a view hands the printer, how that becomes a page, and the two commands.
    printing/print_content.hpp
    printing/print_html.hpp
    printing/print_html.cpp
    printing/print_wrapping.hpp
    printing/print_wrapping.cpp
    printing/page_furniture.hpp
    printing/page_furniture.cpp
    controllers/PrintController.hpp
    controllers/PrintController.cpp

    # Find and Go To (Phase 11): the policy, the docked bar, and the pointer dialog.
    controllers/FindController.hpp
    controllers/FindController.cpp
    views/FindBar.hpp
    views/FindBar.cpp
    dialogs/GoToDialog.hpp
    dialogs/GoToDialog.cpp

    # The Settings dialog (SET-01..09): the schema and its edit buffer, then the renderer over them.
    dialogs/settings_schema.hpp
    dialogs/settings_schema.cpp
    dialogs/SettingsDialog.hpp
    dialogs/SettingsDialog.cpp
    dialogs/TransferListEditor.hpp
    dialogs/TransferListEditor.cpp

    # Opt-in diagnostic logging (SET-09).
    services/DiagnosticLog.hpp
    services/DiagnosticLog.cpp

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
    style/card_surface.hpp
    style/card_surface.cpp
    style/FluentStyle.hpp
    style/FluentStyle.cpp
    style/FocusHighlight.hpp
    style/FocusHighlight.cpp
    views/Card.hpp
    views/Card.cpp
    views/PaneHeader.hpp
    views/PaneHeader.cpp
    views/ShadedTabBar.hpp
    views/ShadedTabBar.cpp
    views/PaneCycler.hpp
    views/PaneCycler.cpp
    views/toolbar_catalogue.hpp
    views/toolbar_catalogue.cpp
    views/toolbar_plan.hpp
    views/toolbar_plan.cpp
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
    views/JsonEscapeValidator.hpp
    views/JsonEscapeValidator.cpp
    views/JsonCellDelegate.hpp
    views/JsonCellDelegate.cpp
    views/cell_paste_plan.hpp
    views/cell_paste_plan.cpp
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
# The bundled icon set. Committed assets, compiled into the binary so the app has no external asset directory to locate
# at run time. CONFIGURE_DEPENDS re-runs the globs when a file is added or removed, so regenerating the set does not need
# a manual reconfigure.
#
# THE SET IS COMMITTED IN TWO FORMS AND BOTH ARE COMPILED IN:
#
#   png/<grid>/<size>/<name>.png   the artwork, drawn by hand
#   svg/<grid>/<name>.svg          traced from it, exactly      (tools/trace_png_to_svg.py)
#
# config::icons::ARTWORK_SOURCE names which of the two is the artwork -- the direction above reversed on 2026-07-31 and
# could reverse again -- while which one IconLibrary READS is a run-time choice starting at
# config::icons::DEFAULT_ICON_SOURCE and moved by the Settings dialog's Debug group. Compiling both is what makes the
# second a setting rather than a build-system change; see AppConfig.hpp. The cost is the unused form carried in the
# binary either way (~25 KB of SVG, ~47 KB of PNG at the base rungs), which is the accepted price of the fallback
# being reachable -- and of the icon author being able to see the file they drew.
#
# BASE is the icons root for both, so each file keeps its own subtree in the resource path and the resource tree mirrors
# the asset tree exactly: ":/vje/icons/svg/16/go-up.svg", ":/vje/icons/png/16/32/go-up.png".
#
# VJE_ICON_* are consumed by tests/ as well (variables flow into add_subdirectory), so the icon-backed test target
# compiles the same resources rather than a duplicate list.
#-----------------------------------------------------------------------------------------------------------------------

set ( VJE_ICON_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/images/icons" )

# One subdirectory per authored grid (see config::icons::GRIDS), and under the PNG tree one further level per rendered
# size. The rungs above each master's own size are optional -- IconLibrary skips what is absent -- so these globs are
# deliberately not asserted against an expected file count.

file ( GLOB VJE_ICON_SVG_FILES CONFIGURE_DEPENDS "${VJE_ICON_DIRECTORY}/svg/*/*.svg"   )
file ( GLOB VJE_ICON_PNG_FILES CONFIGURE_DEPENDS "${VJE_ICON_DIRECTORY}/png/*/*/*.png" )

if ( NOT VJE_ICON_SVG_FILES )
    message ( FATAL_ERROR "No icons found under ${VJE_ICON_DIRECTORY}/svg/<grid>/. The committed SVG icon assets are missing." )
endif ()

if ( NOT VJE_ICON_PNG_FILES )
    message ( FATAL_ERROR "No icons found under ${VJE_ICON_DIRECTORY}/png/<grid>/<size>/. Run tools/export_icon_pngs after regenerating the SVGs." )
endif ()

qt_add_resources ( vje_app "vje_icons_svg"
    PREFIX "/vje/icons"
    BASE   "${VJE_ICON_DIRECTORY}"
    FILES  ${VJE_ICON_SVG_FILES}
)

qt_add_resources ( vje_app "vje_icons_png"
    PREFIX "/vje/icons"
    BASE   "${VJE_ICON_DIRECTORY}"
    FILES  ${VJE_ICON_PNG_FILES}
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
        Qt6::PrintSupport
        vje_warnings
)

# Keep a console subsystem for now so `vje_app --version` prints to the terminal (the Phase 0 smoke check).
# The GUI (windowed, no-console) subsystem is selected at packaging time (Phase 16).

set_target_properties ( vje_app PROPERTIES WIN32_EXECUTABLE OFF )

# Headless Qt Test targets for the app-side services that carry no widget dependency (Phase 5).

if ( VJE_BUILD_TESTS )
    add_subdirectory ( tests )
endif ()
