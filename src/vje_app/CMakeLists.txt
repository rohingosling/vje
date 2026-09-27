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

# Every source but main.cpp, named once so tests/tst_main_window can build the WHOLE window around its own composition
# root -- a second list would be a second opinion about what the window is made of, free to drift from this one.

set ( VJE_APP_SOURCES
    MainWindow.hpp
    MainWindow.cpp
    window_title.hpp
    window_title.cpp
    selection_summary.hpp
    selection_summary.cpp

    # The single place compile-time tunables live (layout metrics, look and feel, first-run defaults, limits).
    AppConfig.hpp

    # Application services (Phase 5).
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
    controllers/edit_reporting.hpp
    controllers/edit_reporting.cpp
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

    # The shared modal dialog structure (STYLE-15, Phase 15a): the frame every dialog VJE lays out itself is built
    # from, and the About dialog that replaced the last QMessageBox::about call.
    dialogs/dialog_frame.hpp
    dialogs/dialog_frame.cpp
    dialogs/AboutDialog.hpp
    dialogs/AboutDialog.cpp

    # One structure for every message box (STYLE-18, Phase 15j.1): the size floor, the inset, VJE's own captions and the
    # icon by kind. The button ORDER is FluentStyle's (LD-9).
    dialogs/MessageBox.hpp
    dialogs/MessageBox.cpp

    # One-line text prompts at a stated width: the Add commands' key, and Rename Key.
    dialogs/text_prompt.hpp
    dialogs/text_prompt.cpp

    # Title bars a step darker than their window, dark or light by VJE's theme (STYLE-19, Phase 15j.2). The platform
    # call is platform/title_bar; this is what to ask for, and the filter that asks it of every window.
    style/title_bar_surface.hpp
    style/title_bar_surface.cpp
    services/TitleBarSync.hpp
    services/TitleBarSync.cpp

    # The Settings dialog (SET-01..09): the schema and its edit buffer, then the renderer over them.
    dialogs/settings_schema.hpp
    dialogs/settings_schema.cpp
    dialogs/SettingsDialog.hpp
    dialogs/SettingsDialog.cpp
    dialogs/TransferListEditor.hpp
    dialogs/TransferListEditor.cpp

    # The group boxes the Settings dialog divides its pages into (SET-01c, Phase 15k.3): the outline and title drawn
    # from the palette rather than from Fusion's fixed frame image.
    dialogs/GroupBox.hpp
    dialogs/GroupBox.cpp

    # Opt-in diagnostic logging (SET-09).
    services/DiagnosticLog.hpp
    services/DiagnosticLog.cpp

    # Windows Explorer's menus kept in step with SET-15 (FILE-15, Phase 15j). Constructed by main.cpp alone.
    services/ExplorerIntegrationSync.hpp
    services/ExplorerIntegrationSync.cpp

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
    style/dialog_surface.hpp
    style/dialog_surface.cpp
    style/FluentStyle.hpp
    style/FluentStyle.cpp
    style/FocusHighlight.hpp
    style/FocusHighlight.cpp
    style/tooltip_text.hpp
    style/tooltip_text.cpp
    style/theme_colour.hpp
    style/theme_colour.cpp
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

    # Tree view pane (Phase 6), and its drag-to-reorder (EDIT-10, Phase 15f).
    models/JsonTreeModel.hpp
    models/JsonTreeModel.cpp
    views/TreeViewPane.hpp
    views/TreeViewPane.cpp
    views/TreeNodeDelegate.hpp
    views/TreeNodeDelegate.cpp
    views/ReorderTreeView.hpp
    views/ReorderTreeView.cpp
    views/tree_drop_plan.hpp
    views/tree_drop_plan.cpp

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
    views/GridHeaderView.hpp
    views/GridHeaderView.cpp
    views/IEditorView.hpp
    views/FormView.hpp
    views/FormView.cpp
    views/TabCloseButton.hpp
    views/TabCloseButton.cpp
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
    views/code_folding.hpp
    views/code_folding.cpp
    views/JsonHighlighter.hpp
    views/JsonHighlighter.cpp
    views/CodeEditor.hpp
    views/CodeEditor.cpp
    views/CodeView.hpp
    views/CodeView.cpp

    # The JSONPath Query view (QUERY-01..07, Phase 15d). The fourth registered view, and the first that is applicable
    # to a DOCUMENT rather than to the selected node.
    views/JsonPathView.hpp
    views/JsonPathView.cpp
)

qt_add_executable ( vje_app
    main.cpp
    ${VJE_APP_SOURCES}
)

# Consumers include app headers relative to the vje_app root (e.g. "services/ThemeService.hpp").

target_include_directories ( vje_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} )

#-----------------------------------------------------------------------------------------------------------------------
# THE SET IS TWO FAMILIES AND THREE TREES, and Settings > Appearance > Interface style picks a family (SET-13):
#
#   classic/png/<deviceSize>/<name>.png          VJE's own artwork, hand-drawn, one drawing per display scaling
#   fluent/microsoft/<designSize>/<name>.svg     vendored from Microsoft's set (tools/pull_microsoft_icons.py)
#   fluent/custom/svg/<designSize>/<name>.svg    VJE glyphs Microsoft has no equivalent for (tools/trace_png_to_svg.py)
#
# EACH TREE GETS ITS OWN PREFIX AND ITS OWN BASE, rather than one prefix over the icons root. Merging the two Fluent
# sub-trees would let a name held by both resolve by REGISTRATION ORDER -- silently, and precisely for the overlap this
# design creates while the vendoring is partly done. IconLibrary's microsoft-then-custom probe is the stated rule.
#
# THE GLOBS ARE <tree>/*/*.<ext> AND NOT RECURSIVE, deliberately: assets/images/icons also holds
# fluent/custom/svg-reference-png/ (the tracer's INPUT, 329 files that must never reach the binary) and the git-ignored
# _review/ contact sheets.
#
# VJE_ICON_* are consumed by tests/ as well (variables flow into add_subdirectory), so the icon-backed test target
# compiles the same resources rather than a duplicate list.
#-----------------------------------------------------------------------------------------------------------------------

set ( VJE_ICON_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/images/icons" )

file ( GLOB VJE_ICON_CLASSIC_FILES       CONFIGURE_DEPENDS "${VJE_ICON_DIRECTORY}/classic/png/*/*.png" )
file ( GLOB VJE_ICON_FLUENT_MS_FILES     CONFIGURE_DEPENDS "${VJE_ICON_DIRECTORY}/fluent/microsoft/*/*.svg" )
file ( GLOB VJE_ICON_FLUENT_CUSTOM_FILES CONFIGURE_DEPENDS "${VJE_ICON_DIRECTORY}/fluent/custom/svg/*/*.svg" )

if ( NOT VJE_ICON_CLASSIC_FILES )
    message ( FATAL_ERROR "No Classic icons under ${VJE_ICON_DIRECTORY}/classic/png/<deviceSize>/. These are the hand-drawn masters -- they are committed, not generated." )
endif ()

if ( NOT VJE_ICON_FLUENT_CUSTOM_FILES )
    message ( FATAL_ERROR "No custom Fluent icons under ${VJE_ICON_DIRECTORY}/fluent/custom/svg/<designSize>/. Run tools/trace_png_to_svg.py to trace them from the reference PNGs." )
endif ()

# A WARNING RATHER THAN AN ERROR, and that is what makes the vendoring incremental. An empty Microsoft tree is a
# legitimate state: the custom set covers every name, so Fluent is already complete and each vendored glyph replaces
# one without anything else landing with it. Failing configure here would block that entirely.

if ( NOT VJE_ICON_FLUENT_MS_FILES )
    message ( WARNING "No vendored Microsoft icons under ${VJE_ICON_DIRECTORY}/fluent/microsoft/<designSize>/. Fluent will be served entirely by the custom set. Run tools/pull_microsoft_icons.py." )
endif ()

# The CMake half of config::icons::LADDER's invariant. A missing Classic device size fails no build and no test -- it
# silently costs one display scaling, which QIcon covers by scaling a neighbour. That is exactly the class of defect
# this project turns into a configure or compile error rather than leaving to be noticed by eye.

foreach ( device_size 16 20 24 25 28 30 35 )   # == the deviceSize column of config::icons::LADDER
    if ( NOT IS_DIRECTORY "${VJE_ICON_DIRECTORY}/classic/png/${device_size}" )
        message ( FATAL_ERROR "Classic icons: no ${device_size} px directory under ${VJE_ICON_DIRECTORY}/classic/png/. It is a rung of config::icons::LADDER." )
    endif ()
endforeach ()

qt_add_resources ( vje_app "vje_icons_classic"
    PREFIX "/vje/icons/classic"
    BASE   "${VJE_ICON_DIRECTORY}/classic/png"
    FILES  ${VJE_ICON_CLASSIC_FILES}
)

qt_add_resources ( vje_app "vje_icons_fluent_microsoft"
    PREFIX "/vje/icons/fluent/microsoft"
    BASE   "${VJE_ICON_DIRECTORY}/fluent/microsoft"
    FILES  ${VJE_ICON_FLUENT_MS_FILES}
)

qt_add_resources ( vje_app "vje_icons_fluent_custom"
    PREFIX "/vje/icons/fluent/custom"
    BASE   "${VJE_ICON_DIRECTORY}/fluent/custom/svg"
    FILES  ${VJE_ICON_FLUENT_CUSTOM_FILES}
)

# The application icon -- HAND-DRAWN RASTER as of 2026-08-13, replacing the generated vector plate that
# tools/generate_app_icon.py drew into assets/images/app/. It is the author's artwork now, exactly like the Classic
# family, and for the same reason: it is authored as pixels. Compiled in so QApplication::setWindowIcon can set the
# window and taskbar icon on both platforms at run time. On Windows the same masters are also embedded in the
# executables as an icon resource (below); installing the Linux hicolor PNG set is a packaging concern (Phase 17).
#
# GLOBBED RATHER THAN NAMED, because an application icon is asked for at sizes nobody chooses -- a 16 px title bar, a
# 32 px taskbar, a 256 px file listing -- and a raster master answers exactly one of them. main.cpp puts every file in
# this directory into ONE QIcon, so drawing a second size is a drop-in with no code and no CMake edit. Today there is
# one master and Qt scales it.

file ( GLOB VJE_APP_ICON_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/images/icons/application-icon/*.png" )

if ( NOT VJE_APP_ICON_FILES )
    message ( FATAL_ERROR "No application icon under ${CMAKE_SOURCE_DIR}/assets/images/icons/application-icon/. It is hand-drawn artwork -- committed, not generated." )
endif ()

qt_add_resources ( vje_app "vje_app_icon"
    PREFIX "/vje/app"
    BASE   "${CMAKE_SOURCE_DIR}/assets/images/icons/application-icon"
    FILES  ${VJE_APP_ICON_FILES}
)

#-----------------------------------------------------------------------------------------------------------------------
# WINDOWS: THE SAME MASTERS, EMBEDDED AS THE EXECUTABLES' ICON RESOURCE (FILE-15, Phase 15j).
#
# Explorer draws an application's icon from the executable file -- the Explorer entries name "vje.exe",0, the first
# icon resource -- and QApplication::setWindowIcon above reaches only the running window, so without this both
# Explorer entries, the file listing and Open with showed a generic icon.
#
# pack_icon (windows_resources/) is built first and copies every master into one .ico UNRESAMPLED, as PNG entries; the
# glob above is its input, so a newly drawn master reaches the executable exactly as it reaches the window icon. The
# .ico is generated rather than committed, so it cannot drift from the PNGs. vje.exe alone carries it: the shell reads
# no icon from a .com file (measured; src/vje_console/CMakeLists.txt).
#-----------------------------------------------------------------------------------------------------------------------

if ( WIN32 )

    add_executable        ( vje_pack_icon windows_resources/pack_icon.cpp )
    target_link_libraries ( vje_pack_icon PRIVATE vje_warnings )

    set ( VJE_APPLICATION_ICO "${CMAKE_CURRENT_BINARY_DIR}/windows_resources/vje.ico" )
    set ( VJE_APPLICATION_RC  "${CMAKE_CURRENT_BINARY_DIR}/windows_resources/application_icon.rc" )

    add_custom_command (
        OUTPUT  "${VJE_APPLICATION_ICO}"
        COMMAND vje_pack_icon "${VJE_APPLICATION_ICO}" ${VJE_APP_ICON_FILES}
        DEPENDS vje_pack_icon ${VJE_APP_ICON_FILES}
        COMMENT "Packing the application-icon masters into vje.ico"
        VERBATIM
    )

    add_custom_target ( vje_application_icon DEPENDS "${VJE_APPLICATION_ICO}" )

    # One ICON statement. Its resource is the executable's first, which is what "vje.exe",0 names.

    file ( CONFIGURE OUTPUT "${VJE_APPLICATION_RC}" CONTENT "VJE_APPLICATION_ICON ICON \"${VJE_APPLICATION_ICO}\"\n" )

    set_source_files_properties ( "${VJE_APPLICATION_RC}" PROPERTIES OBJECT_DEPENDS "${VJE_APPLICATION_ICO}" )

    target_sources   ( vje_app PRIVATE "${VJE_APPLICATION_RC}" )
    add_dependencies ( vje_app vje_application_icon )

endif ()

target_link_libraries ( vje_app
    PRIVATE
        vje_core
        vje_cli
        vje_settings
        vje_platform
        Qt6::Widgets
        Qt6::Svg
        Qt6::PrintSupport
        vje_warnings
)

# THE EXECUTABLE IS `vje` (CLI-07). The target keeps its name -- tests and the VJE_APP_SOURCES list refer to it -- and
# only the file changes: vje.exe on Windows, vje on Linux, in the one directory the Windows console twin looks in.
#
# ON WINDOWS IT IS A WINDOW PROGRAM. Every launch that does not come from a terminal -- a shortcut, a double click,
# and from Phase 15j Explorer -- used to open a console window beside VJE, because the Phase 0 build kept the console
# subsystem so that --version could print. Printing is now vje.com's job, and vje.exe given a command still runs it
# and returns its exit code (main.cpp). WIN32_EXECUTABLE is ignored on Linux, where there is no such distinction.

set_target_properties ( vje_app PROPERTIES
    OUTPUT_NAME              vje
    WIN32_EXECUTABLE         ON
    RUNTIME_OUTPUT_DIRECTORY "${VJE_EXECUTABLE_DIRECTORY}"
)

# Headless Qt Test targets for the app-side services that carry no widget dependency (Phase 5).

if ( VJE_BUILD_TESTS )
    add_subdirectory ( tests )
endif ()
