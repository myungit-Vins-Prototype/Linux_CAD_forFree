Final clipboard hygiene supplement, no production changes.

Permanent changes versus header2e118679: local QMimeData clone and RAII ClipboardGuard definitions; guard instance lives inside the real mass-dialog callback after visible+settle, before first Copy or text selection. Restore Clipboard and Selection when supportsSelection(), then process events while dialog is still open, before reject. Stack unwind follows the same cleanup. FindBuffer is untouched. Runner adds only explicit QMimeData include.

Clone retains text, HTML, image QVariant, URLs, color QVariant and exact QByteArray data for every advertised original format. Its formats list includes any standard aliases materialized by Qt setters. retrieveData delegates typed values to QMimeData and returns saved original bytes for QByteArray requests. All previous clipboard/selection contents remain only in memory: no dumps, hashes or logs. Manifest hashes are source/log/binary-file hashes only.

Final private offscreen driver compiles the exact permanent method, same frozen GUI12af and mass3e74 production objects as layout proof. Test74/74PASS. The isolated process seeds its own MIME sentinel with text/HTML/image/URL/color and binary bytes including zeros/nonASCII, then verifies standard values and all original formats/bytes after guard restoration:12/12PASS, exit0. A separate private header injects throw immediately after first Copy to test true stack unwinding; sentinel13/13PASS, exit0, with23 pre-failure UIchecks. The injected throw and compile macro are diagnostic-only and absent from the permanent test.

Offscreen reports supportsSelection=false. No runtime Selection-restoration claim is made here; root Wayland runner may cover that branch. Both channels use the same clone helper. No sentinel is placed on a real display. Root owns subsequent full TU/CMake/Wayland final build. No repository writes or user CAD reads were performed; previous proof seals are verified unchanged.

Final permanent header SHA57fc820e437b1c14ffd514298c84c2639880b5bf610f1092ac7256702ee2f9da.
Final runner SHA3d1010e64b28bf8100cfeb7e93485a6095e3d6148528126549b673d27b40b490.
