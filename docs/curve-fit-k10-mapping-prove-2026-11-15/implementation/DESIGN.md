# K10 mapping — agreed minimum scope

Production unchanged during staging. AGENTS/CLAUDE and graph read. Existing entry wrapBodyOnSurface does not use the known-carrier hook. Generic edge fit and surface fit can omit the same target knot feature; their sampled coherence check therefore accepts mutually coherent but wrong geometry.

## Families and integration

New focused fk_mapping_carrier.cpp/.h helper returns optional<Body>. It recognizes flat planar source faces on zero offset height, source Line3/polynomial degree-one BSpline3 edges (Trimmed supported), and affine isoparametric images. Higher-degree source trims, diagonal UV images, nonzero height offsets and unknown families retain the explicitly legacy path. Recognized inconclusive bounds throw. Target is original BSplineSurface or original ExtrusionSurface with Line/NURBS generator; no rounded conversion to NURBS. The existing generic 27-point Jacobian guard is executed before the helper, unchanged; no continuous injectivity claim is made.

Original target carrier is copied/shared as immutable geometry. Source topology/history is preserved. Face sense includes the planar source orientation determinant. Actual SP-curves are explicit degree-one affine maps in the original edge parameter, not sampled inverse fits. Edges use continuous certified composition on the actual target and actual SP-curves.

## Continuous map-roundoff bridge

The original map coefficients come from the unchanged source frame, frozen anchor scaleU/scaleV, angle/shift/anchor, and original source edge poles/knots or Line origin/direction. Outward affine arithmetic encloses the difference between this original map and the constructed UV coefficients over whole spans. Small residual cross-axis components are charged to that error, never silently declared zero. Entire original and constructed UV rectangles plus their connecting corridor must lie in natural target domain; periodic shifts outside are explicitly unresolved.

For global bounds M_U>=||Su|| and M_V>=||Sv|| over that corridor, target displacement is <=M_U*epsU+M_V*epsV. Add H, the maximum source height plus normalOffset. For a planar source, height is affine on the whole face; a convex box containing every boundary's complete geometric hull also contains the bounded planar region, so its affine extrema enclose the interior. Both actual edge geometry and the support region receive this check. Fit error plus map bridge uses the existing connection/fit budget without inflation.

New bounds API detail::surfaceParameterBounds(SurfacePtr)->optional<SurfaceParameterBounds{u,v}>. It derives quotient derivative bounds from ORIGINAL homogeneous controls (normalized positive weights), with outward arithmetic: S=H/W; S_u=(H_u W-H W_u)/W^2, similarly v. Derivative-control hulls bound H,H_u,W,W_u throughout the full natural knot rectangle; Wlower>0. Recenter H to reduce cancellation, with original placements applied outward. Extrusion has S_u=C', S_v=D and uses original curve controls. Unsupported returns empty; nonfinite/resource/unresolved recognized data throws. No derivative samples are certificates.

Provider surfaceCompositionErrorBound is extended to Extrusion without approximate conversion: fixed v adds v D W to original generator homogeneous coefficients; fixed u evaluates original generator outward and constructs its affine v line. Existing parameter pullback and residual proof are reused.

## Planned verification

Narrow degree-six .001 feature preserved by carrier and certified edges; polynomial/rational targets; pose/scale and source reverse/nonunit degree-one trims; full actual result and history/source immutability; holes; mapped sense; direct metric bounds versus independent derivative evaluations; invalid finite data/domain/cancellation and atomic failure outputs; legacy mapping runner. Sanitizer separately. No surface-interior regularity certification is added, and unsupported sampled fit sites remain explicit.
