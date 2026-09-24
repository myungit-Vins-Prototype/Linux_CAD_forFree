# Kernel geometrico ForgeCAD (in sviluppo): C++17 puro, senza Qt e senza
# OpenCASCADE. OpenCASCADE serve solo ai test, come riferimento.
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
    fk_mass.h
    fk_mass.cpp
    fk_intersect.h
    fk_intersect.cpp
    fk_marching.h
    fk_marching.cpp
    fk_classify.h
    fk_classify.cpp
    fk_boolean.h
    fk_boolean.cpp
    fk_unify.h
    fk_unify.cpp
)
target_include_directories(forgekernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(forgekernel PUBLIC cxx_std_17)
# Il kernel e' codice numerico nel percorso caldo (proiezioni, intersezioni):
# lo si compila ottimizzato anche in Debug, senza perdere i simboli.
target_compile_options(forgekernel PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
set_target_properties(forgekernel PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

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
)
target_link_libraries(forgekernel_tests PRIVATE
    forgekernel
    TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo TKPrim TKBO TKMesh TKShHealing
)
target_include_directories(forgekernel_tests SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_compile_options(forgekernel_tests PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
set_target_properties(forgekernel_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_test(NAME forgekernel_tests COMMAND forgekernel_tests)
