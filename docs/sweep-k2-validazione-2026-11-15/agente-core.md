# K2 surface certificate — staged core

The production candidate is `fk_sweep_surface_validation.{h,cpp}` in this directory; the repository has not been modified by this agent.

## Contract

`validateSweepSurfaces` works on final rectangular rational BSpline carriers, their explicit profile/path atlas coordinates and genealogy (family and periods). It returns Certified or Unresolved. Unresolved is never interpreted as nonintersection. Trim/cap membership and material nesting are separate caller checks. `poleError` and `weightError` are absolute outward coefficient bounds for conversions whose error must be established by the caller.

It excludes intersections beyond the declared physical tolerance. At genealogical joins, unequal carrier boundaries receive a rational Bernstein bound on their geometric jump; a strong monotonicity estimate converts this jump into a physical collar bound, which must fit the tolerance. This is not a claim of IEEE equality/injectivity of discontinuous fitted carriers.

## Certificate

* Actual B-spline span extraction and de Casteljau use outward arithmetic on homogeneous coefficients. Rational positive weights are required; unsupported/invalid data cannot certify.
* A spatial BVH excludes pairs by outward convex hull separation. No finite sampling can certify exclusion.
* Remaining pairs use an atlas rectangle including every intervening chart. A fixed projection from 3D to 2D and a positive definite symmetric projected Jacobian certify local/global monotonicity on that rectangle. Coordinates are scaled by positive constants for conditioning. Projection choices and subdivision choices are heuristics only; the final certificate remains interval arithmetic.
* Derivative numerators `H'W-HW'` are cached as Bernstein fields. Their coefficients are projected before forming interval hulls, preserving cancellations. Subdivision propagates these polynomial numerator fields and the exact parameter derivative factor; denominators are bounded from child weights. Weight independence is explicit provenance from equal original IEEE input weights, never inferred from equal-width interval bounds.
* Atlas coverage is checked. Internal discontinuous knot multiplicities are rejected. Periodic fundamental boundaries are checked explicitly even when a full-domain monotonicity certificate might otherwise avoid visiting the seam.
* Source V intervals are indexed conservatively. Candidate collection precedes costly trimming when the chart-cell budget is exceeded.
* `SweepSurfaceQuery` owns an extraction prepared once, reused by cap-side projection/contact queries. Contact requires the genealogical boundary coordinate and a signed-height monotonicity bound with the full physical derivative norm.

## Resource semantics

Visits bound recursive validation and actual periodic seam candidates; cell, coefficient, depth and local-chart limits are explicit. These are not a hard process-memory or wall-time limit: input inspection, extraction, index/BVH preparation and broadphase ordering also consume work. Cancellation checkpoints are present throughout preparation and traversal.

## Validation

* Public core tests: 14 checks pass, including a rectangle, joins, a 90-degree folded sheet, rational periodic cylinder, missing periodic closure, duplicate atlas domain, scale underflow, internal-discontinuity constructor rejection, a single-carrier fold, bounded coefficient perturbation, query/wrapper equivalence, cap contact and budget zero.
* Cached numerator propagation: 58,050 sampled derivative-component inclusions pass across polynomial/U-rational/general-rational nets, three scales, asymmetric splits, both child sides and four recursive levels. Sampling is a regression check of the algebra, not an acceptance certificate.
* Independent seam-agent audit: 48 nets and 3,168 sampled derivatives pass; no false certificate found.
* The optional bit-adjacent experiment was not selected: portable `std::nextafter` remains the production implementation. Its 6,000,020 comparisons were bit-identical, but only ~13% faster on the reduced gate.

Integration, analytic conversion error bounds, cap trims/nesting, full regression/build and final user-facing classification belong to the parent integration.

## Final subdivision choice and integration gates

The final candidate keeps portable `std::nextafter`. It normally bisects the physically longer patch direction. On rectangles with derivative-norm ratio outside [1/4,4], it alternates the axes: this prevents spending all early cuts on the long profile direction while leaving path-direction Jacobian variation unchanged. This changes only the search order; every separation/monotonicity certificate is unchanged.

Final hybrid side-only gates:

* Helix 15/16/17 turns: PASS, 63.85 seconds total; visits 168090/161382/163865 (including periodic seam charging), cells 22536/22536/19874.
* K4 complete 18 valid scale/pose/representation combinations plus genuine fold rejection: PASS, approximately 17 seconds (pure alternation 16.20 seconds; final hybrid separately passed).
* Closed annular torus, analytic/NURBS and whole/split paths: PASS, 37.56 seconds total. Independent full parent integration also passed all four variants; NURBS split now passes in 18.75 seconds instead of exhausting 500000 visits.

The public 14-check harness is rerun against the final clean source. Debug/instrumented and faster-bit-adjacent experiments are intentionally separate directories and are not the selected source.
