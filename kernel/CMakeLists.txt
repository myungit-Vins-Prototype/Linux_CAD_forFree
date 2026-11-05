# Kernel geometrico ForgeCAD: C++17 puro, senza Qt e senza OpenCASCADE.
# OpenCASCADE serve solo ai test di confronto (vedi occt_tests/).
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <charconv>
    int main() {
        double value = 0;
        const char text[] = \"1.5\";
        return int(std::from_chars(text, text + 3, value, std::chars_format::general).ec);
    }
" FORGEKERNEL_HAS_FLOAT_FROM_CHARS)
set_source_files_properties(fk_exchange.cpp PROPERTIES COMPILE_DEFINITIONS
    FORGEKERNEL_HAS_FLOAT_FROM_CHARS=$<BOOL:${FORGEKERNEL_HAS_FLOAT_FROM_CHARS}>)

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
    fk_curve_length_cache.h
    fk_curve_algo.cpp
    fk_surface_algo.h
    fk_surface_algo.cpp
    fk_face_normal_bounds.h
    fk_face_normal_bounds.cpp
    fk_sketch_projection.h
    fk_sketch_projection.cpp
    fk_surface_distance.h
    fk_surface_distance.cpp
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
    fk_bspline_isoline.h
    fk_bspline_isoline.cpp
    fk_intersect.h
    fk_intersect.cpp
    fk_marching.h
    fk_marching.cpp
    fk_classify.h
    fk_classify.cpp
    fk_cancel.h
    fk_parallel.h
    fk_body_io.h
    fk_body_io.cpp
    fk_boolean.h
    fk_boolean.cpp
    fk_unify.h
    fk_unify.cpp
    fk_history.h
    fk_history.cpp
    fk_tessellate.h
    fk_tessellate.cpp
    fk_nurbs.h
    fk_nurbs.cpp
    fk_curve_surface.h
    fk_curve_surface.cpp
    fk_revolve.h
    fk_revolve.cpp
    fk_blend_paths.h
    fk_blend_paths.cpp
    fk_blend.h
    fk_blend.cpp
    fk_blend_loop.h
    fk_blend_loop.cpp
    fk_blend_model.h
    fk_blend_surface.h
    fk_blend_surface.cpp
    fk_blend_contact_breaks.h
    fk_blend_contact_breaks.cpp
    fk_sheet.h
    fk_surface_tools.h
    fk_surface_tools.cpp
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
    fk_project.h
    fk_project.cpp
    fk_thicken.h
    fk_thicken.cpp
    fk_shell.h
    fk_shell.cpp
    fk_transform.h
    fk_transform.cpp
    fk_draft.h
    fk_draft.cpp
    fk_hermite.h
    fk_hermite.cpp
    fk_helix.h
    fk_helix.cpp
    fk_sweep.h
    fk_sweep.cpp
    fk_loft.h
    fk_loft_merge.cpp
    fk_loft.cpp
    fk_loft_guide_constraints.h
    fk_loft_guide_constraints.cpp
    fk_loft_profiles.h
    fk_loft_profiles.cpp
    fk_loft_sections.cpp
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
# Le ricorrenze specializzate e generiche devono arrotondare gli stessi
# passaggi: la contrazione implicita a FMA cambia i prefissi delle derivate
# e i segni dei residui geometrici su x86-64-v3. Manteniamo SIMD/O2 e le
# std::fma esplicite dei predicati esatti. Vale anche per le funzioni inline
# del kernel compilate nei chiamanti C++/Objective-C++ (non per CUDA).
target_compile_options(forgekernel PUBLIC
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-ffp-contract=off>"
    "$<$<COMPILE_LANG_AND_ID:OBJCXX,Clang,AppleClang>:-ffp-contract=off>"
)
set_target_properties(forgekernel PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Test di confronto con OpenCASCADE (OCCT): OCCT e' solo un riferimento per i
# test e non entra mai in forgekernel ne' nell'applicazione. Sta in una cartella
# CMake separata perche' la configurazione di OCCT aggiunge definizioni a livello
# di cartella (HAVE_TK, OCC_CONVERT_SIGNALS, ...) che cosi' non raggiungono gli
# altri target. Si escludono con -DFORGECAD_BUILD_OCCT_TESTS=OFF.
if(FORGECAD_BUILD_OCCT_TESTS)
    add_subdirectory(occt_tests)
endif()

# Istanze esatte NURBS, specchi e persistenza senza duplicare i poli.
add_executable(forgekernel_surface_instances_tests tests/fk_test_main.cpp tests/test_surface_instances.cpp)
target_link_libraries(forgekernel_surface_instances_tests PRIVATE forgekernel)
target_compile_options(forgekernel_surface_instances_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_surface_instances_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_surface_instances_tests COMMAND forgekernel_surface_instances_tests)

# Robustezza dei formati propri, indipendente dalla presenza di OCCT.
add_executable(forgekernel_exchange_robustness_tests
    tests/fk_test_main.cpp tests/test_exchange_robustness.cpp)
target_link_libraries(forgekernel_exchange_robustness_tests PRIVATE forgekernel)
target_compile_options(forgekernel_exchange_robustness_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_exchange_robustness_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_exchange_robustness_tests COMMAND forgekernel_exchange_robustness_tests)
set_tests_properties(forgekernel_exchange_robustness_tests PROPERTIES TIMEOUT 30)

# Genealogia delle operazioni: solo il kernel, senza OCCT (eseguibile anche
# dove le librerie di riferimento non ci sono).
add_executable(forgekernel_history_tests
    tests/fk_test.h
    tests/fk_test_main.cpp
    tests/test_history.cpp
)
target_link_libraries(forgekernel_history_tests PRIVATE forgekernel)
target_compile_options(forgekernel_history_tests PRIVATE -Wall -Wextra -Wpedantic -O2 -g)
set_target_properties(forgekernel_history_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_history_tests COMMAND forgekernel_history_tests)

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

# Politica delle tolleranze: collegamento al solo kernel proprio.
add_executable(forgekernel_tolerance_tests
    tests/fk_test_main.cpp
    tests/test_tolerance.cpp
)
target_link_libraries(forgekernel_tolerance_tests PRIVATE forgekernel)
target_compile_options(forgekernel_tolerance_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_tolerance_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_tolerance_tests COMMAND forgekernel_tolerance_tests)

# Geometria delle nuove funzioni, eseguibile anche senza il riferimento OCCT.
add_executable(forgekernel_surface_tools_tests tests/fk_test_main.cpp tests/test_surface_tools.cpp tests/test_fill.cpp)
target_link_libraries(forgekernel_surface_tools_tests PRIVATE forgekernel)
target_compile_options(forgekernel_surface_tools_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_surface_tools_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_surface_tools_tests COMMAND forgekernel_surface_tools_tests)
set_tests_properties(forgekernel_surface_tools_tests PROPERTIES TIMEOUT 180)

# Raccordi conici e variabili: geometria e stazioni in lunghezza d'arco.
add_executable(forgekernel_blend_profile_tests tests/fk_test_main.cpp tests/test_blend_profiles.cpp)
target_link_libraries(forgekernel_blend_profile_tests PRIVATE forgekernel)
target_compile_options(forgekernel_blend_profile_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_blend_profile_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_blend_profile_tests COMMAND forgekernel_blend_profile_tests)
set_tests_properties(forgekernel_blend_profile_tests PROPERTIES TIMEOUT 60)

# Orientamento delle facce rifilate e curva da doppia proiezione: solo kernel proprio.
add_executable(forgekernel_face_normal_bounds_tests tests/fk_test_main.cpp tests/test_face_normal_bounds.cpp)
target_link_libraries(forgekernel_face_normal_bounds_tests PRIVATE forgekernel)
target_compile_options(forgekernel_face_normal_bounds_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_face_normal_bounds_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_face_normal_bounds_tests COMMAND forgekernel_face_normal_bounds_tests)
set_tests_properties(forgekernel_face_normal_bounds_tests PROPERTIES TIMEOUT 30)

add_executable(forgekernel_sketch_projection_tests tests/test_sketch_projection.cpp)
target_link_libraries(forgekernel_sketch_projection_tests PRIVATE forgekernel)
target_compile_options(forgekernel_sketch_projection_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_sketch_projection_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_sketch_projection_tests COMMAND forgekernel_sketch_projection_tests)
set_tests_properties(forgekernel_sketch_projection_tests PROPERTIES TIMEOUT 60)

# Contatti B-rep e riferimenti esatti alle curve proiettate: solo kernel proprio.
add_executable(forgekernel_projected_curve_reference_tests tests/test_projected_curve_references.cpp)
target_link_libraries(forgekernel_projected_curve_reference_tests PRIVATE forgekernel)
target_compile_options(forgekernel_projected_curve_reference_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_projected_curve_reference_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_projected_curve_reference_tests COMMAND forgekernel_projected_curve_reference_tests)
set_tests_properties(forgekernel_projected_curve_reference_tests PROPERTIES TIMEOUT 60)

add_executable(forgekernel_surface_parameterization_tests tests/fk_test_main.cpp tests/test_surface_parameterization.cpp)
target_link_libraries(forgekernel_surface_parameterization_tests PRIVATE forgekernel)
target_compile_options(forgekernel_surface_parameterization_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_surface_parameterization_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_surface_parameterization_tests COMMAND forgekernel_surface_parameterization_tests)
set_tests_properties(forgekernel_surface_parameterization_tests PROPERTIES TIMEOUT 60)

add_executable(forgekernel_disjoint_union_tests tests/fk_test_main.cpp tests/test_disjoint_union.cpp)
target_link_libraries(forgekernel_disjoint_union_tests PRIVATE forgekernel)
target_compile_options(forgekernel_disjoint_union_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_disjoint_union_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_disjoint_union_tests COMMAND forgekernel_disjoint_union_tests)
set_tests_properties(forgekernel_disjoint_union_tests PROPERTIES TIMEOUT 60)


# Loft con guide implicite esatte e sezioni piane da riferimenti spaziali.
# Indipendente da OpenCASCADE e dai documenti esterni dell'utente.
add_executable(forgekernel_loft_projected_guide_tests tests/test_loft_projected_guides.cpp)
target_link_libraries(forgekernel_loft_projected_guide_tests PRIVATE forgekernel)
target_compile_options(forgekernel_loft_projected_guide_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_loft_projected_guide_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_loft_projected_guide_tests COMMAND forgekernel_loft_projected_guide_tests)
set_tests_properties(forgekernel_loft_projected_guide_tests PROPERTIES TIMEOUT 120)

# Continuita' interna e invarianza della forma loft: solo kernel proprio.
add_executable(forgekernel_loft_shape_tests tests/test_loft_shape.cpp)
target_link_libraries(forgekernel_loft_shape_tests PRIVATE forgekernel)
target_compile_options(forgekernel_loft_shape_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_loft_shape_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_loft_shape_tests COMMAND forgekernel_loft_shape_tests)
set_tests_properties(forgekernel_loft_shape_tests PROPERTIES TIMEOUT 120)

# Sezioni proiettate/estratte implicite: costruzione NURBS entro budget, sorgenti immutate.
add_executable(forgekernel_loft_projected_section_tests tests/test_loft_projected_sections.cpp)
target_link_libraries(forgekernel_loft_projected_section_tests PRIVATE forgekernel)
target_compile_options(forgekernel_loft_projected_section_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_loft_projected_section_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_loft_projected_section_tests COMMAND forgekernel_loft_projected_section_tests)
set_tests_properties(forgekernel_loft_projected_section_tests PROPERTIES TIMEOUT 120)

# Condizioni trasversali alle guide: direzioni, G1, forma e sezioni esatte.
add_executable(forgekernel_loft_complexity_tests tests/fk_test_main.cpp tests/test_loft_complexity.cpp)
target_link_libraries(forgekernel_loft_complexity_tests PRIVATE forgekernel)
target_compile_options(forgekernel_loft_complexity_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_loft_complexity_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_loft_complexity_tests COMMAND forgekernel_loft_complexity_tests)
set_tests_properties(forgekernel_loft_complexity_tests PROPERTIES TIMEOUT 120)

add_executable(forgekernel_loft_guide_constraint_tests tests/test_loft_guide_constraints.cpp)
target_link_libraries(forgekernel_loft_guide_constraint_tests PRIVATE forgekernel)
target_compile_options(forgekernel_loft_guide_constraint_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_loft_guide_constraint_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_loft_guide_constraint_tests COMMAND forgekernel_loft_guide_constraint_tests)
set_tests_properties(forgekernel_loft_guide_constraint_tests PROPERTIES TIMEOUT 120)

add_executable(forgekernel_offset_split_crease_tests tests/test_offset_split_creases.cpp)
target_link_libraries(forgekernel_offset_split_crease_tests PRIVATE forgekernel)
target_compile_definitions(forgekernel_offset_split_crease_tests PRIVATE FORGECAD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(forgekernel_offset_split_crease_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_offset_split_crease_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_offset_split_crease_tests COMMAND forgekernel_offset_split_crease_tests)
set_tests_properties(forgekernel_offset_split_crease_tests PROPERTIES TIMEOUT 60)

add_executable(forgekernel_blend_contact_break_tests tests/fk_test_main.cpp tests/test_blend_contact_breaks.cpp)
target_link_libraries(forgekernel_blend_contact_break_tests PRIVATE forgekernel)
target_compile_definitions(forgekernel_blend_contact_break_tests PRIVATE FORGECAD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(forgekernel_blend_contact_break_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_blend_contact_break_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_blend_contact_break_tests COMMAND forgekernel_blend_contact_break_tests)
set_tests_properties(forgekernel_blend_contact_break_tests PROPERTIES TIMEOUT 60)

add_executable(forgekernel_plane_jets_tests tests/test_bspline_plane_jets.cpp)
target_link_libraries(forgekernel_plane_jets_tests PRIVATE forgekernel)
target_compile_options(forgekernel_plane_jets_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_plane_jets_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_plane_jets_tests COMMAND forgekernel_plane_jets_tests)
set_tests_properties(forgekernel_plane_jets_tests PROPERTIES TIMEOUT 30)

# Jet a u fisso: NURBS razionali, istanze, nodi stretti e quadrature di massa.
add_executable(forgekernel_bspline_isoline_tests tests/fk_test_main.cpp tests/test_bspline_isoline.cpp)
target_link_libraries(forgekernel_bspline_isoline_tests PRIVATE forgekernel)
target_compile_options(forgekernel_bspline_isoline_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_bspline_isoline_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_bspline_isoline_tests COMMAND forgekernel_bspline_isoline_tests)
set_tests_properties(forgekernel_bspline_isoline_tests PROPERTIES TIMEOUT 60)

add_executable(forgekernel_loft_correspondence_tests tests/fk_test_main.cpp tests/test_loft_correspondence.cpp)
target_link_libraries(forgekernel_loft_correspondence_tests PRIVATE forgekernel)
target_compile_options(forgekernel_loft_correspondence_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_loft_correspondence_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_loft_correspondence_tests COMMAND forgekernel_loft_correspondence_tests)
set_tests_properties(forgekernel_loft_correspondence_tests PROPERTIES TIMEOUT 120)

add_executable(forgekernel_loft_point_tests tests/test_loft_points.cpp)
target_link_libraries(forgekernel_loft_point_tests PRIVATE forgekernel)
target_compile_options(forgekernel_loft_point_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_loft_point_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_loft_point_tests COMMAND forgekernel_loft_point_tests)
set_tests_properties(forgekernel_loft_point_tests PROPERTIES TIMEOUT 120)

# Cuciture UV dei loft chiusi durante booleane e unificazione delle facce.
add_executable(forgekernel_boolean_seam_tests tests/test_boolean_seams.cpp)
target_link_libraries(forgekernel_boolean_seam_tests PRIVATE forgekernel)
target_compile_options(forgekernel_boolean_seam_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_boolean_seam_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_boolean_seam_tests COMMAND forgekernel_boolean_seam_tests)
set_tests_properties(forgekernel_boolean_seam_tests PROPERTIES TIMEOUT 120)

# Griglie NURBS fitte: le coppie di pezze disgiunte non consumano la ricerca.
add_executable(forgekernel_marching_seed_grid_tests tests/test_marching_seed_grid.cpp)
target_link_libraries(forgekernel_marching_seed_grid_tests PRIVATE forgekernel)
target_compile_options(forgekernel_marching_seed_grid_tests PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(forgekernel_marching_seed_grid_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME forgekernel_marching_seed_grid_tests COMMAND forgekernel_marching_seed_grid_tests)
set_tests_properties(forgekernel_marching_seed_grid_tests PROPERTIES TIMEOUT 60)
