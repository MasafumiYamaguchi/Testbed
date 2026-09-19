# ADR 0020: Ratio tracking for reference transmittance

Status: Implemented for Issue #23. Physical GPU acceptance remains deferred by
the user's instruction. This estimator is CPU reference code and does not use
the interactive preview's sun cache.

`ratio_track` consumes the same owning `TrackingSnapshot`, fixed R32F grid,
trilinear interpolation, cloud TRS and world-metre ray as `delta_track`. The
snapshot's hard envelope, base and cut mask is applied after interpolation,
matching the dense preview model. No procedural noise is evaluated after the
snapshot is frozen. Global and local proposals use the same exhaustively
validated halo majorants; the latter changes proposal rates at brick boundaries.

For each candidate from the piecewise constant proposal rate M, the weight is
multiplied by `1 - sigma_t / M`. The implementation accumulates the logarithm with
`log1p` and compensated summation. The public result contains T, log T, candidate
count, interval count, random draw count and an underflow flag. Converting a
finite log T to an unrepresentable double T produces a reported zero. An exactly
zero factor produces negative infinity in log T. Neither event ends traversal:
later majorant or safety-budget failures must still be detected.

There is no weight threshold, roulette, residual ratio method or preview-cache
shortcut. A finite event budget is a resource guard: exhausting it throws
`TrackingFailure` with reason `event_budget` and invalidates the entire sample.
Nonfinite values, representational distance stalls and underestimated majorants
also throw; an acceptance ratio is never clamped to conceal a violation. Errors
include the seed, pixel, sample, bounce, stream, event, distance and coefficients.

The counter-based generator reserves dimension 2 for ratio-tracking flights.
Delta distances and acceptance decisions use dimensions 0 and 1. Pixel, sample,
bounce, caller stream and all 64 seed/event bits participate in the random key.
Callers must change bounce/stream for distinct light evaluations; equal keys
intentionally reproduce equal results.

## Verification

`white_ratio_tracking_tests` prints a table with fixture, global/local mode,
seed, sample count, reference T, estimated mean, sample variance, standard error,
candidate count, random draw count and quadrature discrepancy. The deterministic
seeds are 17, 42 and 4294967313; sample counts are 2048 and 16384. Each mean must
agree within six measured standard errors, an explicit quadrature discrepancy
when applicable, and a 0.00002 absolute floor.

- A homogeneous line has analytical T = exp(-4). An off-ray voxel deliberately
  loosens the global bound, so the test covers fractional weights and compares
  event work against local bounds.
- Piecewise constant voxel rows have known analytical integrals under the actual
  clamped-trilinear model, including linear transitions between voxel regions.
  This avoids comparing a smooth interpolation with an unrelated discontinuous
  analytical medium.
- A noncubic heterogeneous grid and oblique ray are compared with independently
  evaluated midpoint integration at 32768 and 65536 steps. Discretization error
  is recorded separately from Monte Carlo variance.
- Reverse rays, starts on brick faces, vacuum, missed bounds and zero-length
  paths cover interval handling. Vacuum and zero-length paths give exactly one.
- A 1000-metre thick medium with a loose bound records approximately 2000
  candidates per estimate and retains finite log T after T underflows. A tiny
  safety budget must invalidate the sample rather than return the current weight.
- Corrupt global/local bounds fail during snapshot construction. Invalid rays
  fail even in vacuum. Equal keys reproduce results and ratio dimensions differ
  from delta dimensions.

Build integration: add `src/core/ratio_tracking.cpp` to `white_core`, link
`tests/ratio_tracking_tests.cpp` as `white_ratio_tracking_tests`, and register
CTest `ratio_tracking_contract`. The test has no GPU or external dependency.

The mathematical background is the null-scattering transmittance treatment in
[PBRT's volume scattering integrators](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators).
The implementation and tests are original code.
