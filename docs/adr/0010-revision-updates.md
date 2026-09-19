# 0010: Latest revision, bounded asynchronous density work

Issue #12. Document replacements retain monotonic revision and acceptance time.
Density jobs carry immutable Scene, producer revision and a canonical input hash
(including grid dimensions). Camera, sun, optics and exposure are excluded from
the density hash. Stable IDs sort cells/cuts before hashing. The hash is a cache
identity, not a security primitive. Invalidation is explicit: density invalidates
all lighting/HDR/display, optics or sun invalidate lighting/HDR/display, camera
invalidates HDR/display, and exposure invalidates only display conversion.

One worker records/submits density work using SDL's thread-safe GPU APIs; window,
ImGui and presentation stay on the main thread. The worker owns at most one
running job and one latest unstarted request. New requests replace the unstarted
one. A running request checks staleness only after its current 16-layer submission
fence. Stale resources are then released without publishing. A completed-result
slot blocks starting further work until the main thread drains it, bounding GPU
textures to the published cache plus one replacement. Pending requests contain
source parameters, not GPU allocations. Budgeting reserves 64 MiB for the largest
supported published texture while a worker runs; requested replacement, transfer
and main render targets are additional. Destruction stops requests, joins after
GPU fences, then destroys pipelines/device.

Main-thread publication rechecks the density hash against the latest Document.
An older producer revision may be reused only when its density inputs still match
(the camera may have changed); its texture is combined with the latest camera/
light/optics. A mismatched cache is never used: direct procedural evaluation of
the latest Scene remains available while the bake runs. One drag uses an internal
width at most 96, at most 32 view steps and 4 shadow steps; release restores the
user's quality. Thus slow cache jobs do not force the editor to show stale geometry.
GPU/budget errors preserve Document/history and leave direct preview available.

Acceptance-to-present-submission latency is logged once per first displayed
revision using Document's steady-clock timestamp. The distribution retains the
last 512 samples and reports count/p50/p95. It measures a successful submission
containing that revision, not physical scan-out, GPU timestamp or input-device
latency. Superseded revisions that were never presented are reflected in job
coalescing/discard counters rather than assigned fabricated latency samples.

CPU tests inject a slow active request and 999 replacements, reject reversed/
unknown completions, reject stale/failed outputs and check the invalidation table.
The Windows stress mixes sixty density edits, Undo/Redo, sun/exposure and resize,
with a cancellable artificial 30 ms delay after fences. It logs pending/running
counts and discard/coalescing totals. Screenshots/numeric comparisons drain work
explicitly at verification points; normal editing does not wait for bakes. The
final idle screenshot uses a 128³ cache and higher 256-pixel/96-step/8-shadow quality;
it is separate from fixed-quality error comparisons.

GPU work can still contend with graphics on the same device; this is a bounded
functional design, not proof of RTX responsiveness or memory-leak freedom. The
physical Phase-0 performance gate and long-run resource/latency measurements remain
outstanding. No reprojection, local dirty bricks, generic job engine or extra GPU.
