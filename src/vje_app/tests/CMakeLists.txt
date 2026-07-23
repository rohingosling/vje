#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_app tests (Qt Test + CTest)
# Version: 2.0.0
# Date:    2026
# Author:  Rohin Gosling
#
# Description:
#
#   Qt Test targets for the app-side services. Two flavours:
#
#     vje_app_add_test     -- HEADLESS. For services depending on Qt Core only (SettingsStore). Compiles the service
#                             source directly and links Qt6::Core + Qt6::Test, mirroring the vje_core suites.
#
#     vje_app_add_gui_test -- For units needing a QApplication (a QStyle, a QPalette, a QPixmap). Runs under the
#                             "offscreen" QPA platform, so it needs no display and stays CI-safe on both platforms.
#
#   Widget-level UI smoke (driving the shell with QTest::keyClick / mouseClick) remains manual and late by
#   design. What the GUI targets here cover is style METRICS and icon RENDERING -- both fully
#   deterministic, and both places where a silent regression (a missing Qt SVG module, a lost override) would
#   otherwise only surface by eye.
#
#-----------------------------------------------------------------------------------------------------------------------

# Each unit is a small Qt Test executable registered with CTest under a vje_app.* name. The service sources it needs
# are compiled straight into the test target (they are self-contained).

function ( vje_app_add_test test_name )

    add_executable        ( ${test_name} ${ARGN} )
    target_include_directories ( ${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. )
    target_link_libraries ( ${test_name} PRIVATE Qt6::Core Qt6::Test vje_warnings )
    add_test              ( NAME vje_app.${test_name} COMMAND ${test_name} )

endfunction ()

# As above, but linking Widgets + Svg and forcing the offscreen platform plugin so the test needs no display server.

function ( vje_app_add_gui_test test_name )

    add_executable        ( ${test_name} ${ARGN} )
    target_include_directories ( ${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. )
    target_link_libraries ( ${test_name} PRIVATE Qt6::Widgets Qt6::Svg Qt6::Test vje_warnings )
    add_test              ( NAME vje_app.${test_name} COMMAND ${test_name} )

    set_tests_properties ( vje_app.${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" )

endfunction ()

#-----------------------------------------------------------------------------------------------------------------------
# Targets
#-----------------------------------------------------------------------------------------------------------------------

vje_app_add_test ( tst_settings_store
    tst_settings_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

vje_app_add_gui_test ( tst_fluent_style
    tst_fluent_style.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

vje_app_add_gui_test ( tst_icon_library
    tst_icon_library.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

# The focus-driven selection bar (STYLE-12). Its whole reason to exist is that Qt answers WINDOW activation and not
# widget focus, so the suite drives real focus moves between two views and reads the resulting palettes -- which needs
# a QApplication but no display, and is exactly the kind of "silent regression, visible only by eye" the GUI harness is
# here for. ThemeService supplies the Inactive colours the class reads, so it is compiled in rather than faked.

vje_app_add_gui_test ( tst_focus_highlight
    tst_focus_highlight.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FocusHighlight.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FocusHighlight.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/PaneHeader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/PaneHeader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tab_surface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tab_surface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/ShadedTabBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/ShadedTabBar.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

# The splitter grip (STYLE-04). Both claims are about rendered pixels -- where the grip sits and how far its tone is
# from the background -- so the suite grabs the widget and reads them back.

vje_app_add_gui_test ( tst_workspace_splitter
    tst_workspace_splitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/WorkspaceSplitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/WorkspaceSplitter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

# Tab between the workspace panes (NAV-04). No document and no project models -- the class knows only widgets and a
# ring of callbacks, so the suite is the class plus stand-in QWidgets.

vje_app_add_gui_test ( tst_pane_cycler
    tst_pane_cycler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/PaneCycler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/PaneCycler.hpp
)

# The tree model (Phase 6). It projects a JsonDocument and is driven through the real edit commands,
# so unlike the other app-side targets it links vje_core. The icon library is passed as null in the test, so no icon
# resource is needed here -- the TREE-03 mapping is asserted through the static type_icon_name().

vje_app_add_gui_test ( tst_json_tree_model
    tst_json_tree_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonTreeModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonTreeModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

target_link_libraries ( tst_json_tree_model PRIVATE vje_core )

# The tree PANE (Phase 6). What it covers is the behaviour that lives in the view rather than the
# model -- expansion / selection preservation across a projection rebuild, and the reveal-intent rule. QTreeView tracks
# expansion without ever being shown, so these run offscreen on both platforms rather than being left to manual smoke.

vje_app_add_gui_test ( tst_tree_view_pane
    tst_tree_view_pane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/TreeViewPane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/TreeViewPane.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/PaneHeader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/PaneHeader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tab_surface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tab_surface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FocusHighlight.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FocusHighlight.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/NodeContextActions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/NodeContextActions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonTreeModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonTreeModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

target_link_libraries ( tst_tree_view_pane PRIVATE vje_core )

#-----------------------------------------------------------------------------------------------------------------------
# Editor pane, Form View, and the array table (Phase 7).
#
# The Form View's pieces are shared by several of these targets, so the source lists are named once here rather than
# repeated per target -- the same reasoning as VJE_ICON_FILES above.
#-----------------------------------------------------------------------------------------------------------------------

set ( VJE_GRID_MODEL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/IGridProjection.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/cell_presentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/cell_presentation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/grid_navigation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/grid_navigation.hpp
)

# The presentation rule and the settings->profile reader are shared by the Form View and the Phase 8 views, so they
# are named once here for the same reason VJE_FORM_VIEW_SOURCES is.

set ( VJE_PRESENTATION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/node_presentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/node_presentation.hpp
)

set ( VJE_SETTINGS_PROFILE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

set ( VJE_FORM_VIEW_SOURCES
    ${VJE_GRID_MODEL_SOURCES}
    ${VJE_PRESENTATION_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonFormModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonFormModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonTableModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonTableModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonNumberValidator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonNumberValidator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonKeyValidator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonKeyValidator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonCellDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonCellDelegate.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/FormGridController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/FormGridController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FocusHighlight.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FocusHighlight.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/NodeContextActions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/NodeContextActions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.hpp
)

# The current-cell movement math is pure and Qt-Core-only, so it runs in the HEADLESS harness -- no QApplication, no
# platform plugin. That it can is the point: the landability and edge rules are stated where nothing can hide behind a
# widget's own behaviour.

vje_app_add_test ( tst_grid_navigation
    tst_grid_navigation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/grid_navigation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/grid_navigation.hpp
)

# The presentation rule and the settings->profile reader are shared by the Form View and the Phase 8 views, so they
# are named once here for the same reason VJE_FORM_VIEW_SOURCES is.

set ( VJE_PRESENTATION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/node_presentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/node_presentation.hpp
)

set ( VJE_SETTINGS_PROFILE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

vje_app_add_gui_test ( tst_json_table_model
    tst_json_table_model.cpp
    ${VJE_GRID_MODEL_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonTableModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonTableModel.hpp
)

target_link_libraries ( tst_json_table_model PRIVATE vje_core )

vje_app_add_gui_test ( tst_json_form_model
    tst_json_form_model.cpp
    ${VJE_GRID_MODEL_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonFormModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonFormModel.hpp
)

target_link_libraries ( tst_json_form_model PRIVATE vje_core )

vje_app_add_gui_test ( tst_json_cell_delegate
    tst_json_cell_delegate.cpp
    ${VJE_GRID_MODEL_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonFormModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../models/JsonFormModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonNumberValidator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonNumberValidator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonKeyValidator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonKeyValidator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonCellDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonCellDelegate.hpp
)

target_link_libraries ( tst_json_cell_delegate PRIVATE vje_core )

# The Form View drives real QTableViews with real key events, so unlike the model suites it needs the whole widget
# stack. It still runs offscreen -- an editor opens and is readable without a display server.

vje_app_add_gui_test ( tst_form_view
    tst_form_view.cpp
    ${VJE_FORM_VIEW_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/FormView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/FormView.hpp
)

target_link_libraries ( tst_form_view PRIVATE vje_core )

# The editor pane is driven by STUB views (defined in the test), which is what makes "a fourth view is one provider plus
# one registration" a checked claim rather than an intention -- the pane compiles and passes without the Form View.

vje_app_add_gui_test ( tst_editor_pane
    tst_editor_pane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FocusHighlight.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FocusHighlight.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/EditorPane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/EditorPane.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/ShadedTabBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/ShadedTabBar.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tab_surface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tab_surface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/IEditorView.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

target_link_libraries ( tst_editor_pane PRIVATE vje_core )

#-----------------------------------------------------------------------------------------------------------------------
# Text View and Code View (Phase 8).
#
# The two PURE helpers run in the headless harness for the same reason grid_navigation does: their edge cases -- an
# already-flush line, a malformed tail -- are stated where nothing can hide behind a widget's own behaviour.
#-----------------------------------------------------------------------------------------------------------------------

vje_app_add_test ( tst_code_indentation
    tst_code_indentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/code_indentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/code_indentation.hpp
)

target_link_libraries ( tst_code_indentation PRIVATE vje_core )

vje_app_add_test ( tst_json_text_index
    tst_json_text_index.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/json_text_index.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/json_text_index.hpp
)

target_link_libraries ( tst_json_text_index PRIVATE vje_core )

# The highlighter needs a QTextDocument, and the palette is measured from a QPalette -- so both need the GUI harness,
# and neither needs a display.

vje_app_add_gui_test ( tst_json_highlighter
    tst_json_highlighter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonHighlighter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonHighlighter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/CodeTokenPalette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/CodeTokenPalette.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
)

target_link_libraries ( tst_json_highlighter PRIVATE vje_core )

vje_app_add_gui_test ( tst_text_view
    tst_text_view.cpp
    ${VJE_PRESENTATION_SOURCES}
    ${VJE_SETTINGS_PROFILE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/TextView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/TextView.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/fixed_font.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/fixed_font.hpp
)

target_link_libraries ( tst_text_view PRIVATE vje_core )

# The Code View drives a real editor with real key events -- the indentation keys and the commit lifecycle are only
# meaningfully testable through the widget, since what they act on is a QTextDocument.

vje_app_add_gui_test ( tst_code_view
    tst_code_view.cpp
    ${VJE_SETTINGS_PROFILE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/CodeView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/CodeView.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/CodeEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/CodeEditor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonHighlighter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonHighlighter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/code_indentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/code_indentation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/json_text_index.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/json_text_index.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/CodeTokenPalette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/CodeTokenPalette.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/fixed_font.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/fixed_font.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.hpp
)

target_link_libraries ( tst_code_view PRIVATE vje_core )

# The tab shading rule (STYLE-13 / STYLE-14). Drives the real ThemeService palettes, so what is asserted is the shading
# the shipped themes actually produce rather than a fixture's.

vje_app_add_gui_test ( tst_tab_surface
    tst_tab_surface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tab_surface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tab_surface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/ShadedTabBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/ShadedTabBar.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

# The icon test rasterizes the REAL shipped assets rather than fixtures, so it compiles the same resource the
# application does. VJE_ICON_DIRECTORY / VJE_ICON_FILES come from the parent scope (src/vje_app/CMakeLists.txt).

qt_add_resources ( tst_icon_library "vje_icons_test"
    PREFIX "/vje/icons"
    BASE   "${VJE_ICON_DIRECTORY}"
    FILES  ${VJE_ICON_FILES}
)
