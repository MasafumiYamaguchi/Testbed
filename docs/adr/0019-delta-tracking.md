# 0019 — Fixed-grid delta tracking

Status: implemented for Issue #22; Windows CI and physical-device evidence are
reported separately. This CPU reference primitive does not use GPU preview
lighting approximations and is not a production Final renderer.

## Snapshot and units

`TrackingSnapshot` owns the validated Scene, GridLayout, R32F voxel values, and
majorant hierarchy. Getters expose const data, so worker code never follows an
editing Document or partially published density. The caller must finish a new
snapshot before replacing a render job's input.

The density model exactly matches the fixed cached preview: x-fast voxel-center
values, trilinear interpolation with clamped voxel access inside the finite grid,
then the recipe's hard envelope, base, and cut mask. Grid faces and the exterior
have zero density. No procedural noise is evaluated after the grid. The hard mask
only removes density, so it cannot invalidate the grid's conservative majorant.
Homogeneous fixtures disable the base and cuts and match envelope to grid bounds.

Rays carry a normalized world direction. Begin, end, sampled distance and
extinction coefficients use world metres. The affine inverse transforms the
direction without normalizing it; a nonuniformly scaled volume therefore keeps
the correct world optical depth. Returned collisions sample extinction; the
integrator separately weights or samples scattering using albedo. Escape returns
the final grid exit, or the ray end for a miss. Internal cameras are supported.

## Proposal and failure contract

Global tracking uses the root maximum; local tracking uses the existing 8-voxel
brick DDA and its trilinear halo maxima. Every supplied majorant level is compared
against a complete rebuilt hierarchy before publication, including bricks not
crossed by the first ray. Negative/nonfinite values, shape mismatches and any
underestimate fail. This is not a randomized spot check.

Positive extinction bounds receive a 32-binary64-epsilon expansion and one
upward rounding step. This covers nonnegative trilinear arithmetic rounding;
zero remains exactly zero. The bound is inflated before sampling, while the
acceptance probability is never clamped. Every sampled extinction is still
checked strictly against its interval bound.

Within a positive interval, draw `d = -log1p(-u) / sigma_majorant`; if the flight
crosses the interval boundary, restart the exponential process in the next
interval. Accept a candidate with probability `sigma_t / sigma_majorant`.
Rejected candidates count as null collisions. Zero intervals require no random
draw. The memoryless exponential distribution makes boundary restarts valid.
The mathematical context is PBRT's [medium majorant segments](https://pbr-book.org/4ed/Volume_Scattering/Media)
and [volume scattering integrators](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators).
Implementation code is original to this project.

`TrackingFailure` distinguishes `event_budget`, `invalid_majorant`, and
`numerical` failures. The budget counts candidate events, including nulls. A
zero budget can still complete an empty/missed ray, but its first candidate
fails. Event-cap exhaustion, nonfinite arithmetic, exhausted RNG counters, and
free flights too small to advance the distance all invalidate the sample.
They never return escape or a truncated value labelled as a valid reference.

## Random dimensions and diagnostics

The counter key is `(seed64, pixel32, sample32, bounce32, stream32, event64,
dimension32)`. Ordered 64-bit integer mixing includes every field and returns
52 random bits at open-bin centers in `(0,1)`. Dimension 0 samples delta flight
length, dimension 1 accepts collisions, and dimension 2 is reserved for ratio
tracking. Integrators use separate streams and higher dimensions for camera,
phase, light visibility and roulette draws. Segment-crossing proposals advance
the event counter; no draw is reused at a brick boundary. This CPU RNG is a
separate versioned contract from the existing 24-bit CPU/HLSL phase sampler.

Results expose candidate/null counts, visited intervals and random draws.
The test executable writes CSV rows with fixture, mode, seed, sample count,
analytic survival, estimated survival, 95% interval, censored free-flight mean,
candidate/null counts and elapsed milliseconds. Timings compare CPU algorithms
on the current runner; they are not physical GPU performance measurements.

## Validation

- Four independent seeds, including a seed differing only above bit 32, at
  2,048 / 8,192 / 32,768 samples per mode.
- Homogeneous `sigma_t = 0.6 / m`, length 4 m: eight free-flight distribution
  bins, survival `exp(-2.4)`, and the analytic censored mean and variance. Gates
  use six standard errors plus the finite-count margin; 95% intervals are
  reported as diagnostics, not required to contain every analytic value.
- A sparse slab with exact piecewise-linear optical-depth integral: independent
  global/local estimates must agree within uncertainty. Local proposals must
  use less than half as many candidate events in the fixed workload. Wall time
  is reported without a platform-specific speed threshold.
- Empty intervals, internal origins, exact brick planes, opposite directions,
  zero direction components, grid misses, noncubic grids, translation, rotation,
  nonuniform scale, hard masks and caller-data ownership.
- Deliberate underestimates at every hierarchy level and NaN bounds fail before
  sampling. Excessively loose bounds with zero/one-event budgets fail explicitly.
- RNG reproducibility, open endpoints, all semantic key fields, mean and bins.

These tests establish a fixed-grid event sampler and its uncertainty reporting.
They do not establish unbiased transport through the original continuous
procedural field: voxel baking and its resolution remain part of the model.
