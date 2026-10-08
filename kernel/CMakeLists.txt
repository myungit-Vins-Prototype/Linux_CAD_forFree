# Kernel geometrico ForgeCAD: C++17 puro, senza Qt e senza OpenCASCADE.
# OpenCASCADE serve solo ai test, come riferimento.
add_library(forgekernel STATIC
    fk_precision.h
    fk_math.h
    fk_math.cpp
    fk_predicates.h
    fk_predicates.cpp
    fk_bernstein.h
    fk_bernstein.cpp
    fk_curve.h
    fk_curve.cpp
    fk_curve_ops.h
    fk_curve_ops.cpp
    fk_quadrature.h
    fk_bspline_basis.h
    fk_bspline_basis.cpp
    fk_bspline.h
    fk_bspline.cpp
    fk_surface.h
    fk_surface.cpp
    fk_bspline_surface.h
    fk_bspline_surface.cpp
    fk_curve_algo.h
    fk_curve_algo.cpp
    fk_surface_algo.h
    fk_surface_algo.cpp
    fk_topology.h
    fk_topology.cpp
    fk_body_check.h
    fk_body_check.cpp
    fk_profile.h
    fk_profile.cpp
    fk_extrude.h
    fk_extrude.cpp
    fk_primitives.h
    fk_primitives.cpp
    fk_pcurve.h
    fk_pcurve.cpp
    fk_micro.h
    fk_micro.cpp
    fk_mass.h
    fk_mass.cpp
    fk_intersect.h
    fk_intersect.cpp
    fk_marching.h
    fk_marching.cpp
    fk_classify.h
    fk_classify.cpp
    fk_parallel.h
    fk_body_io.h
    fk_body_io.cpp
    fk_boolean.h
    fk_boolean.cpp
    fk_unify.h
    fk_unify.cpp
    fk_tessellate.h
    fk_tessellate.cpp
    fk_nurbs.h
    fk_nurbs.cpp
    fk_curve_surface.h
    fk_curve_surface.cpp
    fk_revolve.h
    fk_revolve.cpp
    fk_blend.h
    fk_blend.cpp
    fk_blend_loop.h
    fk_blend_loop.cpp
    fk_blend_model.h
    fk_blend_surface.h
    fk_blend_surface.cpp
    fk_sheet.h
    fk_sheet.cpp
    fk_offset.h
    fk_offset.cpp
    fk_sew.h
    fk_sew.cpp
    fk_planar.h
    fk_planar.cpp
    fk_boundary.h
    fk_boundary.cpp
    fk_fill.h
    fk_fill.cpp
    fk_shell.h
    fk_shell.cpp
    fk_transform.h
    fk_transform.cpp
    fk_hermite.h
    fk_hermite.cpp
    fk_helix.h
    fk_helix.cpp
    fk_sweep.h
    fk_sweep.cpp
    fk_loft.h
    fk_loft.cpp
    fk_exchange.h
    fk_exchange.cpp
    fk_step.h
    fk_step.cpp
    fk_iges.h
    fk_iges.cpp
)
target_include_directories(forgekernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Le booleane calcolano le intersezioni tra le facce in parallelo (std::thread).
find_package(Threads REQUIRED)
target_link_libraries(forgekernel PUBLIC Threads::Threads)
target_compile_features(forgekernel PUBLIC cxx_std_17)
# Il kernel e' codice numerico nel percorso caldo (proiezioni, intersezioni):
# lo si compila ottimizzato anche in Debug, senza perdere i simboli.
target_compile_options(forgekernel PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
set_target_properties(forgekernel PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Ponte da OpenCASCADE al kernel proprio: solo per i test (confronti con le
# forme OCCT); a parte, perche' forgekernel resta senza OCCT.
add_library(forgekernel_occt STATIC
    occt/fk_occt_import.h
    occt/fk_occt_import.cpp
)
target_include_directories(forgekernel_occt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/occt)
target_include_directories(forgekernel_occt SYSTEM PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(forgekernel_occt PUBLIC forgekernel TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep)
target_compile_options(forgekernel_occt PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
set_target_properties(forgekernel_occt PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(forgekernel_tests
    tests/fk_test.h
    tests/fk_test_main.cpp
    tests/fk_test_util.h
    tests/fk_test_profiles.h
    tests/test_math.cpp
    tests/test_predicates.cpp
    tests/test_bernstein.cpp
    tests/test_curves.cpp
    tests/test_bspline.cpp
    tests/test_curve_algo.cpp
    tests/test_surfaces.cpp
    tests/test_bspline_surface.cpp
    tests/test_surface_algo.cpp
    tests/test_topology.cpp
    tests/test_primitives.cpp
    tests/test_extrude.cpp
    tests/test_pcurve.cpp
    tests/test_mass.cpp
    tests/test_intersect.cpp
    tests/test_marching.cpp
    tests/test_boolean.cpp
    tests/test_unify.cpp
    tests/test_tessellate.cpp
    tests/test_nurbs.cpp
    tests/test_curve_surface.cpp
    tests/test_revolve.cpp
    tests/test_blend.cpp
    tests/test_sheet.cpp
    tests/test_transform.cpp
    tests/test_body_io.cpp
    tests/test_helix.cpp
    tests/test_sweep.cpp
    tests/test_loft.cpp
    tests/test_occt_import.cpp
    tests/test_blend_features.cpp
    tests/test_step.cpp
    tests/test_iges.cpp
    tests/test_offset.cpp
    tests/test_planar.cpp
    tests/test_boundary.cpp
    tests/test_fill.cpp
    tests/test_shell.cpp
    tests/test_loft_surfaces.cpp
    tests/test_sweep_surfaces.cpp
)
target_link_libraries(forgekernel_tests PRIVATE
    forgekernel
    forgekernel_occt
    TKXSBase TKDESTEP TKDEIGES TKXCAF TKLCAF TKCAF TKDE
    TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo TKPrim TKBO TKMesh TKShHealing TKFillet TKBool TKOffset
)
target_include_directories(forgekernel_tests SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_compile_options(forgekernel_tests PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
# File di prova facoltativi nella radice del progetto (per esempio STEP di altri CAD).
target_compile_definitions(forgekernel_tests PRIVATE FORGECAD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(forgekernel_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME forgekernel_tests COMMAND forgekernel_tests)

# Diagnostica del raccordo non locale: non e' collegata all'applicazione.
# Il self-test controlla casi geometrici analitici, senza dipendere da OCCT.
add_executable(forgekernel_blend_probe
    tests/probe_blend_candidates.cpp
    tests/probe_trimmed_sphere.h
)
target_link_libraries(forgekernel_blend_probe PRIVATE forgekernel)
target_compile_options(forgekernel_blend_probe PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
set_target_properties(forgekernel_blend_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_blend_probe_self_test COMMAND forgekernel_blend_probe --self-test)
