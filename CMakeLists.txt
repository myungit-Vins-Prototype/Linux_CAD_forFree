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
# Kernel geometrico B-rep esatto.
find_package(OpenCASCADE REQUIRED)

# Kernel geometrico proprio (in sviluppo), validato contro OpenCASCADE.
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
    cad_history.h
    cad_history.cpp
    cad_kernel.h
    cad_kernel.cpp
    cad_forge.h
    cad_forge.cpp
    cad_kernel_lab.h
    cad_kernel_lab.cpp
    cuda_support.h
    ${FORGE_CAD_CUDA_SOURCE}
)

target_link_libraries(forgecad PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    OpenGL::GL
    TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep
    TKTopAlgo TKPrim TKBO TKBool TKFillet TKMesh TKShHealing
    TKXSBase TKDESTEP TKDEIGES TKXCAF TKLCAF TKCAF TKDE  # esportazione STEP/IGES (cad_export)
    forgekernel  # kernel proprio: alternativa a OCCT (menu Opzioni) e banco di prova del menu Debug
)
target_include_directories(forgecad SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})

target_compile_options(forgecad PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>
)

if(CMAKE_CUDA_COMPILER)
    set_target_properties(forgecad PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    target_compile_definitions(forgecad PRIVATE FORGECAD_HAS_CUDA=1)
else()
    target_compile_definitions(forgecad PRIVATE FORGECAD_HAS_CUDA=0)
endif()
