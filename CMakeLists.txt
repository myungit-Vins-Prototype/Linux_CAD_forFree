cmake_minimum_required(VERSION 3.21)
project(ForgeCAD LANGUAGES CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

include(CheckLanguage)
if(NOT CMAKE_CUDA_COMPILER AND EXISTS "/opt/cuda/bin/nvcc")
    set(CMAKE_CUDA_COMPILER "/opt/cuda/bin/nvcc" CACHE FILEPATH "CUDA compiler" FORCE)
endif()
check_language(CUDA)
if(CMAKE_CUDA_COMPILER AND NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES native)
endif()
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(FORGE_CAD_CUDA_SOURCE cuda_support.cu)
else()
    set(FORGE_CAD_CUDA_SOURCE cuda_support_cpu.cpp)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)
# OpenCASCADE serve solo ai test del kernel (riferimento per i confronti):
# l'applicazione usa soltanto il kernel proprio.
find_package(OpenCASCADE REQUIRED)

# Kernel geometrico proprio (kernel/), validato contro OpenCASCADE nei test.
enable_testing()
add_subdirectory(kernel)

add_executable(forgecad
    forgeCad2026.cpp
    forgeCad2026_gui.cpp
    forgeCad2026_gui.h
    cad_types.h
    cad_shape_analysis.h
    cad_shape_analysis.cpp
    cad_display_cache.h
    cad_display_cache.cpp
    cad_overlay_renderer.h
    cad_overlay_renderer.cpp
    cad_curve_solver.h
    cad_curve_solver.cpp
    cad_curve_extract.h
    cad_spatial_sketch.h
    cad_spatial_sketch.cpp
    cad_curve_extract.cpp
    cad_snap.h
    cad_snap.cpp
    cad_export.h
    cad_export.cpp
    cad_expression.h
    cad_expression.cpp
    cad_document_io.h
    cad_document_io.cpp
    cad_snapshot_chunks.h
    cad_snapshot_chunks.cpp
    cad_sketch_edit.h
    cad_sketch_edit.cpp
    cad_constraints.h
    cad_constraints.cpp
    cad_icons.h
    cad_icons.cpp
    cad_history.h
    cad_history.cpp
    cad_history_graph.h
    cad_history_graph.cpp
    cad_feature_dependencies.h
    cad_feature_dependencies.cpp
    cad_feature_build.h
    cad_feature_build.cpp
    cad_model_history.h
    cad_model_history.cpp
    cad_topology_ref.h
    cad_topology_ref.cpp
    cad_element_names.h
    cad_element_names.cpp
    cad_kernel.h
    cad_kernel.cpp
    cad_forge.h
    cad_forge.cpp
    cad_features.h
    cad_features.cpp
    cad_mass.h
    cad_mass.cpp
    cad_measure.h
    cad_measure.cpp
    cad_datum.h
    cad_datum.cpp
    cad_import.h
    cad_import.cpp
    cad_pattern.h
    cad_pattern.cpp
    cad_extrude.h
    cad_extrude.cpp
    cad_sketch_refs.h
    cad_sketch_refs.cpp
    cad_cuda_tessellation.h
    cad_cuda_tessellation.cpp
    cad_sketch_offset.h
    cad_sketch_offset.cpp
    cuda_support.h
    ${FORGE_CAD_CUDA_SOURCE}
    icons/forgecad.qrc
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(forgecad PRIVATE cad_macos_menu.h cad_macos_menu.mm)
    find_library(FORGECAD_APPKIT_FRAMEWORK AppKit REQUIRED)
    target_link_libraries(forgecad PRIVATE ${FORGECAD_APPKIT_FRAMEWORK})
    # Nome nativo anche avviando il binario da terminale, senza un bundle .app.
    set(FORGECAD_MACOS_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist")
    target_link_options(forgecad PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${FORGECAD_MACOS_INFO_PLIST}")
    set_property(TARGET forgecad APPEND PROPERTY LINK_DEPENDS "${FORGECAD_MACOS_INFO_PLIST}")
endif()

# Impronta dei sorgenti della geometria per la copia dei corpi nei documenti
# (cad_document_io): se cambia, i corpi salvati si ricalcolano all'apertura.
file(GLOB FORGECAD_GEOMETRY_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/kernel/fk_*.h ${PROJECT_SOURCE_DIR}/kernel/fk_*.cpp
    ${PROJECT_SOURCE_DIR}/cad_*.h ${PROJECT_SOURCE_DIR}/cad_*.cpp)
list(SORT FORGECAD_GEOMETRY_SOURCES)
set(FORGECAD_HASH_LIST ${CMAKE_CURRENT_BINARY_DIR}/generated/geometry_sources.txt)
string(REPLACE ";" "\n" FORGECAD_HASH_LINES "${FORGECAD_GEOMETRY_SOURCES}")
file(GENERATE OUTPUT ${FORGECAD_HASH_LIST} CONTENT "${FORGECAD_HASH_LINES}\n")
set(FORGECAD_HASH_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/forgecad_source_hash.h)
add_custom_command(
    OUTPUT ${FORGECAD_HASH_HEADER} ${CMAKE_CURRENT_BINARY_DIR}/generated/source_hash.stamp
    COMMAND ${CMAKE_COMMAND} -DOUT=${FORGECAD_HASH_HEADER} -DLIST=${FORGECAD_HASH_LIST} -P ${PROJECT_SOURCE_DIR}/cmake/source_hash.cmake
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/generated/source_hash.stamp
    DEPENDS ${FORGECAD_GEOMETRY_SOURCES} ${PROJECT_SOURCE_DIR}/cmake/source_hash.cmake ${FORGECAD_HASH_LIST}
    COMMENT "Impronta dei sorgenti della geometria"
    VERBATIM)
target_sources(forgecad PRIVATE ${FORGECAD_HASH_HEADER})
target_include_directories(forgecad PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(forgecad PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    OpenGL::GL
    forgekernel  # kernel geometrico proprio: tutta la modellazione, STEP e IGES
)

target_compile_options(forgecad PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>
)

if(CMAKE_CUDA_COMPILER)
    set_target_properties(forgecad PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    target_compile_definitions(forgecad PRIVATE FORGECAD_HAS_CUDA=1)
else()
    target_compile_definitions(forgecad PRIVATE FORGECAD_HAS_CUDA=0)
endif()

# Installazione (cmake --install): binario, voce del menu, icone e tipo MIME dei
# documenti .prt. Per l'utente corrente senza installare il binario:
# packaging/linux/install-desktop-integration.sh.
include(GNUInstallDirs)
install(TARGETS forgecad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(FORGECAD_EXEC forgecad)
configure_file(packaging/linux/forgecad.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/forgecad.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/forgecad.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/linux/forgecad-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
foreach(size 16 24 32 48 64 128 256 512)
    install(FILES icons/hicolor/${size}x${size}/apps/forgecad.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
    install(FILES icons/hicolor/${size}x${size}/mimetypes/application-x-forgecad-part.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/mimetypes)
endforeach()
install(FILES icons/forgecad.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES icons/forgecad-document.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/mimetypes
        RENAME application-x-forgecad-part.svg)

# Valutazione delle superfici sulla GPU contro il kernel (solo con nvcc; senza
# GPU il test si salta).
if(CMAKE_CUDA_COMPILER)
    add_executable(forgecad_cuda_tests tests/test_cuda_tessellation.cpp cad_cuda_tessellation.cpp cuda_support.cu)
    set_target_properties(forgecad_cuda_tests PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    target_include_directories(forgecad_cuda_tests PRIVATE ${PROJECT_SOURCE_DIR})
    target_link_libraries(forgecad_cuda_tests PRIVATE forgekernel)
    target_compile_options(forgecad_cuda_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
    add_test(NAME forgecad_cuda_tests COMMAND forgecad_cuda_tests)
endif()

# Test delle interazioni del viewport (Qt offscreen); --gl verifica anche i VBO
# quando e' disponibile una sessione grafica con OpenGL desktop.
option(FORGECAD_BUILD_VIEW_TESTS "Compila i test delle interazioni del viewport" OFF)
if(FORGECAD_BUILD_VIEW_TESTS)
    get_target_property(FORGECAD_VIEW_TEST_SOURCES forgecad SOURCES)
    list(REMOVE_ITEM FORGECAD_VIEW_TEST_SOURCES forgeCad2026.cpp forgeCad2026_gui.cpp ${FORGE_CAD_CUDA_SOURCE} ${FORGECAD_HASH_HEADER})
    list(REMOVE_ITEM FORGECAD_VIEW_TEST_SOURCES cad_macos_menu.h cad_macos_menu.mm)
    add_executable(forgecad_view_tests tests/test_viewport.cpp cuda_support_cpu.cpp ${FORGECAD_VIEW_TEST_SOURCES})
    add_dependencies(forgecad_view_tests forgecad) # genera l'impronta una sola volta, prima dei test
    target_include_directories(forgecad_view_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(forgecad_view_tests PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets OpenGL::GL forgekernel)
    target_compile_options(forgecad_view_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(forgecad_view_tests PRIVATE FORGECAD_HAS_CUDA=0 FORGECAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_executable(forgecad_selection_benchmark tests/benchmark_selection.cpp)
    target_link_libraries(forgecad_selection_benchmark PRIVATE forgekernel)
    # Ambiente offscreen comune; ARGN conserva le opzioni specifiche del test.
    function(forgecad_add_view_test name)
        add_test(NAME ${name} COMMAND forgecad_view_tests ${ARGN})
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")
    endfunction()
    forgecad_add_view_test(forgecad_genealogy_tests --genealogy)
    set_tests_properties(forgecad_genealogy_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_mouse_snapshot_tests --mouse-snapshot)
    set_tests_properties(forgecad_mouse_snapshot_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_sketch_reference_workflow_tests --sketch-reference-workflow)
    set_tests_properties(forgecad_sketch_reference_workflow_tests PROPERTIES TIMEOUT 30)
    forgecad_add_view_test(forgecad_document_access_tests --document-access)
    set_tests_properties(forgecad_document_access_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_sweep_reference_tests --sweep-references)
    set_tests_properties(forgecad_sweep_reference_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_spatial_sketch_tests --spatial-sketch)
    set_tests_properties(forgecad_spatial_sketch_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_curve_extraction_tests --curve-extraction)
    set_tests_properties(forgecad_curve_extraction_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_handle_constraints_tests --handle-constraints)
    set_tests_properties(forgecad_handle_constraints_tests PROPERTIES TIMEOUT 30)
    forgecad_add_view_test(forgecad_dense_fixed_sketch_tests --dense-fixed-sketch)
    set_tests_properties(forgecad_dense_fixed_sketch_tests PROPERTIES TIMEOUT 15)
    forgecad_add_view_test(forgecad_blend_preview_reload_tests --blend-preview-reload)
    forgecad_add_view_test(forgecad_review_regression_tests --review-regressions)
    forgecad_add_view_test(forgecad_copy_faces_tests --copy-faces)
    forgecad_add_view_test(forgecad_reference_list_highlight_tests --reference-list-highlight)
    forgecad_add_view_test(forgecad_surface_pattern_tests --surface-patterns)
    forgecad_add_view_test(forgecad_sheet_dressup_tests --sheet-dressup)
    forgecad_add_view_test(forgecad_midpoint_quadrant_tests --midpoint-quadrant)
    forgecad_add_view_test(forgecad_spline_shape_tests --spline-shape)
    forgecad_add_view_test(forgecad_spline_click_tests --spline-clicks)
    set_tests_properties(forgecad_spline_click_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_edge_loft_tests --edge-loft)
    set_tests_properties(forgecad_edge_loft_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_open_loft_tangency_tests --open-loft-tangency)
    set_tests_properties(forgecad_open_loft_tangency_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_automatic_snap_tests --automatic-snaps)
    forgecad_add_view_test(forgecad_micro_feature_tests --micro-features)
    forgecad_add_view_test(forgecad_extrude_surface_tests --extrude-surface)
    forgecad_add_view_test(forgecad_individual_fill_tests --individual-fill)
    forgecad_add_view_test(forgecad_fill_surface_tests --fill-document)
    forgecad_add_view_test(forgecad_rebuild_all_tests --rebuild-all)
    forgecad_add_view_test(forgecad_ellipse_trim_tests --ellipse-trim)
    forgecad_add_view_test(forgecad_sketch_comb_tests --sketch-comb)
    forgecad_add_view_test(forgecad_shape_analysis_ui_tests --shape-analysis)
    forgecad_add_view_test(forgecad_draft_tests --draft)
    forgecad_add_view_test(forgecad_mesh_export_tests --mesh-export-tests)
    forgecad_add_view_test(forgecad_thicken_tests --thicken)
    forgecad_add_view_test(forgecad_projection_tests --projection-features)
    forgecad_add_view_test(forgecad_workflow_ui_tests --workflow-ui)
    forgecad_add_view_test(forgecad_storyboard_scroll_tests --storyboard-scroll)
    set_tests_properties(forgecad_storyboard_scroll_tests PROPERTIES TIMEOUT 30)
    forgecad_add_view_test(forgecad_history_position_tests --history-position)
    forgecad_add_view_test(forgecad_async_lifetime_tests --async-lifetime)
    forgecad_add_view_test(forgecad_view_tests)
    forgecad_add_view_test(forgecad_orbit_pivot_tests --orbit-pivot)
endif()

add_executable(forgecad_shape_analysis_tests tests/test_shape_analysis.cpp cad_shape_analysis.cpp)
target_include_directories(forgecad_shape_analysis_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_shape_analysis_tests PRIVATE Qt6::Widgets Qt6::OpenGL forgekernel)
add_test(NAME forgecad_shape_analysis_tests COMMAND forgecad_shape_analysis_tests)

add_executable(forgecad_snapshot_chunks_tests tests/test_snapshot_chunks.cpp cad_snapshot_chunks.cpp)
target_include_directories(forgecad_snapshot_chunks_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_snapshot_chunks_tests PRIVATE Qt6::Core)
add_test(NAME forgecad_snapshot_chunks_tests COMMAND forgecad_snapshot_chunks_tests)

# Dipendenze del modello: test autonomo, senza includere la GUI o il kernel.
add_executable(forgecad_feature_dependencies_tests tests/test_feature_dependencies.cpp cad_feature_dependencies.cpp)
target_include_directories(forgecad_feature_dependencies_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_feature_dependencies_tests PRIVATE Qt6::Gui)
add_test(NAME forgecad_feature_dependencies_tests COMMAND forgecad_feature_dependencies_tests)
