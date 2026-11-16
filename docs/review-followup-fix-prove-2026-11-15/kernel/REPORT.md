# K7 / K8 / K9 — implementation and verification

Owned production changes: `kernel/fk_offset.cpp`, `kernel/tests/test_offset_validation.cpp` only. No header change. Parent registers the standalone no-Qt/no-OCCT target with fk_test_main.cpp. Original source stored as fk_offset.before.cpp, narrow patch in changes.patch; hashes in SHA256SUMS. All compilation/linking here used private /tmp objects and compile_commands/link.txt from forgecad-release, no shared build output was mutated.

## K7: remove false analytic recognition

Removed the entire nine-node black-box recognizer, including translation, line and circle branches. Generic callback offsets now fit on source/pcurve breakpoints. Retained exact constructions require information about the whole map:

* Extrusion of a line has constant normal. A circular extrusion whose circle plane is coordinate-normal and whose extrusion direction matches that coordinate axis has an exact radius offset. Other circular orientations take the general fallback; no near-zero cross-product recognition is used.
* Untrimmed-seam raw edges supported by planes or by cylindrical generators with identical source/carrier axis directions have constant normals and are translated.
* A coordinate-axis cylinder and its known coaxial circular section give the exact new section radius. Explicit component identities, radius equality and the single support establish this case.
* Existing offsetJoinedFaces sectionCircle constructions remain. Other seam callbacks are fitted. Revolution meridians now use the general fit rather than being recognized from samples.

Original degree-four hidden bump reproducer: dense 1001-point checks on its narrow knot span measured worst errors 6.3085505954623434e-8 at scale1 and 1.5518288971640179e-8 at scale1000, against requested 1e-7. Baseline errors were .000220863 and .220863 respectively. Final regression asserts the requested tolerance directly (no factor2).

Important boundary: the adaptive Hermite fitter remains an approximation validated by adaptive residual samples; this patch does NOT certify arbitrary callback error continuously. Independent reviewer found a separate degree27 adversarial spline that still aliases the generic fitter. Its cause and mathematical certification requirements are distinct from the removed exact-recognition branch; parent has that evidence. No assertion that every arbitrary callback is globally certified is made here.

## K8: toroidal domain and orientation

Bounds cosine over the actual requested v window, including contained extrema and periodic shifts. Full periods and very large phases conservatively use [-1,1]. Endpoint and reduction uncertainty is padded by64epsilon*max(1,|vlo|,|vhi|); radial tests include arithmetic margin. A source domain crossing rho=R+r*cos(v)=0 is rejected. On a regular domain its constant rho sign determines the oriented offset radius r+sign*d. The output must retain that sign throughout the same domain. This accepts outer trims even when the global carrier becomes spindle-like, and handles regular negative-rho source trims with r-d. Domains too close to singularities or with extremely large phases can be conservatively rejected.

Tests cover regular full torus, horn/spindle offset rejection, horn source rejection, valid exterior trims at three period shifts, invalid inner trims and a regular negative-rho source branch. Independent reviewer additionally passed a10-case matrix, including negative-branch point error1.2e-15.

## K9: cooperative cancellation

fitCurve checks cancellation at entry, each refinement node and immediately before/after every callback, including derivative probes. It checks again before publishing deviation/output. OperationCancelled is not caught or converted. Tests cover pre-cancel (zero callbacks), cancellation inside the last .75 residual callback, twentieth callback during refinement, unchanged deviation on cancellation, and token reset followed by successful fitting.

## Evidence

* Baseline new regression:1/4 pass (expected failures K7/K8/K9), before.log.
* New final regression:4/4 pass, final.log.
* Legacy Offset initial broad filter:26/27; only rounded-prism exact-area regression failed after removing sampled circle recognition (legacy.log).
* Added geometric cylinder-section/generator constructions restore that original test without tolerance changes:1/1 pass, roundedrect.log.
* Legacy Shell broad filter with removal of the recognizer:18/18 pass, shell.log.
* Representative Revolution line/circle geometry and circular extrusion exact CurveType are in the new regression.
* Parent performs final full application build/CTest against the frozen production source.

Build warnings about unused checkCurvature parameters in existing offsetBSpline/C2 helpers predate this change and were not expanded in scope.

## Follow-up: rotational covariance probe (read-only production)

`rotation.cpp` / `rotation.log` compare default pose with frames whose axes are (.2,.3,1) and (1,1,1). Rounded-prism area differences are zero for the coordinate pose. Rotated outward/inward errors are about -2.8878e-7 / -1.6719e-7, exceeding the original1e-9 area threshold. Thus the coordinate-only exact-section preservation introduces a real representation/pose regression in aggregate area accuracy. The rotated circular extrusion remains within requested point tolerance (worst3.17468e-8 at1e-7) but returns BSpline rather than Circle. No source changes were made during this follow-up. Parent informed immediately; a rotationally covariant construction needs whole-domain geometric error bounds for near-coaxial sections, rather than coordinate identities or callback samples.

## Covariance correction (final supersedes coordinate-only branches)

Coordinate-only recognition was replaced by whole-circle bounds using the primitive frame and cylinder axis. For cylinder sections, delta bounds the difference of the projected radial vector and r*e(u), including center offset, axis tilt, arithmetic/frame normalization padding and the Fin.pcurveTolerance whole-domain 3D contract. The resulting normalized-normal error is <=2delta/(r-delta), with delta<r required. Multiplying by |d| and adding construction roundoff gives an error bound; analytic construction is permitted only within0.01 of requested tolerance. Circular extrusions similarly bound the in-plane component of D and normalize T×D using2e/(a-e). This avoids parameter samples and is covariant under rotations. Tolerant cylindrical-generator pcurves take the fit fallback rather than silently assuming their normals are constant.

Final covariance regression:5/5PASS (covariance.log). Rounded prism three poses × two scales (1,1000) × signed offsets has area normalized by scale² within original1e-9 threshold; circular extrusions retain Circle and satisfy1e-7 point tolerance. An oblique extrusion must take BSpline fit and satisfy1e-7. The standalone rotation probe after the correction (rotation-fixed.log) shows areaerror<=1.4210854715202004e-14 and point error<=1.0877919644084146e-15 at unit scale.
