# K10 mapping oblique — frozen V3

Core delivery: final/ contains six files and SHA256SUMS. changes.patch is relative to the original production snapshots (*.before). This agent changed only private staged files; integration, shared build, CMake, architecture and shared documentation are coordinator-owned. AGENTS/CLAUDE and the focused graph were read before implementation.

## Supported extension and unchanged limits

New fk_affine_surface_fit.cpp/.h parses ORIGINAL Line2 or equal-weight degree-one BSpline2 UV pieces, including Trimmed wrappers. It preserves original Line origins/directions or BSP poles/knot anchors even on partial requested ranges. The new family is ORIGINAL ExtrusionSurface with Line/NURBS generator, including existing Trimmed/Transformed generator support. Tensor-product BSpline diagonal composition remains unsupported; its old isoparametric route is unchanged. Other offset, solid and higher-degree-source mapping families remain explicit legacy paths.

The general primitive uses C(u(t))+v(t)D. For each original query, the affine v(t)D term is degree-elevated to three and subtracted from the actual candidate cubic Q(t). The existing outward position machinery then bounds C(u(t)) against Q(t)-v(t)D, using original homogeneous source coefficients and outward inverse-u pullback. No v(u) slope is constructed. u is classified constant ONLY by exact original coefficient equality; every nonzero du, including1e-16, retains its actual variation. Exact-constant-u uses an enclosure of the original generator point. Reverse, nonunit parameter, source knots, partial ranges, and closed UV-piece junctions are covered. Typed public pieces may jump: BOTH endpoint values at closed queries are included.

Recognized unsupported generator/UV families return empty; recognized domain, finite, resource or terminal-bound failures never silently fall back. Natural-domain/full-corridor requirements are retained. Resource limits:100000 UV pieces,200000 preparation events/breaks and200000 aggregate UV/source-span visits per query, plus existing original-source coefficient/product caps. Cancellation occurs throughout parsing, preparation and queries. Data are owned by the returned oracle.

## Adapter, bridges and regression prevention

Existing local map/Jacobian guards, complete source-region height bound, source edge and EVERY original trim bound, original mapped vertex check and atomic publication are retained. For Extrusion the adapter can preserve both affine UV coordinates. It first prefers the previous well-conditioned isoparametric approximation only if its entire displacement is continuously bounded and charged to the SAME existing budget, and the full original/actual corridor remains valid. This is an approximation proven by the metric bridge; it is not a tiny-du classification in the provider. It prevents regression of posed isoparametric cases while leaving meaningful small-u variation intact.

The adapter now reserves the COMPLETE edge/trim bridge before fitting: (1) build actual UV curves and edge bridge; (2) take the maximum over ALL original source fin bridges; (3) fit and recertify actual cubic coefficients with the remaining budget. Oracles are constructed sequentially only in phase3, avoiding accumulated source caches. Obsolete image.fitError storage was removed.

## Measured cause of the scale1000 failure

An early diagonal version fit edge2 at1.9963656671755555e-8, after reserving only the source-edge bridge2.473601919976174e-11. A later original-trim bridge4.110954573345569e-11 produced total2.0004766217489015e-8, exceeding the unchanged2e-8 budget. debug-trim.log captures these terms. Precomputing the full bridge before fitting resolves the same mandatory scale1000 case, without inflating tolerance or relaxing the final check.

The initial unsnapped-only adapter also regressed the previous posed isoparametric scale1000 case: nonzero roundoff u components made the inverse pullback poorly conditioned. Restoring the previous metric-proved isoparametric approximation as a preferred route resolves it; the direct nonzero-du provider remains unchanged. This distinction is covered by both old and new regression gates.

## Verification at freeze

Private strict-FP compile of three affected/new TUs succeeds. New staged seven-group suite PASS7/7(.72s), including twelve matrix variants at scales.01/1/1000, rational data, pose/reverse, nested generator/source trims, holes/history/roundtrip, original Line extrusion, knot pullbacks/wrong candidates, tiny nonzero u, native Line2, constant-u and closed junctions, cancellation/atomic failure and unsupported tensor. The original native_rotation_shift reproduction now has edge error1.023589e-8 versus baseline.001, below the returned certified deviation and unchanged2e-8 edge budget.

Old mapping-carrier suite PASS9/9 after the isoparametric regression was fixed. Private full legacy surface-mapping runner PASS; old surface-composition suite PASS5/5. Independent first provider audit reports48/48 fits and28/28 typed/query/contract checks PASS. Final replay of prior107 checks and new76 checks plus sanitizer are coordinated separately on these frozen hashes; the coordinator owns current full-build results.

No claim of continuous map regularity, global injectivity or certification of arbitrary sampled callbacks is added. Tensor diagonal, unsupported offset and other remaining K10 sites remain open. Natural-boundary interval uncertainty can still refuse a valid posed case explicitly, as documented in the preceding carrier fix.
