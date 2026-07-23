#-----------------------------------------------------------------------------------------------------------------------
# Project: VJE 2.0 -- vje_core (UI-free domain library)
# Version: 2.0.0
# Date:    2026
# Author:  Rohin Gosling
#
# Description:
#
#   The UI-free domain library: Qt Core plus Qt Gui for the undo framework ONLY (Qt 6 moved QUndoStack/QUndoCommand
#   into Qt Gui; they are headless QObjects that run under a QCoreApplication). It links NO Qt Widgets and needs no
#   display, so it stays headlessly unit-testable. It holds the document model, the
#   edit commands + undo, and the services (I/O, validation, search, tokenizer, formatter, converters, cell-paste
#   conversion, shape comparer).
#
#   Phase 0 seeds it with the single-source-of-truth version API (version.hpp) so vje_app and the About box share
#   one version string, and the first CTest target has something real to assert against.
#
#-----------------------------------------------------------------------------------------------------------------------

add_library ( vje_core STATIC
    version.hpp
    version.cpp

    # Document model (Phase 1).
    document/JsonNode.hpp
    document/JsonNode.cpp
    document/JsonPointer.hpp
    document/JsonPointer.cpp
    document/JsonDocument.hpp
    document/JsonDocument.cpp

    # Text <-> model machinery: the shared scanner, the parser, the baseline serializer (Phase 1).
    services/JsonLexer.hpp
    services/JsonLexer.cpp
    services/JsonParser.hpp
    services/JsonParser.cpp
    services/JsonSerializer.hpp
    services/JsonSerializer.cpp

    # Editing and undo: the QUndoCommand family, the pure node transforms, and the UndoController (Phase 2).
    editing/edit_transforms.hpp
    editing/edit_transforms.cpp
    editing/EditCommands.hpp
    editing/EditCommands.cpp
    editing/UndoController.hpp
    editing/UndoController.cpp

    # Core services: I/O, validation, search, the format-profile pretty-printer, the read-only Text View renderer,
    # and the cell-paste type + shape engines (Phase 3).
    services/JsonFormatter.hpp
    services/JsonFormatter.cpp
    services/DocumentIo.hpp
    services/DocumentIo.cpp
    services/Validator.hpp
    services/Validator.cpp
    services/SearchService.hpp
    services/SearchService.cpp
    services/TextViewRenderer.hpp
    services/TextViewRenderer.cpp
    services/JsonShapeComparer.hpp
    services/JsonShapeComparer.cpp
    services/CellPasteConverter.hpp
    services/CellPasteConverter.cpp

    # Import/Export converters: the hand-rolled CSV codec, the simple readable XML export, and the four-strategy XML
    # import (Phase 4). The yaml-cpp-backed YamlCodec is added conditionally below (VJE_ENABLE_YAML).
    convert/CsvCodec.hpp
    convert/CsvCodec.cpp
    convert/XmlExporter.hpp
    convert/XmlExporter.cpp
    convert/XmlImporter.hpp
    convert/XmlImporter.cpp
)

# Consumers include headers as <vje_core/...>, so the include root is the parent src/ directory.

target_include_directories ( vje_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. )

# Qt6::Gui is linked ONLY for the undo framework (QUndoStack/QUndoCommand), which Qt 6 relocated from Qt Core to Qt
# Gui. Those classes are headless and run under a QCoreApplication, so vje_core stays display-free and links NO Widgets
# (see the top-level CMakeLists note).

target_link_libraries ( vje_core
    PUBLIC  Qt6::Core Qt6::Gui
    PRIVATE vje_warnings
)

# Version identity flows in from the CMake project version -- one source of truth.

target_compile_definitions ( vje_core PRIVATE
    VJE_APPLICATION_NAME="VJE"
    VJE_VERSION_STRING="${PROJECT_VERSION}"
)

# YAML import/export (Phase 4) is compiled only when yaml-cpp was fetched (VJE_ENABLE_YAML, cmake/Dependencies.cmake).
# The rest of the converter set (CSV, XML) has no external dependency and always builds. VJE_ENABLE_YAML is exported
# PUBLIC so consumers (vje_app, tests) can gate YAML-only code paths.

if ( VJE_ENABLE_YAML )

    target_sources ( vje_core PRIVATE
        convert/YamlCodec.hpp
        convert/YamlCodec.cpp
    )

    target_link_libraries      ( vje_core PRIVATE yaml-cpp )
    target_compile_definitions ( vje_core PUBLIC  VJE_ENABLE_YAML )

endif ()

if ( VJE_BUILD_TESTS )
    add_subdirectory ( tests )
endif ()
