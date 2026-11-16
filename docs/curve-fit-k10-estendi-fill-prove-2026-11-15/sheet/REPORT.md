# K10: the two planar sheet offset fits

Integrated only kernel/fk_sheet.cpp and new kernel/tests/test_sheet_fit_validation.cpp. Parent owns CMake, architecture and shared docs/build. Original source is fk_sheet.before.cpp, patch changes.patch, hashes SHA256SUMS. Production SHA: source fdf2810ae1777d376fd80719e9fa8210cdfda810e450c706f6e05963129f098c; test d8b7851045e8b3cf5e1ba22477c87ff6a750bc4b679a62eecc9d81a4956b3474.

## Change and geometric contract

Both extendPlanarFace and generalPlanar compute C(t)+length*normalize(orientation*cross(traversal*C'(t),planeNormal)). Therefore the existing extrusion oracle applies with direction=planeNormal and signedDistance=length*traversal*orientation. No sign is applied a second time to direction. Supported NURBS, including wrappers accepted by the oracle, use fitCurveCertified; an unresolved oracle throws and never retries sampled fitting. Unsupported analytic sources retain the legacy fit. The parameter, source breakpoints and existing max(tolerance,1e-8*scale) fit budget are unchanged. The third fit, for implicit intersections around previous line402, is untouched.

## Tests and before/after

Two registered regression groups,12 extension operations:

*8 rational degree3 cases: single/multiple-face branch × canonical forward/reversed edge parameter × face sense, with a rotated and translated plane.
*4 adversarial rational degree27 cases: single/multiple-face branch × geometry scale1/1000. The source adds a linear drift so endpoints and midpoint have both UV coordinates varying. Explicit du/dv assertions prove the multiple-face fixture uses generalPlanar and does not alias an isoparametric branch.

Both tests compare1001 points of the actual far output edge, preserving original parameter up to the explicit edge reversal. Source and oracle operate on canonical edge parameters; the test chooses the matching orientation of the assembled boundary.

Before:1/2 groups passed; all4 adversarial operations failed the unchanged fit budget. After:2/2 passed, including the branch assertions. Commands/build recipe in build.py and measure.py, logs before.log/after.log and measure-before.log/measure-after.log. Measurement binaries explicitly link an original-source sheet object for baseline, so concurrent parent archive updates cannot contaminate the comparison.

At scale1, degree27 worst error falls from5.416313379e-5 to7.149059678e-7. Existing fit budget is1.665251477e-6 for single face (ratio32.5255→.429308) and1.665701799e-6 for multiple faces (32.5167→.429192). At scale1000 the error and budget scale together and ratios stay the same. The large budget comes from the existing geometry-size computation including face bounds of high-degree controls; it was not increased by this change. GeneralPlanar classifier sees du=1.5,dv=.3 at scale1 and1500/300 at scale1000, versus flat1e-9.

## Scope and limits

The certificate covers the two fitted NURBS offset curves. It does not certify the entire resulting sheet topology, all later assembly/trim operations, unsupported analytic legacy fits, or the untouched implicit-intersection fit. Existing oracle resource limits, positive-weight and in-domain requirements, cancellation and fail-closed diagnostics apply. No fallback hides a supported-source certification failure.

Sanitizer result is recorded separately below once execution finishes; no shared build was run here.

## Targeted sanitizers: completed

ASan+UBSan+LSan detect_leaks=1 outside sandbox:2/2 groups (12 operations) PASS, exit0, no findings. Instrumented the frozen fk_sheet.cpp, test TU and test main; reused the already instrumented frozen fk_offset/fk_curve_fit_bounds objects. Remaining kernel archive and inherited system/OCCT libraries are not instrumented (no OCCT API is exercised by the tests). Thus this is targeted coverage, not an entirely sanitized app. Recipes/exact commands and logs: sanitize/build.py, build.log, tests.log, status.log. No production files changed during measurement or sanitizers.
