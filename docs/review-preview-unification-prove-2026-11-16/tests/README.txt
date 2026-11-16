Independent PRE5 / PRE6 / residual UI2 regression review — final private freeze

RESULTS
Final staged fix: 115 PASS, 0 FAIL, exit 0.
Previous lazy stage with no footer helper: 79 PASS, 36 FAIL, exit 1.
Original 79-assertion suite: baseline 69 PASS / 7 FAIL; staged fix 79 PASS / 0 FAIL.
All runs explicitly count 11 actual visible FunctionDialogPanel instances and
11 successful cancellations with cleaned picker, callbacks, preview and lock.

FREEZE
Six source hashes match stage/freeze-sha256.json. Final common.cpp:
bd1006cc5689982ed183b6499292ce3efe956daf8028cc73ca7258036ad9ba82.
Final surfaces.cpp:
3e1a52dbd703f3d875b28feac751966b95defebc10975a78fb6adeb8f56d32bc.
Private stage objects replace five corresponding Release app objects. All other
app objects/kernel remain the current Release build. No sanitizer or OpenGL
rendering claim is made. No production source was changed by this test work.

ELEVEN REAL PANELS
Pattern, Extrusion, Transform, Thread, Offset/Shell, Ruled, Boundary, Fill,
Planar, Datum, Measure. Offset/Shell counts once because it is one runner.
The observer requires the exact title, visible state, actual FunctionDialogPanel
dynamic type and held interaction lock. Early returns for absent source candidates
do not count. Fixture: two built boxes, a built cylinder and a closed square
sketch. Extrusion/Planar use that sketch; Ruled uses opposite sketch edges;
Boundary/Fill use the closed contour; Datum uses the XY reference plane.
Thread starts from a real resolvable cylindrical face, checks initialized body/size
indices, changes standard, switches body and restores the cylinder. Pattern checks
the active source-picking property, then restored enabled dock, callback removal
and re-enabled history rail.

FIXED FOOTERS
Nine runners use PreviewScope. Each checks: one preview in a fixed footer outside
the scrolling form; footer immediately before the confirmation buttons; actual
scroll movement while preview position stays fixed; hide/show without duplicated
or relocated footer/message/buttons. Datum and Measure explicitly print SKIP
because they have no PreviewScope message. On the previous lazy stage all 36
assertions fail: e.g. Pattern preview moves from y=1312 to y=178 during scrolling.
With the helper all nine remain at y=178 before/after actual scrolling.

LIFECYCLE AND STATUS
Shell waits for a real asynchronous ready result, sets thickness zero, checks
geometry/work removal and explicit invalid label immediately and after queued
work. Thickness 0.6 restores a ready preview and clears the old error. Thread
without a face and while picking never claims a calculation is running.
Two real cross-document paths run: shared Boolean -> migrated Shell and migrated
Shell -> shared Boolean. The second command is refused before visible and leaves
no callbacks/work/lock; the first panel/picker survives. The second path uses a
real DocumentTabsWindow and confirms tab consultation remains allowed. A real
command in B opens after A closes. A same-document nested subdialog is allowed.
New onShown can synchronously accept/reject without entering a dead event loop,
and is not called for a rejected cross-document command.

EXPECTED ORIGINAL FAILURES
Three Shell label/recovery assertions, two Thread state assertions, two cross-doc
refusal assertions. These are PRE5, PRE6 and residual UI2, not a new UI8 finding.

REPLAY
python3 /tmp/review-previews-refresh/build_regressions.py
python3 /tmp/review-previews-refresh/build_regressions.py --before-footer
After root integrates and completes the full Release app build:
python3 /tmp/review-previews-refresh/build_regressions.py --app-release
The last mode uses only the app's current Release objects, with no private source
or object overlay. link-app-release.json records that empty overlay explicitly.
Add --keep-platform to preserve the caller's configured Wayland platform.
Build flags/linker command come from the existing Release app/test build. Offscreen
Qt, Fusion style and empty platform themes avoid unrelated display dependencies.

ARCHIVE AND LIMITS
Sources and scripts archived only as .txt, with logs/JSON/this README; no binaries
or object files. Earlier original-baseline logs refer to the archived 79-assertion
source, not the final 115-assertion source. The final private-stage replay covers
all earlier checks plus footer checks. Full Release-object replay is pending root
build at the time of this report. No further broad investigation was performed.
