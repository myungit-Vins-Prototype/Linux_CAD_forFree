# K10 — surface/pcurve composition, read-only proposal

Production remains unchanged. AGENTS/CLAUDE and architecture graph consulted: editBodyFacesPoles is the product entry reached by cad_surface_tools.cpp, reconstructs one curve per edge, then validates each fin via pcurveDeviation. Exact-looking isocurve helpers exist in fk_bspline_surface.cpp:203–246, but their computed double poles are not outward enclosures of the original surface and cannot alone justify a continuous bound.

## Reproduction

probe.cpp builds an undeformed degree-(1,1) rectangular NURBS sheet with U knots 0, .123446789, .123456789, .123466789, 1. Replacement raises the middle U pole row by .01. UV boundary is degree-one polynomial with no internal knots. Operation tolerance 1e-6. Compiled against current libforgekernel.a: editSheetPoles ACCEPTS; peak edge/support discrepancy .01 (10000 tolerances), checkBody reports zero issues. Direct fit error .01 while pcurveDeviation(...,64) reports 4.25926e-12. Thus missing support knots cause both gates to miss the same feature.

## Minimum correct scope

Add a composition provider for BSplineSurface + structurally affine isoparametric Curve<2> spans: native Line<2>, polynomial degree-one BSplineCurve<2>, Trimmed wrappers. Equal-coordinate and affine conditions come from original coefficients, not evaluated samples. Rational degree-one unequal weights have nonlinear speed and must initially remain explicitly unsupported. Diagonal lines and nonlinear trims remain unsupported in this first patch, with legacy behavior stated rather than represented as certified.

Proposed result (detail API): SurfaceCurveFitData { CurveFitErrorBound bound; vector<double> breaks; }; surfaceCompositionErrorBound(surface, pcurve, range). Empty bound means unsupported, infinity means unresolved. Supported inputs cannot silently fallback. Output breaks union UV pieces and all preimages of support knots. Breaks aid refinement only; correctness comes from continuous interval composition and coverage, never nominal rounded preimages.

Reuse the position residual implementation in fk_curve_fit_bounds.cpp. Form the isocurve HOMOGENEOUS coefficients by fixed-coordinate outward de Boor evaluation on ORIGINAL surface poles/weights, with placement chains explicitly unwrapped and applied outward. Do not build an ordinary double iso and subsequently label its data exact. Treat variable-coordinate affine map with the existing outward pullback/candidate mapping. Query must cover the outer parameter image including rounding slivers, both endpoint values at UV piece joins, trims and reversals. Periodic wrapping needs either explicit shifted pieces/seam splits or fail-closed when supported inputs leave the natural domain; choose scope in agreement with root. Resource cap and cancellation as existing providers.

## Integration has two necessary gates

1. chosen support: fitCurveCertified(callback, edge.range, data.breaks, tolerance*.25, bound). Unsupported composition retains the explicitly sampled legacy callback.
2. EVERY adjacent support: obtain its own provider and recertify every actual cubic output span against that provider at the existing allowed budget max(operation tol, original edge tolerance, original fin pcurveTolerance). This applies even when the chosen side used fallback. Supported unresolved rejects. Unsupported adjacent side retains sampled validation. Existing source topological identity, UV metadata, alias atomicity and vertex closure behavior unchanged.

Whole-cubic output is guaranteed by both current fit paths. Validate knot multiplicities rather than assuming if this helper is generalized. Recertification uses actual output coefficients; no intermediate-isocurve C0 approximation bridge is needed.

## Wider generalization (separate scope)

A polynomial affine diagonal UV segment through a tensor-product rational patch has rational composition degree p+q. After splitting at U/V knot crossings, tensor blossom/polar-form restriction along the line constructs its homogeneous Bernstein coefficients outward. This is feasible with the same position bound but requires a separate extraction path. General rational UV curves require surface basis composition/denominator clearing and rigorous knot preimage isolation; no existing generic composition helper was identified. Neither increasing sample count nor simply adding nominal support breaks resolves that general contract.

Tests proposed: current narrow-knot repro, isos at interior fixed coordinate, rational weights, reverse/nonunit t, multiple UV segments, nested trim, placed surface; unchanged neighbor with hidden gap to ensure second gate rejects; fallback diagonal controls explicitly identified; cancellation/resource failure; original body unchanged.
