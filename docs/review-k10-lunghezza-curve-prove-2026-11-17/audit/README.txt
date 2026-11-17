K10 arcLength / private CurveLengthCache — independent static audit V2

Two candidate files only. SHA256 is recorded in static-review.json.
The V1 audit artifacts at the parent directory are copied unchanged; no older seal was mutated. This supplement supersedes the former limitation about squared-norm underflow/overflow: V2 repairs speed computation locally with a guarded hypot fallback.

Independent oracle: native degree-one BSpline with poles (0,0,0),(1,0,0), clamped knots 0,0,h,h and exact full query [0,h], h=2^-600 or 2^600. Its exact derivative is 1/h, and exact physical length is 1. The positions lie in [0,1]; only parametrization is extreme. Legacy squared norm respectively overflows to +Inf or underflows to zero. Fraction identities and scalar Python hypot are independent algebra; they do not claim candidate C++ execution.

ROOT baseline attribution: small-h watchdog exited 124 after about 0.301 s with printed norm Inf; large-h returned length zero; preset-token Line returned 1. ROOT also records a huge-range Line whose quadrature center becomes nonfinite. These are API/native numerical/cancellation defects, not a claim about an ordinary user CAD document. See copied raw text/JSON and ROOT probe source.

Review: normal squared speed retains the old sqrt(dot(d1,d1)) arithmetic. Non-normal squared speed is handled by exact componentwise zero or hypot2/3; derivative and final speed must remain finite. No tolerance/cap/slack change. Both API and private cache check finite per-span integral and accumulated total; cache puts never publish a nonfinite value. Valid cache prefixes and counters may remain after later failure; no transactional get is promised. Tolerance legacy behavior, including zero and negative inputs, remains untouched.

Limits: adaptive quadrature still estimates error and has its existing depth policy. This is not a continuous interval certificate for arbitrary curves or an operation-wide geometric guarantee. C++ V2 runtime belongs to ROOT and is intentionally not inferred from this static audit.

Reproduce ONLY the safe independent scalar oracle:
python3 fraction-dyadic-speed-oracle.py.txt
No C++ compilation, executable or library is included. Do not run the baseline velocity reproducer without an external watchdog.
