# Test del kernel confrontati con OpenCASCADE (OCCT).
#
# OCCT (LGPL 2.1 con Open CASCADE Exception) e' usato SOLO qui, come
# riferimento indipendente per verificare il kernel proprio: non e' collegato a
# forgekernel ne' all'applicazione e non viene distribuito con ForgeCAD.
# Cartella separata: find_package(OpenCASCADE) aggiunge definizioni di
# compilazione all'intera cartella CMake in cui viene chiamato.
find_package(OpenCASCADE QUIET)
if(NOT OpenCASCADE_FOUND)
    message(WARNING "OpenCASCADE non trovato: forgekernel_tests (confronti con OCCT) non viene compilato. "
                    "L'applicazione e gli altri test non ne hanno bisogno; per escluderli senza avviso: -DFORGECAD_BUILD_OCCT_TESTS=OFF")
    return()
endif()

set(FK_KERNEL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Ponte da OpenCASCADE al kernel proprio, per i confronti con le forme OCCT.
add_library(forgekernel_occt STATIC
    ${FK_KERNEL_DIR}/occt/fk_occt_import.h
    ${FK_KERNEL_DIR}/occt/fk_occt_import.cpp
)
target_include_directories(forgekernel_occt PUBLIC ${FK_KERNEL_DIR}/occt)
target_include_directories(forgekernel_occt SYSTEM PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(forgekernel_occt PUBLIC forgekernel TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep)
target_compile_options(forgekernel_occt PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
set_target_properties(forgekernel_occt PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(forgekernel_tests
    ${FK_KERNEL_DIR}/tests/fk_test.h
    ${FK_KERNEL_DIR}/tests/fk_test_main.cpp
    ${FK_KERNEL_DIR}/tests/fk_test_util.h
    ${FK_KERNEL_DIR}/tests/fk_test_profiles.h
    ${FK_KERNEL_DIR}/tests/test_cancel.cpp
    ${FK_KERNEL_DIR}/tests/test_math.cpp
    ${FK_KERNEL_DIR}/tests/test_predicates.cpp
    ${FK_KERNEL_DIR}/tests/test_bernstein.cpp
    ${FK_KERNEL_DIR}/tests/test_curves.cpp
    ${FK_KERNEL_DIR}/tests/test_bspline.cpp
    ${FK_KERNEL_DIR}/tests/test_curve_algo.cpp
    ${FK_KERNEL_DIR}/tests/test_surfaces.cpp
    ${FK_KERNEL_DIR}/tests/test_bspline_surface.cpp
    ${FK_KERNEL_DIR}/tests/test_surface_algo.cpp
    ${FK_KERNEL_DIR}/tests/test_tolerance.cpp
    ${FK_KERNEL_DIR}/tests/test_topology.cpp
    ${FK_KERNEL_DIR}/tests/test_primitives.cpp
    ${FK_KERNEL_DIR}/tests/test_extrude.cpp
    ${FK_KERNEL_DIR}/tests/test_pcurve.cpp
    ${FK_KERNEL_DIR}/tests/test_mass.cpp
    ${FK_KERNEL_DIR}/tests/test_intersect.cpp
    ${FK_KERNEL_DIR}/tests/test_marching.cpp
    ${FK_KERNEL_DIR}/tests/test_boolean.cpp
    ${FK_KERNEL_DIR}/tests/test_unify.cpp
    ${FK_KERNEL_DIR}/tests/test_tessellate.cpp
    ${FK_KERNEL_DIR}/tests/test_nurbs.cpp
    ${FK_KERNEL_DIR}/tests/test_curve_surface.cpp
    ${FK_KERNEL_DIR}/tests/test_revolve.cpp
    ${FK_KERNEL_DIR}/tests/test_blend.cpp
    ${FK_KERNEL_DIR}/tests/test_blend_contact_breaks.cpp
    ${FK_KERNEL_DIR}/tests/test_sheet.cpp
    ${FK_KERNEL_DIR}/tests/test_transform.cpp
    ${FK_KERNEL_DIR}/tests/test_draft.cpp
    ${FK_KERNEL_DIR}/tests/test_body_io.cpp
    ${FK_KERNEL_DIR}/tests/test_helix.cpp
    ${FK_KERNEL_DIR}/tests/test_sweep.cpp
    ${FK_KERNEL_DIR}/tests/test_loft.cpp
    ${FK_KERNEL_DIR}/tests/test_occt_import.cpp
    ${FK_KERNEL_DIR}/tests/test_blend_features.cpp
    ${FK_KERNEL_DIR}/tests/test_step.cpp
    ${FK_KERNEL_DIR}/tests/test_iges.cpp
    ${FK_KERNEL_DIR}/tests/test_offset.cpp
    ${FK_KERNEL_DIR}/tests/test_planar.cpp
    ${FK_KERNEL_DIR}/tests/test_boundary.cpp
    ${FK_KERNEL_DIR}/tests/test_fill.cpp
    ${FK_KERNEL_DIR}/tests/test_surface_tools.cpp
    ${FK_KERNEL_DIR}/tests/test_project.cpp
    ${FK_KERNEL_DIR}/tests/test_thicken.cpp
    ${FK_KERNEL_DIR}/tests/test_shell.cpp
    ${FK_KERNEL_DIR}/tests/test_loft_surfaces.cpp
    ${FK_KERNEL_DIR}/tests/test_sweep_surfaces.cpp
    ${FK_KERNEL_DIR}/tests/test_history.cpp
)
# OCCT 7.8 ha riunito lo scambio STEP/IGES nei toolkit TKDE*; le versioni
# precedenti (per esempio 7.6 di Ubuntu 24.04 e Debian 12) usano i nomi storici.
if("${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}" VERSION_GREATER_EQUAL 7.8)
    set(FORGEKERNEL_OCCT_EXCHANGE TKDESTEP TKDEIGES TKDE)
else()
    set(FORGEKERNEL_OCCT_EXCHANGE TKSTEP TKIGES TKXDESTEP TKXDEIGES)
endif()
target_link_libraries(forgekernel_tests PRIVATE
    forgekernel
    forgekernel_occt
    TKXSBase ${FORGEKERNEL_OCCT_EXCHANGE} TKXCAF TKLCAF TKCAF
    TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo TKPrim TKBO TKMesh TKShHealing TKFillet TKBool TKOffset
)
target_include_directories(forgekernel_tests SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_compile_options(forgekernel_tests PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
# File di prova facoltativi nella radice del progetto (per esempio STEP di altri CAD).
target_compile_definitions(forgekernel_tests PRIVATE FORGECAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(forgekernel_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Stessa posizione di prima nella cartella di build: <build>/kernel/forgekernel_tests.
set_target_properties(forgekernel_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME forgekernel_tests COMMAND forgekernel_tests)
