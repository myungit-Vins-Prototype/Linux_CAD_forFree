# Sanitizer verification

Two explicit instrumentation passes, all private /tmp objects. `build.py` records the initial reproducible compiler/linker recipe; `build.log` contains exact commands. Both fk_offset.cpp and fk_curve_fit_bounds.cpp, test TU(s) and fk_test_main.cpp were instrumented with `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, no fastmath, fp contraction remains disabled.

The linked kernel static archive and OCCT/system shared libraries were NOT instrumented. No OCCT API is used by these tests, but its libraries are inherited from the baseline link.txt. This is targeted coverage, not a fully sanitized application.

Initial sandbox execution:3/3 oracle tests and7/7 fit tests passed during execution with no ASan/UBSan findings; LeakSanitizer failed at exit because sandbox ptrace prevents leak inspection. These logs are retained (`bounds.log`, `validation.log`) and are not counted as clean leak results.

Final outside-sandbox execution explicitly authorized by root used:

```
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 <runner>
```

`bounds-final.log`:3/3 PASS; `validation-final.log`:11/11 PASS. `status-final.log`:both exit0. No ASan, UBSan or LSan reports. The final11test TU was compiled after its last modification (source16:10:26, instrumented object16:12:11).

For this pass, fk_offset.cpp was SHA4563435968a70fdcb311ca0ecc041e8057462cd36ef107c412f2fe98cfe069b6 and the bounds source SHAeaaadbb944037473c08a0217b285f6a62e531c8058cccfbc95bc61258c97e284. The subsequent final7c8a2248 source only restricts the full-period guard to BSpline bases after unwrapping; the tested NURBS branch is identical. `build-guard.py` / `build-guard.log` instrument the final source and relink the same11test object; the dedicated periodic filter is rerun on that exact final source (see guard-periodic.log).
