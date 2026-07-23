#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_core tests (Qt Test + CTest)
# Version: 2.0.0
# Date:    2026
# Author:  Rohin Gosling
#
# Description:
#
#   Headless Qt Test targets for vje_core, registered with CTest. Each test is a small executable linking vje_core
#   and Qt6::Test. Phase 0 ships one trivial test (the version API) to prove the build -> test pipeline is green;
#   the document-model, undo, and service suites arrive with their phases (Phases 1-4).
#
#-----------------------------------------------------------------------------------------------------------------------

# Each unit is a small Qt Test executable registered with CTest under a vje_core.* name.

function ( vje_core_add_test test_name source_file )

    add_executable        ( ${test_name} ${source_file} )
    target_link_libraries ( ${test_name} PRIVATE vje_core Qt6::Test )
    add_test              ( NAME vje_core.${test_name} COMMAND ${test_name} )

endfunction ()

vje_core_add_test ( tst_version        tst_version.cpp )
vje_core_add_test ( tst_json_node      tst_json_node.cpp )
vje_core_add_test ( tst_json_pointer   tst_json_pointer.cpp )
vje_core_add_test ( tst_json_lexer     tst_json_lexer.cpp )
vje_core_add_test ( tst_json_parser    tst_json_parser.cpp )
vje_core_add_test ( tst_json_serializer tst_json_serializer.cpp )
vje_core_add_test ( tst_json_document  tst_json_document.cpp )
vje_core_add_test ( tst_edit_transforms tst_edit_transforms.cpp )
vje_core_add_test ( tst_edit_commands  tst_edit_commands.cpp )
vje_core_add_test ( tst_undo_controller tst_undo_controller.cpp )

# Core services (Phase 3).
vje_core_add_test ( tst_json_formatter     tst_json_formatter.cpp )
vje_core_add_test ( tst_document_io        tst_document_io.cpp )
vje_core_add_test ( tst_validator          tst_validator.cpp )
vje_core_add_test ( tst_search_service     tst_search_service.cpp )
vje_core_add_test ( tst_text_view_renderer tst_text_view_renderer.cpp )
vje_core_add_test ( tst_shape_comparer     tst_shape_comparer.cpp )
vje_core_add_test ( tst_cell_paste_converter tst_cell_paste_converter.cpp )

# Import/Export converters (Phase 4). The YAML suite builds only when yaml-cpp was fetched (VJE_ENABLE_YAML).
vje_core_add_test ( tst_csv_codec    tst_csv_codec.cpp )
vje_core_add_test ( tst_xml_exporter tst_xml_exporter.cpp )
vje_core_add_test ( tst_xml_importer tst_xml_importer.cpp )

if ( VJE_ENABLE_YAML )
    vje_core_add_test ( tst_yaml_codec tst_yaml_codec.cpp )
endif ()
