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
# Senza scelta esplicita, usa native solo quando CMake ha rilevato una GPU.
# Su macchine senza GPU resta il default del compilatore CUDA; non forzare
# -arch=native (nvcc avviserebbe e userebbe comunque il proprio default).
set(FORGECAD_AUTO_CUDA_ARCHITECTURES OFF)
if(CMAKE_CUDA_COMPILER AND NOT DEFINED CMAKE_CUDA_ARCHITECTURES AND "$ENV{CUDAARCHS}" STREQUAL "")
    set(FORGECAD_AUTO_CUDA_ARCHITECTURES ON)
    # OFF lascia scegliere l'architettura al compilatore, anche se il build
    # esistente era stato configurato con native senza una voce nella cache.
    set(CMAKE_CUDA_ARCHITECTURES OFF)
endif()
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    if(FORGECAD_AUTO_CUDA_ARCHITECTURES AND
       CMAKE_CUDA_ARCHITECTURES_NATIVE MATCHES "^[0-9]+(-real)?(;[0-9]+(-real)?)*$")
        set(CMAKE_CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES_NATIVE}")
    endif()
    set(FORGE_CAD_CUDA_SOURCE cuda_support.cu)
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(FORGE_CAD_CUDA_SOURCE cuda_support_metal.mm)
        set_source_files_properties(cuda_support_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    else()
        set(FORGE_CAD_CUDA_SOURCE cuda_support_cpu.cpp)
    endif()
endif()

# Il server della mappa usa le vere opzioni della build, senza eseguire
# l'analisi architetturale durante la compilazione dell'applicazione.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)
# Config package su Homebrew; find_path/find_library anche sulle distribuzioni
# che forniscono libzstd-dev senza un package CMake.
find_package(zstd CONFIG QUIET)
if(TARGET zstd::libzstd_shared)
    set(FORGECAD_ZSTD_TARGET zstd::libzstd_shared)
elseif(TARGET zstd::libzstd_static)
    set(FORGECAD_ZSTD_TARGET zstd::libzstd_static)
else()
    find_path(FORGECAD_ZSTD_INCLUDE_DIR zstd.h REQUIRED)
    find_library(FORGECAD_ZSTD_LIBRARY NAMES zstd REQUIRED)
    add_library(forgecad_zstd UNKNOWN IMPORTED)
    set_target_properties(forgecad_zstd PROPERTIES IMPORTED_LOCATION "${FORGECAD_ZSTD_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FORGECAD_ZSTD_INCLUDE_DIR}")
    set(FORGECAD_ZSTD_TARGET forgecad_zstd)
endif()
find_package(Threads REQUIRED)
# OpenCASCADE serve solo ai test del kernel (riferimento per i confronti):
# l'applicazione usa soltanto il kernel proprio.
find_package(OpenCASCADE REQUIRED)

# Kernel geometrico proprio (kernel/), validato contro OpenCASCADE nei test.
enable_testing()
add_subdirectory(kernel)

# Calcolo GPU sperimentale: stesso filtro conservativo con Metal e CUDA.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(FORGECAD_BOX_SOURCE gpu_box_support.mm)
elseif(CMAKE_CUDA_COMPILER)
    set(FORGECAD_BOX_SOURCE gpu_box_support.cu)
else()
    set(FORGECAD_BOX_SOURCE gpu_box_support_cpu.cpp)
endif()
add_library(forgecad_compute STATIC cad_gpu_broad_phase.cpp cad_gpu_broad_phase.h gpu_box_support.h ${FORGECAD_BOX_SOURCE})
target_include_directories(forgecad_compute PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_compute PUBLIC forgekernel)
target_compile_options(forgecad_compute PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall;-Wextra;-Wpedantic>)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_options(forgecad_compute PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    find_library(FORGECAD_METAL_FRAMEWORK Metal REQUIRED)
    find_library(FORGECAD_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(forgecad_compute PUBLIC ${FORGECAD_METAL_FRAMEWORK} ${FORGECAD_FOUNDATION_FRAMEWORK})
elseif(CMAKE_CUDA_COMPILER)
    set_target_properties(forgecad_compute PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
endif()
add_executable(forgecad_gpu_broad_phase_tests tests/test_gpu_broad_phase.cpp)
target_link_libraries(forgecad_gpu_broad_phase_tests PRIVATE forgecad_compute)
add_test(NAME forgecad_gpu_broad_phase_tests COMMAND forgecad_gpu_broad_phase_tests)
add_test(NAME forgecad_box_fallback_tests COMMAND forgecad_gpu_broad_phase_tests --cpu-fallback)
set_tests_properties(forgecad_box_fallback_tests PROPERTIES TIMEOUT 30)
set_tests_properties(forgecad_gpu_broad_phase_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_executable(forgecad_metal_box_pool_tests tests/test_metal_box_pool.cpp)
    target_link_libraries(forgecad_metal_box_pool_tests PRIVATE forgecad_compute)
    target_compile_options(forgecad_metal_box_pool_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME forgecad_metal_box_pool_tests COMMAND forgecad_metal_box_pool_tests)
    set_tests_properties(forgecad_metal_box_pool_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

add_executable(forgecad
    forgeCad2026.cpp
    forgeCad2026_application_theme.cpp
    forgeCad2026_application_theme.h
    forgeCad2026_menu.cpp
    forgeCad2026_ui_glass.cpp
    forgeCad2026_ui_glass.h
    forgeCad2026_gui.cpp
    forgeCad2026_gui.h
    forgeCad2026_ribbon_preview.cpp
    forgeCad2026_ribbon_preview.h
    forgeCad2026_ribbon_editor.cpp
    forgeCad2026_ribbon_editor.h
    forgeCad2026_viewport.cpp
    forgeCad2026_zoom.cpp
    forgeCad2026_zoom.h
    forgeCad2026_viewport_sketch.cpp
    forgeCad2026_viewport_document.cpp
    forgeCad2026_viewport_selection.cpp
    forgeCad2026_viewport_render.cpp
    forgeCad2026_viewport_preview.cpp
    forgeCad2026_viewport_scene.cpp
    forgeCad2026_viewport_interaction.cpp
    forgeCad2026_viewport_internal.h
    forgeCad2026_feature_preview.cpp
    forgeCad2026_feature_preview.h
    forgeCad2026_sketch_curvature.cpp
    forgeCad2026_sketch_curvature.h
    forgeCad2026_viewport.h
    forgeCad2026_camera.cpp
    forgeCad2026_camera.h
    forgeCad2026_glass_compositor.cpp
    forgeCad2026_glass_compositor.h
    forgeCad2026_gpu_picker.cpp
    forgeCad2026_gpu_picker.h
    forgeCad2026_scene_renderer.cpp
    forgeCad2026_scene_renderer.h
    forgeCad2026_gui_common.cpp
    forgeCad2026_gui_common.h
    forgeCad2026_storyboard.cpp
    forgeCad2026_storyboard.h
    forgeCad2026_dialogs.h
    forgeCad2026_dialogs_common.cpp
    forgeCad2026_dialogs_common.h
    forgeCad2026_dialogs_solids.cpp
    forgeCad2026_dialogs_surfaces.cpp
    forgeCad2026_dialogs_surface_tools.cpp
    forgeCad2026_dialogs_sketch.cpp
    forgeCad2026_dialogs_analysis.cpp
    cad_types.h
    cad_shape_analysis.h
    cad_shape_analysis.cpp
    cad_display_cache.h
    cad_display_cache.cpp
    cad_memory.h
    cad_memory.cpp
    cad_overlay_renderer.h
    cad_overlay_renderer.cpp
    cad_curve_solver.h
    cad_curve_solver.cpp
    cad_curve_extract.h
    cad_spatial_sketch.h
    cad_surface_tools.h
    cad_surface_tools.cpp
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
    cad_document_recovery.h
    cad_document_recovery.cpp
    cad_document_read.h
    cad_document_read.cpp
    cad_drawing.h
    cad_drawing.cpp
    cad_drawing_dimension.h
    cad_drawing_dimension.cpp
    cad_drawing_sheet.h
    cad_drawing_numbering.h
    cad_drawing_sheet.cpp
    cad_drawing_derived.h
    cad_drawing_derived.cpp
    cad_drawing_editor.h
    cad_drawing_editor.cpp
    cad_drawing_preparation.cpp
    cad_drawing_preparation.h
    cad_drawing_silhouette.h
    cad_drawing_silhouette.cpp
    cad_drawing_axes.h
    cad_drawing_annotations.h
    cad_drawing_technical.h
    cad_drawing_centers.h
    cad_drawing_visibility.h
    cad_drawing_visibility.cpp
    cad_drawing_page.h
    cad_drawing_page.cpp
    cad_drawing_file.h
    cad_drawing_file.cpp
    cad_drawing_browser.h
    cad_drawing_browser.cpp
    cad_drawing_toolbars.h
    cad_drawing_revisions.h
    cad_drawing_revisions.cpp
    cad_drawing_revision_ui.cpp
    cad_drawing_restore.cpp
    cad_drawing_release.h
    cad_drawing_release.cpp
    cad_drawing_release_ui.cpp
    cad_revision_client_internal.h
    cad_drawing_window.h
    cad_drawing_window.cpp
    cad_drawing_sheets_ui.cpp
    cad_document_version.h
    cad_revision_store.h
    cad_revision_store.cpp
    cad_revision_recovery_ui.h
    cad_revision_recovery_ui.cpp
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
target_link_libraries(forgecad PRIVATE forgecad_compute)

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
include(cmake/geometry_sources.cmake)
# Il glob applicativo rileva soltanto aggiunte/rimozioni (per esempio una nuova
# implementazione di un header); non inserisce tutti i moduli nell'impronta.
file(GLOB FORGECAD_SOURCE_CATALOG CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/*.h ${PROJECT_SOURCE_DIR}/*.cpp)
file(GLOB FORGECAD_KERNEL_GEOMETRY_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/kernel/fk_*.h ${PROJECT_SOURCE_DIR}/kernel/fk_*.cpp)
forgecad_collect_geometry_sources(FORGECAD_GEOMETRY_SOURCES "${PROJECT_SOURCE_DIR}"
    cad_feature_build.cpp cad_history.cpp cad_document_io.cpp
    cad_gpu_broad_phase.cpp ${FORGECAD_BOX_SOURCE}
    ${FORGE_CAD_CUDA_SOURCE} ${FORGECAD_KERNEL_GEOMETRY_SOURCES})
# Un include aggiunto a un modulo del calcolo aggiorna la chiusura alla build
# successiva, anche quando non e' stato creato alcun file nuovo.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FORGECAD_GEOMETRY_SOURCES})
set(FORGECAD_GEOMETRY_DIRECTORIES)
foreach(source IN LISTS FORGECAD_GEOMETRY_SOURCES)
    get_filename_component(directory "${source}" DIRECTORY)
    list(APPEND FORGECAD_GEOMETRY_DIRECTORIES "${directory}")
endforeach()
list(REMOVE_DUPLICATES FORGECAD_GEOMETRY_DIRECTORIES)
foreach(directory IN LISTS FORGECAD_GEOMETRY_DIRECTORIES)
    file(GLOB local_geometry_catalog CONFIGURE_DEPENDS
        "${directory}/*.h" "${directory}/*.hpp" "${directory}/*.cpp")
endforeach()
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
add_test(NAME forgecad_geometry_sources_tests COMMAND ${CMAKE_COMMAND}
    -DSOURCE_ROOT=${PROJECT_SOURCE_DIR} -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}
    -P ${PROJECT_SOURCE_DIR}/tests/test_geometry_sources.cmake)
set_tests_properties(forgecad_geometry_sources_tests PROPERTIES TIMEOUT 30)
target_sources(forgecad PRIVATE ${FORGECAD_HASH_HEADER})
target_include_directories(forgecad PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(forgecad PRIVATE
    ZLIB::ZLIB
    ${FORGECAD_ZSTD_TARGET}
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

# Runner ribbon separato con preferenze isolate: non viene installato
# e non partecipa alla build predefinita. Preferenze isolate nell'entrypoint.
option(FORGECAD_BUILD_RIBBON_PREVIEW "Abilita l'eseguibile sperimentale della ribbon" OFF)
if(FORGECAD_BUILD_RIBBON_PREVIEW)
    get_target_property(FORGECAD_RIBBON_SOURCES forgecad SOURCES)
    list(REMOVE_ITEM FORGECAD_RIBBON_SOURCES forgeCad2026.cpp)
    add_executable(forgecad_ribbon_preview EXCLUDE_FROM_ALL
        forgeCad2026_ribbon_preview_main.cpp ${FORGECAD_RIBBON_SOURCES})
    get_target_property(FORGECAD_RIBBON_INCLUDES forgecad INCLUDE_DIRECTORIES)
    get_target_property(FORGECAD_RIBBON_LIBRARIES forgecad LINK_LIBRARIES)
    get_target_property(FORGECAD_RIBBON_OPTIONS forgecad COMPILE_OPTIONS)
    get_target_property(FORGECAD_RIBBON_DEFINITIONS forgecad COMPILE_DEFINITIONS)
    target_include_directories(forgecad_ribbon_preview PRIVATE ${FORGECAD_RIBBON_INCLUDES})
    target_link_libraries(forgecad_ribbon_preview PRIVATE ${FORGECAD_RIBBON_LIBRARIES})
    target_compile_options(forgecad_ribbon_preview PRIVATE ${FORGECAD_RIBBON_OPTIONS})
    if(FORGECAD_RIBBON_DEFINITIONS)
        target_compile_definitions(forgecad_ribbon_preview PRIVATE ${FORGECAD_RIBBON_DEFINITIONS})
    endif()
    if(CMAKE_CUDA_COMPILER)
        set_target_properties(forgecad_ribbon_preview PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    endif()
    set_target_properties(forgecad_ribbon_preview PROPERTIES
        OUTPUT_NAME forgecad-ribbon-preview RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ribbon-preview")
    add_test(NAME forgecad_ribbon_preview_tests COMMAND forgecad_ribbon_preview --self-test)
    set_tests_properties(forgecad_ribbon_preview_tests PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")
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
    set_tests_properties(forgecad_cuda_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
endif()

# Budget attivo/inattivo CUDA e richieste/trim concorrenti; stub senza toolkit.
add_executable(forgecad_tessellation_availability_tests tests/test_tessellation_availability.cpp cad_cuda_tessellation.cpp)
target_include_directories(forgecad_tessellation_availability_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_tessellation_availability_tests PRIVATE forgekernel)
add_test(NAME forgecad_tessellation_availability_tests COMMAND forgecad_tessellation_availability_tests)
add_executable(forgecad_cuda_pool_tests tests/test_cuda_pool.cpp ${FORGE_CAD_CUDA_SOURCE})
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Il test di fallback deve continuare a provare lo stub CPU senza Metal.
    set_property(TARGET forgecad_cuda_pool_tests PROPERTY SOURCES tests/test_cuda_pool.cpp cuda_support_cpu.cpp)
    add_executable(forgecad_metal_tessellation_tests tests/test_metal_tessellation.cpp cad_cuda_tessellation.cpp cuda_support_metal.mm)
    target_include_directories(forgecad_metal_tessellation_tests PRIVATE ${PROJECT_SOURCE_DIR})
    target_link_libraries(forgecad_metal_tessellation_tests PRIVATE forgekernel Threads::Threads ${FORGECAD_METAL_FRAMEWORK} ${FORGECAD_FOUNDATION_FRAMEWORK})
    add_test(NAME forgecad_metal_tessellation_tests COMMAND forgecad_metal_tessellation_tests)
    set_tests_properties(forgecad_metal_tessellation_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    set_tests_properties(forgecad_metal_tessellation_tests PROPERTIES ENVIRONMENT "FORGECAD_METAL_MIN_BATCH=2048")
endif()
target_include_directories(forgecad_cuda_pool_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_cuda_pool_tests PRIVATE Threads::Threads)
target_compile_options(forgecad_cuda_pool_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
if(CMAKE_CUDA_COMPILER)
    set_target_properties(forgecad_cuda_pool_tests PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    add_test(NAME forgecad_cuda_pool_tests COMMAND forgecad_cuda_pool_tests)
    set_tests_properties(forgecad_cuda_pool_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
else()
    add_test(NAME forgecad_cuda_pool_tests COMMAND forgecad_cuda_pool_tests --cpu-stub)
    set_tests_properties(forgecad_cuda_pool_tests PROPERTIES TIMEOUT 30)
endif()

# Algebra del solver senza Qt/GPU: riferimento indipendente e componenti connesse.
add_executable(forgecad_constraint_linear_tests tests/test_constraint_linear.cpp)
target_include_directories(forgecad_constraint_linear_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_options(forgecad_constraint_linear_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_constraint_linear_tests COMMAND forgecad_constraint_linear_tests)
set_tests_properties(forgecad_constraint_linear_tests PROPERTIES TIMEOUT 30)

# Durata delle mesh CPU/VBO della cache; la variante --gl e' una prova nativa manuale.
add_executable(forgecad_display_cache_memory_tests tests/test_display_cache_memory.cpp cad_display_cache.cpp)
target_include_directories(forgecad_display_cache_memory_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_display_cache_memory_tests PRIVATE Qt6::Gui Qt6::OpenGL OpenGL::GL)
target_compile_options(forgecad_display_cache_memory_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_display_cache_memory_tests COMMAND forgecad_display_cache_memory_tests)
set_tests_properties(forgecad_display_cache_memory_tests PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")

# Export STL/OBJ B-spline: CPU sempre disponibile, confronto GPU esplicito.
add_executable(forgecad_mesh_export_backend_tests tests/test_mesh_export_backends.cpp
    cad_export.cpp cad_cuda_tessellation.cpp ${FORGE_CAD_CUDA_SOURCE})
target_include_directories(forgecad_mesh_export_backend_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_mesh_export_backend_tests PRIVATE forgekernel Qt6::Core Qt6::Gui)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(forgecad_mesh_export_backend_tests PRIVATE ${FORGECAD_METAL_FRAMEWORK} ${FORGECAD_FOUNDATION_FRAMEWORK})
    add_test(NAME forgecad_mesh_export_metal_tests COMMAND forgecad_mesh_export_backend_tests --require-gpu)
    set_tests_properties(forgecad_mesh_export_metal_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    set_tests_properties(forgecad_mesh_export_metal_tests PROPERTIES ENVIRONMENT "FORGECAD_METAL_MIN_BATCH=2048")
endif()
target_compile_options(forgecad_mesh_export_backend_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME forgecad_mesh_export_cpu_tests COMMAND forgecad_mesh_export_backend_tests)
set_tests_properties(forgecad_mesh_export_cpu_tests PROPERTIES TIMEOUT 120)
if(CMAKE_CUDA_COMPILER)
    set_target_properties(forgecad_mesh_export_backend_tests PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    add_test(NAME forgecad_mesh_export_cuda_tests COMMAND forgecad_mesh_export_backend_tests --require-gpu)
    add_test(NAME forgecad_mesh_export_cuda_specialized_tests COMMAND forgecad_mesh_export_backend_tests --require-gpu)
    set_tests_properties(forgecad_mesh_export_cuda_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
        ENVIRONMENT "FORGECAD_CUDA_GENERIC=1")
    set_tests_properties(forgecad_mesh_export_cuda_specialized_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
        ENVIRONMENT "FORGECAD_CUDA_SPECIALIZED=1;FORGECAD_CUDA_GENERIC=0")
endif()

# Test delle interazioni del viewport (Qt offscreen); --gl verifica anche i VBO
# quando e' disponibile una sessione grafica con OpenGL desktop.
option(FORGECAD_BUILD_VIEW_TESTS "Compila i test delle interazioni del viewport" OFF)
if(FORGECAD_BUILD_VIEW_TESTS)
    set(FORGECAD_EXAMPLE_DIR "${PROJECT_SOURCE_DIR}/File_Esempio" CACHE PATH "Cartella dei documenti di esempio per i test GUI")
    # Il comparatore offline usa NumPy; nessuna nuova dipendenza dell'app.
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import numpy"
            RESULT_VARIABLE FORGECAD_NUMPY_IMPORT_RESULT OUTPUT_QUIET ERROR_QUIET)
        if(FORGECAD_NUMPY_IMPORT_RESULT EQUAL 0)
            add_test(NAME forgecad_numeric_mesh_comparator_tests COMMAND "${Python3_EXECUTABLE}"
                -m unittest discover -s tests -p test_compare_document_mesh_backends.py -v)
            set_tests_properties(forgecad_numeric_mesh_comparator_tests PROPERTIES
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 30 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
        else()
            message(STATUS "Test comparatore mesh non registrato: NumPy non disponibile")
        endif()
    endif()
    get_target_property(FORGECAD_VIEW_TEST_SOURCES forgecad SOURCES)
    # Finestra, viewport, storyboard e dialoghi sono unita' separate anche nei test.
    list(REMOVE_ITEM FORGECAD_VIEW_TEST_SOURCES forgeCad2026.cpp ${FORGE_CAD_CUDA_SOURCE} ${FORGECAD_HASH_HEADER})
    list(REMOVE_ITEM FORGECAD_VIEW_TEST_SOURCES cad_macos_menu.h cad_macos_menu.mm)
    add_executable(forgecad_view_tests tests/test_viewport.cpp tests/test_declared_history_cache.h tests/test_boolean_history.h tests/test_blend_history.h tests/test_extrusion_history.h tests/test_revolution_history.h tests/test_all_history.h tests/test_native_revisions.h tests/test_workspace_revisions.h tests/test_linked_drawing.h tests/test_drawing_workflow.h tests/test_drawing_revisions.h tests/test_projected_drawing_file.h tests/test_drawing_multisheet.h tests/test_drawing_revision_restore.h tests/test_drawing_release.h tests/test_catalog_recovery_gui.h tests/test_surface_tools_gui.h tests/test_document_units.h tests/test_history_suppression.h tests/test_function_panels.h tests/test_drawing_mouse.h tests/test_drawing_section_model.h tests/test_drawing_performance.h cuda_support_cpu.cpp ${FORGECAD_VIEW_TEST_SOURCES})
    add_dependencies(forgecad_view_tests forgecad) # genera l'impronta una sola volta, prima dei test
    target_include_directories(forgecad_view_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(forgecad_view_tests PRIVATE forgecad_compute Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets OpenGL::GL forgekernel ZLIB::ZLIB ${FORGECAD_ZSTD_TARGET})
    target_compile_options(forgecad_view_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(forgecad_view_tests PRIVATE FORGECAD_HAS_CUDA=0 FORGECAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_executable(forgecad_selection_benchmark tests/benchmark_selection.cpp)
    target_link_libraries(forgecad_selection_benchmark PRIVATE forgekernel)
    # Ambiente offscreen senza GLX: evita connessioni X11 anche nei test OpenGL.
    # ARGN conserva le opzioni specifiche del test.
    function(forgecad_add_view_test name)
        add_test(NAME ${name} COMMAND forgecad_view_tests ${ARGN})
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion;FORGECAD_EXAMPLE_DIR=${FORGECAD_EXAMPLE_DIR}")
    endfunction()
    forgecad_add_view_test(forgecad_topology_label_tests --topology-labels)
    forgecad_add_view_test(forgecad_document_read_tests --document-read)
    forgecad_add_view_test(forgecad_document_units_tests --document-units)
    forgecad_add_view_test(forgecad_function_panels_tests --function-panels-review)
    set_tests_properties(forgecad_function_panels_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_history_suppression_tests --history-suppression)
    set_tests_properties(forgecad_document_read_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_pattern_placement_tests --pattern-placements)
    forgecad_add_view_test(forgecad_pattern_result_tests --pattern-results)
    forgecad_add_view_test(forgecad_pattern_legacy_history_tests --pattern-legacy-history)
    forgecad_add_view_test(forgecad_failed_feature_repair_tests --failed-feature-repair)
    forgecad_add_view_test(forgecad_pattern_single_story_tests --pattern-single-story)
    set_tests_properties(forgecad_pattern_single_story_tests PROPERTIES TIMEOUT 60)
    set_tests_properties(forgecad_pattern_legacy_history_tests forgecad_failed_feature_repair_tests PROPERTIES TIMEOUT 60)
    set_tests_properties(forgecad_pattern_result_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_pattern_placement_tests PROPERTIES TIMEOUT 30)
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
    forgecad_add_view_test(forgecad_surface_tools_tests --surface-tools)
    set_tests_properties(forgecad_surface_tools_tests PROPERTIES TIMEOUT 180)
    forgecad_add_view_test(forgecad_catalog_recovery_gui_tests --catalog-recovery)
    set_tests_properties(forgecad_catalog_recovery_gui_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_drawing_release_tests --drawing-release)
    set_tests_properties(forgecad_drawing_release_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_drawing_restore_tests --drawing-restore)
    set_tests_properties(forgecad_drawing_restore_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_drawing_multisheet_tests --drawing-multisheet)
    set_tests_properties(forgecad_drawing_multisheet_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_drawing_revisions_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_workspace_revisions_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_native_revisions_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_all_history_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_preview_performance_tests --preview-performance)
    forgecad_add_view_test(forgecad_history_preview_mesh_tests --history-preview-mesh)
    set_tests_properties(forgecad_history_preview_mesh_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_sketch_slot_point_tests --sketch-slot-point)
    forgecad_add_view_test(forgecad_feature_handle_tests --feature-handles)
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
    forgecad_add_view_test(forgecad_document_consultation_tests --document-consultation)
    set_tests_properties(forgecad_document_consultation_tests PROPERTIES TIMEOUT 90)
    set_tests_properties(forgecad_document_access_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_sweep_reference_tests --sweep-references)
    set_tests_properties(forgecad_sweep_reference_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_spatial_sketch_tests --spatial-sketch)
    set_tests_properties(forgecad_spatial_sketch_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_curve_extraction_tests --curve-extraction)
    set_tests_properties(forgecad_curve_extraction_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_handle_constraints_tests --handle-constraints)
    forgecad_add_view_test(forgecad_solver_partition_tests --solver-partition)
    forgecad_add_view_test(forgecad_connected_solver_tests --connected-solver-benchmark)
    forgecad_add_view_test(forgecad_display_mesh_tests --display-mesh)
    forgecad_add_view_test(forgecad_lazy_mesh_tests --lazy-mesh)
    set_tests_properties(forgecad_lazy_mesh_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_solver_partition_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(forgecad_handle_constraints_tests PROPERTIES TIMEOUT 30)
    forgecad_add_view_test(forgecad_sketch_editing_tests --sketch-editing)
    set_tests_properties(forgecad_sketch_editing_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_helix_start_angle_tests --helix-start-angle)
    set_tests_properties(forgecad_helix_start_angle_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_sketch_menu_cancel_tests --sketch-menu-cancel)
    set_tests_properties(forgecad_sketch_menu_cancel_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_document_recovery_tests --document-recovery)
    set_tests_properties(forgecad_document_recovery_tests PROPERTIES TIMEOUT 90)
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
    forgecad_add_view_test(forgecad_body_owner_history_tests --body-owner-history)
    set_tests_properties(forgecad_body_owner_history_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_datum_story_tests --datum-story)
    set_tests_properties(forgecad_datum_story_tests PROPERTIES TIMEOUT 60)
    forgecad_add_view_test(forgecad_sketch_story_tests --sketch-story)
    set_tests_properties(forgecad_sketch_story_tests PROPERTIES TIMEOUT 120)
    forgecad_add_view_test(forgecad_history_position_tests --history-position)
    forgecad_add_view_test(forgecad_async_lifetime_tests --async-lifetime)
    forgecad_add_view_test(forgecad_view_tests)
    forgecad_add_view_test(forgecad_orbit_pivot_tests --orbit-pivot)
    forgecad_add_view_test(forgecad_smooth_zoom_tests --smooth-zoom)
endif()

add_executable(forgecad_application_theme_tests tests/test_application_theme.cpp forgeCad2026_application_theme.cpp)
target_include_directories(forgecad_application_theme_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(forgecad_application_theme_tests PRIVATE Qt6::Widgets)
target_compile_options(forgecad_application_theme_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_application_theme_tests COMMAND forgecad_application_theme_tests)
set_tests_properties(forgecad_application_theme_tests PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(forgecad_shape_analysis_tests tests/test_shape_analysis.cpp cad_shape_analysis.cpp)
target_include_directories(forgecad_shape_analysis_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_shape_analysis_tests PRIVATE Qt6::Widgets Qt6::OpenGL forgekernel)
add_test(NAME forgecad_shape_analysis_tests COMMAND forgecad_shape_analysis_tests)

add_executable(forgecad_snapshot_chunks_tests tests/test_snapshot_chunks.cpp cad_snapshot_chunks.cpp)
add_executable(forgecad_document_compression_tests tests/test_document_compression.cpp cad_document_read.cpp)
target_include_directories(forgecad_document_compression_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_document_compression_tests PRIVATE Qt6::Gui ZLIB::ZLIB ${FORGECAD_ZSTD_TARGET})
add_test(NAME forgecad_document_compression_tests COMMAND forgecad_document_compression_tests)
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

add_executable(forgecad_revision_release_tests tests/test_revision_releases.cpp cad_revision_store.cpp)
target_include_directories(forgecad_revision_release_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_revision_release_tests PRIVATE Qt6::Core)
target_compile_options(forgecad_revision_release_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_revision_release_tests COMMAND forgecad_revision_release_tests)
set_tests_properties(forgecad_revision_release_tests PROPERTIES TIMEOUT 60)

add_executable(forgecad_catalog_recovery_tests tests/test_catalog_recovery.cpp cad_revision_store.cpp)
target_include_directories(forgecad_catalog_recovery_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_catalog_recovery_tests PRIVATE Qt6::Core)
target_compile_options(forgecad_catalog_recovery_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_catalog_recovery_tests COMMAND forgecad_catalog_recovery_tests)
set_tests_properties(forgecad_catalog_recovery_tests PROPERTIES TIMEOUT 60)

# Dipendenze del modello: test autonomo, senza includere la GUI o il kernel.
add_executable(forgecad_feature_dependencies_tests tests/test_feature_dependencies.cpp cad_feature_dependencies.cpp)
target_include_directories(forgecad_feature_dependencies_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_feature_dependencies_tests PRIVATE Qt6::Gui)
add_test(NAME forgecad_feature_dependencies_tests COMMAND forgecad_feature_dependencies_tests)

add_executable(forgecad_geometry_refs_tests tests/test_geometry_refs.cpp cad_model_history.cpp cad_feature_dependencies.cpp)
target_include_directories(forgecad_geometry_refs_tests PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/kernel)
target_link_libraries(forgecad_geometry_refs_tests PRIVATE Qt6::Gui)
target_compile_options(forgecad_geometry_refs_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_geometry_refs_tests COMMAND forgecad_geometry_refs_tests)
set_tests_properties(forgecad_geometry_refs_tests PROPERTIES TIMEOUT 30)

# Workspace nascosto: risoluzione, creazione differita e conservazione archivio.
add_executable(forgecad_workspace_tests tests/test_workspace.cpp cad_workspace.cpp cad_revision_store.cpp)
target_include_directories(forgecad_workspace_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_workspace_tests PRIVATE Qt6::Core)
target_compile_options(forgecad_workspace_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_workspace_tests COMMAND forgecad_workspace_tests)
set_tests_properties(forgecad_workspace_tests PROPERTIES TIMEOUT 60)

# I runner delle tavole usano gli stessi popup dell'app, senza il viewport 3D.
add_library(forgecad_drawing_menus STATIC forgeCad2026_menu.cpp forgeCad2026_ui_glass.cpp)
target_link_libraries(forgecad_drawing_menus PUBLIC Qt6::Widgets)
target_compile_definitions(forgecad_drawing_menus PRIVATE FORGECAD_MENU_STANDALONE=1)
target_compile_options(forgecad_drawing_menus PRIVATE -Wall -Wextra -Wpedantic)

# Implementazioni identiche compilate una sola volta per i runner 2D.
# App e runner viewport mantengono i propri target e le rispettive definizioni.
add_library(forgecad_drawing_test_support STATIC
    cad_icons.cpp
    forgeCad2026_zoom.cpp
    cad_drawing_editor.cpp
    cad_drawing_preparation.cpp
    cad_drawing_preparation.h
    cad_drawing_sheet.cpp
    cad_drawing_derived.cpp
    cad_drawing.cpp
    cad_drawing_dimension.cpp
    cad_drawing_silhouette.cpp
    cad_drawing_visibility.cpp
    cad_drawing_page.cpp)
target_include_directories(forgecad_drawing_test_support PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_test_support PUBLIC Qt6::Widgets forgekernel forgecad_drawing_menus)
target_compile_options(forgecad_drawing_test_support PRIVATE -Wall -Wextra -Wpedantic)

function(forgecad_add_drawing_editor_test name timeout)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE forgecad_drawing_test_support)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout}
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=;QT_STYLE_OVERRIDE=Fusion")
endfunction()

forgecad_add_drawing_editor_test(forgecad_drawing_preparation_tests 90
    tests/test_drawing_preparation.cpp)

forgecad_add_drawing_editor_test(forgecad_drawing_scale_tests 60
    tests/test_drawing_scale.cpp)

# Primo editor del foglio: prova GUI autonoma senza nuovo contenitore nativo.
forgecad_add_drawing_editor_test(forgecad_drawing_editor_tests 60
    tests/test_drawing_editor.cpp)

forgecad_add_drawing_editor_test(forgecad_projected_drawing_tests 60
    tests/test_projected_drawing.cpp)

forgecad_add_drawing_editor_test(forgecad_drawing_view_edit_tests 60
    tests/test_drawing_view_edit.cpp tests/test_drawing_pan.h tests/test_drawing_view_preview.h)

forgecad_add_drawing_editor_test(forgecad_drawing_layout_history_tests 60
    tests/test_drawing_layout_history.cpp)

# Quote associative complete e interazione degli snap, senza includere la GUI 3D.
add_executable(forgecad_drawing_dimensions_tests tests/test_drawing_dimensions.cpp cad_drawing.cpp cad_drawing_dimension.cpp cad_drawing_sheet.cpp cad_drawing_derived.cpp)
target_include_directories(forgecad_drawing_dimensions_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_dimensions_tests PRIVATE Qt6::Gui forgekernel)
target_compile_options(forgecad_drawing_dimensions_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_drawing_dimensions_tests COMMAND forgecad_drawing_dimensions_tests)
set_tests_properties(forgecad_drawing_dimensions_tests PROPERTIES TIMEOUT 60)

forgecad_add_drawing_editor_test(forgecad_drawing_dimensions_gui_tests 60
    tests/test_drawing_dimensions_gui.cpp tests/test_drawing_dimension_series.h tests/test_drawing_dimension_numbering.h tests/test_drawing_ambiguous_snaps.h tests/test_drawing_segment_spacing.h)

# Inserimento con anteprima e contorni apparenti delle superfici.
forgecad_add_drawing_editor_test(forgecad_drawing_view_placement_tests 120
    tests/test_drawing_view_placement.cpp)

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

forgecad_add_drawing_editor_test(forgecad_drawing_hidden_lines_tests 120
    tests/test_drawing_hidden_lines.cpp)

# Formato foglio, cartiglio e PDF vettoriale senza gli strumenti interattivi.
forgecad_add_drawing_editor_test(forgecad_drawing_page_tests 120
    tests/test_drawing_page.cpp)

forgecad_add_drawing_editor_test(forgecad_drawing_derived_tests 120
    tests/test_drawing_derived.cpp tests/test_drawing_cut_view_selection.h tests/test_drawing_cut_snaps.h)

# Dettagli circolari: stesso renderer delle viste derivate, riferimenti originali.
forgecad_add_drawing_editor_test(forgecad_drawing_detail_tests 120
    tests/test_drawing_detail.cpp cad_drawing_detail.h)

forgecad_add_drawing_editor_test(forgecad_drawing_auxiliary_tests 120
    tests/test_drawing_auxiliary.cpp)

# Interruzione grafica con misura e riferimenti integrali.
forgecad_add_drawing_editor_test(forgecad_drawing_break_tests 120
    tests/test_drawing_break.cpp cad_drawing_break.h)

add_executable(forgecad_revision_numbers_tests tests/test_revision_numbers.cpp cad_revision_store.cpp)
target_include_directories(forgecad_revision_numbers_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_revision_numbers_tests PRIVATE Qt6::Core)
target_compile_options(forgecad_revision_numbers_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_revision_numbers_tests COMMAND forgecad_revision_numbers_tests)
forgecad_add_drawing_editor_test(forgecad_drawing_presentation_tests 120
    tests/test_drawing_presentation.cpp)

forgecad_add_drawing_editor_test(forgecad_drawing_complete_sections_tests 120
    tests/test_drawing_complete_sections.cpp)

forgecad_add_drawing_editor_test(forgecad_drawing_centers_tests 120
    tests/test_drawing_centers.cpp)

forgecad_add_drawing_editor_test(forgecad_drawing_axes_tests 120
    tests/test_drawing_axes.cpp tests/test_drawing_technical_annotations.h tests/test_drawing_options.h cad_drawing_style.h cad_numeric_input.h tests/test_drawing_axis_dimensions.h tests/test_drawing_annotations.h tests/test_drawing_tolerances.h cad_drawing_tolerances.h)

forgecad_add_drawing_editor_test(forgecad_drawing_ui_controls_tests 120
    tests/test_drawing_ui_controls.cpp)

add_executable(forgecad_drawing_focus_tests tests/test_drawing_focus.cpp)
target_include_directories(forgecad_drawing_focus_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_drawing_focus_tests PRIVATE Qt6::Widgets forgekernel)
add_test(NAME forgecad_drawing_focus_tests COMMAND forgecad_drawing_focus_tests)
set_tests_properties(forgecad_drawing_focus_tests PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Richiede un contesto OpenGL: in ambiente headless senza supporto viene saltato.
add_executable(forgecad_overlay_uniform_tests tests/test_overlay_uniform.cpp cad_overlay_renderer.cpp)
target_include_directories(forgecad_overlay_uniform_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(forgecad_overlay_uniform_tests PRIVATE Qt6::Gui Qt6::OpenGL)
target_compile_options(forgecad_overlay_uniform_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME forgecad_overlay_uniform_tests COMMAND forgecad_overlay_uniform_tests)
set_tests_properties(forgecad_overlay_uniform_tests PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "opengl"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;QT_QPA_PLATFORMTHEME=")
