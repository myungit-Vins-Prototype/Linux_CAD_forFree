cmake_minimum_required(VERSION 3.21)
project(ForgeCAD LANGUAGES CXX)

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
    cad_curve_solver.h
    cad_curve_solver.cpp
    cad_snap.h
    cad_snap.cpp
    cad_export.h
    cad_export.cpp
    cad_document_io.h
    cad_document_io.cpp
    cad_sketch_edit.h
    cad_sketch_edit.cpp
    cad_constraints.h
    cad_constraints.cpp
    cad_icons.h
    cad_icons.cpp
    cad_history.h
    cad_history.cpp
    cad_kernel.h
    cad_kernel.cpp
    cad_forge.h
    cad_forge.cpp
    cad_features.h
    cad_features.cpp
    cad_mass.h
    cad_mass.cpp
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
    cuda_support.h
    ${FORGE_CAD_CUDA_SOURCE}
)

# Impronta dei sorgenti della geometria per la copia dei corpi nei documenti
# (cad_document_io): se cambia, i corpi salvati si ricalcolano all'apertura.
file(GLOB FORGECAD_GEOMETRY_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/kernel/fk_*.h ${PROJECT_SOURCE_DIR}/kernel/fk_*.cpp
    ${PROJECT_SOURCE_DIR}/cad_*.h ${PROJECT_SOURCE_DIR}/cad_*.cpp
    ${PROJECT_SOURCE_DIR}/forgeCad2026_gui.cpp)
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
