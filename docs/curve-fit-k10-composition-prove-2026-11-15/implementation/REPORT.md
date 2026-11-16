# K10 surface-poles composition — staged delivery

Three owned production files integrated after parent approval; repository bytes match SHA256SUMS. No pending source edits. Ownership: fk_curve_fit_bounds.cpp/.h and fk_surface_poles.cpp. Original snapshots, patch and manifest are local in this directory. Parent coordinates shared build, documentation, architecture and CMake.

## Cause and correction

editBodyFacesPoles reconstructed support(pcurve(t)) using only UV breakpoints, then accepted every adjacent support via pcurveDeviation with 64 samples per fitted span. Surface knot events were absent from BOTH gates. probe.log demonstrates an accepted degree-(1,1) narrow NURBS feature with .01 error at tolerance1e-6, checkBody0. The separate direct-fit measurement shows .01 error and only4.25926e-12 sampled residual.

The provider surfaceCompositionErrorBound recognizes structurally affine isoparametric UV Line2 (unit axis direction), polynomial/equal-weight degree-one BSpline2 spans, and Trimmed wrappers. It retains original origins/directions or poles/knots. It prepares isocurve homogeneous coefficients directly with outward fixed-axis de Boor evaluation and variable-axis blossom extraction, including original instanced basis/placement chains. It never takes a rounded ordinary uIsoCurve/vIsoCurve as an exact source. Only active fixed-direction support rows enter weight scaling. Rational positive surface weights are supported.

Both source-image and inverse candidate maps use interval arithmetic. Surface knot preimages are refinement hints, not certificates; source queries cover affine rounding slivers independently. Reverse directions, partial/nonunit ranges, and closed UV-piece junctions are enclosed. Callback lookup is logarithmic in UV-piece count plus intersected pieces; resource caps and cancellation follow existing bounds machinery.

The selected curve uses fitCurveCertified at the existing quarter-tolerance budget. Every supported adjacent carrier independently verifies ACTUAL output cubic Bezier coefficients at its unchanged allowed budget, even if the selected carrier used fallback. Unknown UV families retain their explicit sampled fallback. Supported unresolved bounds reject, never fallback. Original body is copied atomically as before, with unchanged topology, pcurves and history behavior.

## Verification so far

Private build.py compiles two affected TUs from actual compile_commands.json. Private legacy-tests links dedicated objects before current archive using actual link.txt. SurfacePoles20/20 PASS, including repeat on final frozen objects (legacy-final.log). probe-fixed.log reports accepted edge error1.05911e-11/checkBody0 at the previously missed peak; direct legacy fit deliberately remains erroneous, demonstrating cause.

Test agent reports5/5 new groups PASS (0.58s): eighteen single-face variants across rational/scale/pose/interior iso/nonunit/reverse mappings; hidden shared-neighbor gap rejected while coherent simultaneous edits succeed; direct composition knot/bound/unsupported-diagonal checks. Additional groups cover nested valid instances/materialized affine maps, invalid input, and cancellation. Independent review on final SHA reports30/30 PASS: rational nonclamped tensor degree3x2, weights scaled1e-100/1/1e100, fixedU/V interior, reverse/nonunit/nestedTrim, L-shaped UV pieces and nested placements; dense2001 checks always <= certified deviation <=1e-7. No static blocker on endpoint, inverse pullback or homogeneous extraction. Sanitizer is checking the same final sources.

## Explicit limits

Diagonal/nonlinear UV composition, angular conics, unequal-weight rational degree-one UV maps and other support families remain unsupported/sampled. A recognized affine isoparametric map outside the natural surface domain (including periodic shifts) fails explicitly rather than being silently sampled. Constant UV maps are unsupported. Surface singularity and global self-intersection checks elsewhere in this operation remain unchanged and sampled; this delivery certifies the supported edge approximation/adjacency residual only. Budget exhaustion can reject valid difficult geometry.
