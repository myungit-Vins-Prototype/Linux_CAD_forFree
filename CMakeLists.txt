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
    cad_drawing.h
    cad_drawing.cpp
    cad_drawing_dimension.h
    cad_drawing_dimension.cpp
    cad_drawing_sheet.h
    cad_drawing_sheet.cpp
    cad_drawing_derived.h
    cad_drawing_derived.cpp
    cad_drawing_editor.h
    cad_drawing_editor.cpp
    cad_drawing_silhouette.h
    cad_drawing_silhouette.cpp
    cad_drawing_axes.h
    cad_drawing_annotations.h
    cad_drawing_centers.h
    cad_drawing_visibility.h
    cad_drawing_visibility.cpp
    cad_drawing_page.h
    cad_drawing_page.cpp
    cad_drawing_file.h
    cad_drawing_file.cpp
    cad_drawing_revisions.h
    cad_drawing_revisions.cpp
    cad_drawing_revision_ui.cpp
    cad_revision_client_internal.h
    cad_drawing_window.h
    cad_drawing_window.cpp
    cad_document_version.h
    cad_revision_store.h
    cad_revision_store.cpp
    cad_native_revisions.h
    cad_native_revisions.cpp
    cad_revision_client.h
    cad_revision_client.cpp
    cad_workspace.h
    cad_workspace.cpp
    cad_snapshot_chunks.h
    cad_snapshot_chunks.cpp
    cad_sketch_edit.h
    cad_sketch_edit.cpp
    cad_constraints.h
    cad_constraints.cpp
    cad_toolbar.h
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
    add_executable(forgecad_view_tests tests/test_viewport.cpp tests/test_declared_history_cache.h tests/test_boolean_history.h tests/test_blend_history.h tests/test_extrusion_history.h tests/test_revolution_history.h tests/test_all_history.h tests/test_native_revisions.h tests/test_workspace_revisions.h tests/test_linked_drawing.h tests/test_drawing_revisions.h tests/test_projected_drawing_file.h tests/test_drawing_mouse.h tests/test_drawing_section_model.h cuda_support_cpu.cpp ${FORGECAD_VIEW_TEST_SOURCES})
    add_dependencies(forgecad_view_tests forgecad) # genera l'impronta una sola volta, prima dei test
    target_include_directories(forgecad_view_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(forgecad_view_tests PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets OpenGL::GL forgekernel)
    target_compile_options(forgecad_view_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(forgecad_view_tests PRIVATE FORGECAD_HAS_CUDA=0 FORGECAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_executable(forgecad_selection_benchmark tests/benchmark_selection.cpp)
    target_link_libraries(forgecad_selection_benchmark PRIVATE forgekernel)
    # Ambiente offscreen senza GLX: evita connessioni X11 anche nei test OpenGL.
    # ARGN conserva le opzioni specifiche del test.
    function(forgecad_add_view_test name)
        add_test(NAME ${name} COMMAND forgecad_view_tests ${ARGN})
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")
    endfunction()
    forgecad_add_view_test(forgecad_topology_label_tests --topology-labels)
    forgecad_add_view_test(forgecad_genealogy_tests --genealogy)
    forgecad_add_view_test(forgecad_history_diagnostics_tests --history-diagnostics)
    forgecad_add_view_test(forgecad_declared_history_cache_tests --declared-history-cache)
    forgecad_add_view_test(forgecad_boolean_history_tests --boolean-history)
    forgecad_add_view_test(forgecad_blend_history_tests --blend-history)
    forgecad_add_view_test(forgecad_all_history_tests --all-history)
    forgecad_add_view_test(forgecad_native_revisions_tests --native-revisions)
    forgecad_add_view_test(forgecad_workspace_revisions_tests --workspace-revisions)
    forgecad_add_view_test(forgecad_linked_drawing_tests --linked-drawings)
    forgecad_add_view_test(forgecad_drawing_revisions_tests --drawing-revisions)
    forgecad_add_view_test(forgecad_projected_drawing_file_tests --projected-drawings)
    set_tests_properties(forgecad_drawing_revisions_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_workspace_revisions_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_native_revisions_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_all_history_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_revolution_history_tests --revolution-history)
    set_tests_properties(forgecad_revolution_history_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_extrusion_history_tests --extrusion-history)
    set_tests_properties(forgecad_extrusion_history_tests PROPERTIES TIMEOUT 60)
    set_tests_properties(forgecad_blend_history_tests PROPERTIES TIMEOUT 60)
    set_tests_properties(forgecad_boolean_history_tests PROPERTIES TIMEOUT 60)
    set_tests_properties(forgecad_declared_history_cache_tests PROPERTIES TIMEOUT 60)
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

# Sezioni e quote associative: modulo geometrico autonomo, senza GUI o GL.
add_executable(forgecad_drawing_tests tests/test_drawing.cpp cad_drawing.cpp cad_drawing_dimension.cpp)
target_include_directories(forgecad_drawing_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_tests PRIVATE Qt6::Core forgekernel)
target_compile_options(forgecad_drawing_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_tests COMMAND forgecad_drawing_tests)
set_tests_properties(forgecad_drawing_tests PROPERTIES TIMEOUT 60)

# Archivio delle revisioni: filesystem e processi concorrenti, senza GUI/kernel.
add_executable(forgecad_revision_store_tests tests/test_revision_store.cpp cad_revision_store.cpp)
target_include_directories(forgecad_revision_store_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_revision_store_tests PRIVATE Qt6::Core)
target_compile_options(forgecad_revision_store_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_revision_store_tests COMMAND forgecad_revision_store_tests)
set_tests_properties(forgecad_revision_store_tests PROPERTIES TIMEOUT 60)

# Dipendenze del modello: test autonomo, senza includere la GUI o il kernel.
add_executable(forgecad_feature_dependencies_tests tests/test_feature_dependencies.cpp cad_feature_dependencies.cpp)
target_include_directories(forgecad_feature_dependencies_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_feature_dependencies_tests PRIVATE Qt6::Gui)
add_test(NAME forgecad_feature_dependencies_tests COMMAND forgecad_feature_dependencies_tests)

# Workspace nascosto: risoluzione, creazione differita e conservazione archivio.
add_executable(forgecad_workspace_tests tests/test_workspace.cpp cad_workspace.cpp cad_revision_store.cpp)
target_include_directories(forgecad_workspace_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_workspace_tests PRIVATE Qt6::Core)
target_compile_options(forgecad_workspace_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_workspace_tests COMMAND forgecad_workspace_tests)
set_tests_properties(forgecad_workspace_tests PROPERTIES TIMEOUT 60)

# Primo editor del foglio: prova GUI autonoma senza nuovo contenitore nativo.
add_executable(forgecad_drawing_editor_tests cad_icons.cpp tests/test_drawing_editor.cpp cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_drawing_page.cpp)
target_include_directories(forgecad_drawing_editor_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_editor_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_editor_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_editor_tests COMMAND forgecad_drawing_editor_tests)
set_tests_properties(forgecad_drawing_editor_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_projected_drawing_tests cad_icons.cpp tests/test_projected_drawing.cpp cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_drawing_page.cpp)
target_include_directories(forgecad_projected_drawing_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_projected_drawing_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_projected_drawing_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_projected_drawing_tests COMMAND forgecad_projected_drawing_tests)
set_tests_properties(forgecad_projected_drawing_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_drawing_view_edit_tests tests/test_drawing_view_edit.cpp tests/test_drawing_view_preview.h cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_icons.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_drawing_page.cpp)
target_include_directories(forgecad_drawing_view_edit_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_view_edit_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_view_edit_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_view_edit_tests COMMAND forgecad_drawing_view_edit_tests)
set_tests_properties(forgecad_drawing_view_edit_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_drawing_layout_history_tests tests/test_drawing_layout_history.cpp cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_icons.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_drawing_page.cpp)
target_include_directories(forgecad_drawing_layout_history_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_layout_history_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_layout_history_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_layout_history_tests COMMAND forgecad_drawing_layout_history_tests)
set_tests_properties(forgecad_drawing_layout_history_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

# Quote associative complete e interazione degli snap, senza includere la GUI 3D.
add_executable(forgecad_drawing_dimensions_tests tests/test_drawing_dimensions.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp)
target_include_directories(forgecad_drawing_dimensions_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_dimensions_tests PRIVATE Qt6::Gui forgekernel)
target_compile_options(forgecad_drawing_dimensions_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_dimensions_tests COMMAND forgecad_drawing_dimensions_tests)
set_tests_properties(forgecad_drawing_dimensions_tests PROPERTIES TIMEOUT 60)

add_executable(forgecad_drawing_dimensions_gui_tests tests/test_drawing_dimensions_gui.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing_editor.cpp cad_icons.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_drawing_page.cpp)
target_include_directories(forgecad_drawing_dimensions_gui_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_dimensions_gui_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_dimensions_gui_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_dimensions_gui_tests COMMAND forgecad_drawing_dimensions_gui_tests)
set_tests_properties(forgecad_drawing_dimensions_gui_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

# Inserimento con anteprima e contorni apparenti delle superfici.
add_executable(forgecad_drawing_view_placement_tests tests/test_drawing_view_placement.cpp cad_drawing_editor.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_icons.cpp cad_drawing_page.cpp)
target_include_directories(forgecad_drawing_view_placement_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_view_placement_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_view_placement_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_view_placement_tests COMMAND forgecad_drawing_view_placement_tests)
set_tests_properties(forgecad_drawing_view_placement_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_drawing_silhouette_tests tests/test_drawing_silhouette.cpp cad_drawing_silhouette.cpp)
target_include_directories(forgecad_drawing_silhouette_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_silhouette_tests PRIVATE Qt6::Core forgekernel)
target_compile_options(forgecad_drawing_silhouette_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_silhouette_tests COMMAND forgecad_drawing_silhouette_tests)
set_tests_properties(forgecad_drawing_silhouette_tests PROPERTIES TIMEOUT 120)

# Classificazione grafica delle linee nascoste e comando per singola vista.
add_executable(forgecad_drawing_visibility_tests tests/test_drawing_visibility.cpp cad_drawing_visibility.cpp cad_drawing_silhouette.cpp cad_drawing.cpp cad_drawing_dimension.cpp)
target_include_directories(forgecad_drawing_visibility_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_visibility_tests PRIVATE Qt6::Gui forgekernel)
target_compile_options(forgecad_drawing_visibility_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_visibility_tests COMMAND forgecad_drawing_visibility_tests)
set_tests_properties(forgecad_drawing_visibility_tests PROPERTIES TIMEOUT 120)

add_executable(forgecad_drawing_hidden_lines_tests tests/test_drawing_hidden_lines.cpp cad_drawing_editor.cpp cad_drawing_visibility.cpp cad_drawing_silhouette.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_icons.cpp cad_drawing_page.cpp)
target_include_directories(forgecad_drawing_hidden_lines_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_hidden_lines_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_hidden_lines_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_hidden_lines_tests COMMAND forgecad_drawing_hidden_lines_tests)
set_tests_properties(forgecad_drawing_hidden_lines_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

# Formato foglio, cartiglio e PDF vettoriale senza gli strumenti interattivi.
add_executable(forgecad_drawing_page_tests tests/test_drawing_page.cpp cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing_page.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_icons.cpp)
target_include_directories(forgecad_drawing_page_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_page_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_page_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_page_tests COMMAND forgecad_drawing_page_tests)
set_tests_properties(forgecad_drawing_page_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_drawing_derived_tests tests/test_drawing_derived.cpp tests/test_drawing_cut_snaps.h cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing_page.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_icons.cpp)
target_include_directories(forgecad_drawing_derived_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_derived_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_derived_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_derived_tests COMMAND forgecad_drawing_derived_tests)
set_tests_properties(forgecad_drawing_derived_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_revision_numbers_tests tests/test_revision_numbers.cpp cad_revision_store.cpp)
target_include_directories(forgecad_revision_numbers_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_revision_numbers_tests PRIVATE Qt6::Core)
target_compile_options(forgecad_revision_numbers_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_revision_numbers_tests COMMAND forgecad_revision_numbers_tests)
add_executable(forgecad_drawing_presentation_tests tests/test_drawing_presentation.cpp cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing_page.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_icons.cpp)
target_include_directories(forgecad_drawing_presentation_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_presentation_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_presentation_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_presentation_tests COMMAND forgecad_drawing_presentation_tests)
set_tests_properties(forgecad_drawing_presentation_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_drawing_complete_sections_tests tests/test_drawing_complete_sections.cpp cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing_page.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_icons.cpp)
target_include_directories(forgecad_drawing_complete_sections_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_complete_sections_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_complete_sections_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_complete_sections_tests COMMAND forgecad_drawing_complete_sections_tests)
set_tests_properties(forgecad_drawing_complete_sections_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_drawing_centers_tests tests/test_drawing_centers.cpp cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing_page.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_icons.cpp)
target_include_directories(forgecad_drawing_centers_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_centers_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_centers_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_centers_tests COMMAND forgecad_drawing_centers_tests)
set_tests_properties(forgecad_drawing_centers_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

add_executable(forgecad_drawing_axes_tests tests/test_drawing_axes.cpp tests/test_drawing_axis_dimensions.h tests/test_drawing_annotations.h tests/test_drawing_tolerances.h cad_drawing_tolerances.h cad_drawing_editor.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp cad_drawing_page.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_silhouette.cpp cad_drawing_visibility.cpp cad_icons.cpp)
target_include_directories(forgecad_drawing_axes_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_axes_tests PRIVATE Qt6::Widgets forgekernel)
target_compile_options(forgecad_drawing_axes_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_axes_tests COMMAND forgecad_drawing_axes_tests)
set_tests_properties(forgecad_drawing_axes_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")
