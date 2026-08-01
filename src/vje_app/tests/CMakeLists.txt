#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_app tests (Qt Test + CTest)
# Version: 2.0.0
# Date:    2024
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

# The file lifecycle (Phase 10). HEADLESS, and that it can be is the point: behind IDialogService the controller has no
# widget and no native modal in it, so every branch of the FILE-08 gate is an ordinary assertion rather than a manual
# click. ImmediateIo stands in for the thread-pool runner, which is what makes the cases deterministic.

vje_app_add_test ( tst_file_controller
    tst_file_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/FileController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/FileController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/BackgroundIo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/BackgroundIo.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IDialogService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/XmlImportController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/XmlImportController.hpp
)

target_link_libraries ( tst_file_controller PRIVATE vje_core )

#-----------------------------------------------------------------------------------------------------------------------
# Import XML to JSON (FILE-13, Phase 12). The dialog's whole decision surface, HEADLESS -- which is the phase's design
# stated as a build rule: the strategy list, the preselection, the preview, the notes and the truncation are the
# controller's, and none of them needs a screen. The dialog itself is covered separately, offscreen, below.
#-----------------------------------------------------------------------------------------------------------------------

vje_app_add_test ( tst_xml_import_controller
    tst_xml_import_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/XmlImportController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/XmlImportController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

target_link_libraries ( tst_xml_import_controller PRIVATE vje_core )

# The dialog: which gesture reaches which controller call, and which control answers which state. It builds a real
# CodeEditor and a real highlighter, so it needs the widget stack -- but not a display, and it asserts nothing about
# keyboard focus (lessons-learned Q10).

vje_app_add_gui_test ( tst_xml_import_dialog
    tst_xml_import_dialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dialogs/XmlImportDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dialogs/XmlImportDialog.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/XmlImportController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/XmlImportController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/CodeEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/CodeEditor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonHighlighter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonHighlighter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/code_indentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/code_indentation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/CodeTokenPalette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/CodeTokenPalette.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/fixed_font.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/fixed_font.hpp
)

target_link_libraries ( tst_xml_import_dialog PRIVATE vje_core )

#-----------------------------------------------------------------------------------------------------------------------
# Printing (FILE-12, Phase 13). Two targets for two separable claims: what a printed page LOOKS like (pure, headless)
# and what a print JOB does (a real PDF through a real QPrinter).
#-----------------------------------------------------------------------------------------------------------------------

# Every decision about the appearance of a printed page, with no printer, no QApplication and no paper anywhere near
# it. That it can run in the HEADLESS harness is the phase's design stated as a build rule.

vje_app_add_test ( tst_print_html
    tst_print_html.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_html.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_html.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_content.hpp
)

# Wrapping preformatted text under each line's own indentation -- the Code View's half of "the printed page is wrapped
# at the PAGE's width". Pure and Qt-Core-only, so it runs headless alongside print_html.

vje_app_add_test ( tst_print_wrapping
    tst_print_wrapping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_wrapping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_wrapping.hpp
)

# The commands themselves. It compiles FileController for one free function -- document_display_name, which is what
# makes the page header name the document the way the title bar and the status bar do rather than forming a fourth
# opinion. The session printer is pointed at a PDF, so the page count and the bytes are both real.

vje_app_add_gui_test ( tst_print_controller
    tst_print_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/PrintController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/PrintController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_html.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_html.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_content.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_wrapping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_wrapping.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/page_furniture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/page_furniture.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/fixed_font.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/fixed_font.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/FileController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/FileController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/XmlImportController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/XmlImportController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/BackgroundIo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/BackgroundIo.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IDialogService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

target_link_libraries ( tst_print_controller PRIVATE vje_core Qt6::PrintSupport )

#-----------------------------------------------------------------------------------------------------------------------
# Find and Go To (Phase 11). Two targets for two separable claims: what Find MEANS (the controller) and what the bar
# WIRES to it (the widget).
#-----------------------------------------------------------------------------------------------------------------------

# The match list, the wrap, the re-query position rule, the snapshot re-anchoring, and all three FIND-04 outcomes are
# ordinary assertions with no display anywhere near them -- the phase's design in one line, and this suite is where it
# pays off.
#
# It nonetheless runs in the OFFSCREEN GUI harness rather than the headless one, for exactly the reason tst_clipboard
# _service does: FIND-05 puts a QClipboard in the controller, and QClipboard is Qt Gui. Nothing here draws anything, and
# one case drives the real offscreen clipboard end to end -- which is the case worth having, since "the text Go To
# accepts is the text that lands on the clipboard" is the whole claim.

vje_app_add_gui_test ( tst_find_controller
    tst_find_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/FindController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/FindController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ClipboardService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ClipboardService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.hpp
)

target_link_libraries ( tst_find_controller PRIVATE vje_core )

# The bar itself: which gesture reaches which controller call. It drives real key events into a real QLineEdit, so it
# needs the widget stack -- but not a display, and it asserts nothing about keyboard focus (lessons-learned Q10). The
# icon library is compiled in and passed as null, so no icon resource is needed here.

vje_app_add_gui_test ( tst_find_bar
    tst_find_bar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/FindBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/FindBar.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/FindController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/FindController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ClipboardService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ClipboardService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/StatusService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/IconLibrary.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ThemeService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/FluentStyle.hpp
)

target_link_libraries ( tst_find_bar PRIVATE vje_core )

# The Settings dialog's CONTENT (SET-01..09), which is data rather than widgets -- so the group order, every default,
# the SET-09 dependency, and the all-or-nothing apply are pinned without ever opening a dialog. It compiles the profile
# readers too, because its most valuable case is the one that compares the schema's defaults against theirs.
#
# It runs in the offscreen GUI harness for one reason: QAction lives in QtGui in Qt 6, and SET-04's Toolbar group is
# built from the window's actions. Nothing here draws anything.

vje_app_add_gui_test ( tst_settings_schema
    tst_settings_schema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dialogs/settings_schema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dialogs/settings_schema.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_catalogue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_catalogue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/converters.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/settings_profiles.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

target_link_libraries ( tst_settings_schema PRIVATE vje_core )

#-----------------------------------------------------------------------------------------------------------------------
# Toolbar customization (SET-04, Phase 10.5). Three targets for three separable claims: which commands are eligible and
# how a stored layout is read (catalogue), what the bar renders for a layout (plan), and what the control does to a
# layout (editor). The first two are pure; the third is a widget, and drives one offscreen.
#-----------------------------------------------------------------------------------------------------------------------

# The shipped layout, the normalization a stored layout goes through, and the one-time migration off the superseded
# per-button visibility keys -- which runs against a file this build will never write again, so nothing but a test can
# notice it failing.

vje_app_add_gui_test ( tst_toolbar_catalogue
    tst_toolbar_catalogue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_catalogue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_catalogue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

# Which buttons and separators the toolbar carries for a given layout. Pure, so every way to end up with a stray rule --
# and a user can now author them directly -- is stated here rather than inside a widget rebuild.

vje_app_add_gui_test ( tst_toolbar_plan
    tst_toolbar_plan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_plan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_plan.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_catalogue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_catalogue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

# The transfer-list control itself. The result IS the left list's contents, so there is no pure helper to drive -- but
# selection, insertion and ordering are all model state, and none of it needs a display or keyboard focus.

vje_app_add_gui_test ( tst_transfer_list_editor
    tst_transfer_list_editor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dialogs/TransferListEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dialogs/TransferListEditor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dialogs/settings_schema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dialogs/settings_schema.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_catalogue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/toolbar_catalogue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SettingsStore.hpp
)

# Opt-in diagnostic logging (SET-09). The default folder is injected, so the suite writes only inside its temp dir.

vje_app_add_test ( tst_diagnostic_log
    tst_diagnostic_log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/DiagnosticLog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/DiagnosticLog.hpp
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

# The workspace card (STYLE-01/02/05). The claim that needed the machinery -- rounded corners over square content --
# is invisible to a test that inspects widgets rather than pixels, so the suite grabs the card and reads them back.

vje_app_add_gui_test ( tst_card
    tst_card.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/Card.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/Card.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/card_surface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/card_surface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../style/tone.hpp
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
# repeated per target -- the same reasoning as VJE_ICON_SVG_FILES / VJE_ICON_PNG_FILES above.
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonEscapeValidator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonEscapeValidator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonCellDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonCellDelegate.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/cell_paste_plan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/cell_paste_plan.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/FormGridController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/FormGridController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ClipboardService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ClipboardService.hpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonEscapeValidator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonEscapeValidator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonCellDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/JsonCellDelegate.hpp
)

target_link_libraries ( tst_json_cell_delegate PRIVATE vje_core )

# The cell-paste decision (EDITOR-11). Pure composition of CellPasteConverter + JsonShapeComparer + the jagged flow, so
# it runs in the HEADLESS harness -- the whole paste policy pinned where nothing hides behind a message box.

vje_app_add_test ( tst_cell_paste_plan
    tst_cell_paste_plan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/cell_paste_plan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/cell_paste_plan.hpp
)

target_link_libraries ( tst_cell_paste_plan PRIVATE vje_core )

# The dual-format clipboard (EDIT-06 / EDITOR-11). The pure codec runs over QMimeData (Qt Core), but ClipboardService
# links QClipboard (Qt Gui), and one case drives the offscreen clipboard end to end -- so it is a GUI test.

vje_app_add_gui_test ( tst_clipboard_service
    tst_clipboard_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ClipboardService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/ClipboardService.hpp
)

target_link_libraries ( tst_clipboard_service PRIVATE vje_core )

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../services/SelectionService.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/CodeView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../views/CodeView.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_wrapping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../printing/print_wrapping.hpp
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

# The icon test rasterizes the REAL shipped assets rather than fixtures, so it compiles the same resources the
# application does -- BOTH sources, so the suite can assert that the two agree whichever one the build selects.
# VJE_ICON_DIRECTORY / VJE_ICON_SVG_FILES / VJE_ICON_PNG_FILES come from the parent scope (src/vje_app/CMakeLists.txt).

qt_add_resources ( tst_icon_library "vje_icons_test_svg"
    PREFIX "/vje/icons"
    BASE   "${VJE_ICON_DIRECTORY}"
    FILES  ${VJE_ICON_SVG_FILES}
)

qt_add_resources ( tst_icon_library "vje_icons_test_png"
    PREFIX "/vje/icons"
    BASE   "${VJE_ICON_DIRECTORY}"
    FILES  ${VJE_ICON_PNG_FILES}
)
