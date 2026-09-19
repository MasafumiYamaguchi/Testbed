# 0011: Repeatable bounded edit benchmark

A static screenshot does not measure editing. This benchmark uses one saved
8-cell Scene and three separate deterministic trajectories: move cell 0 by a
bounded sine offset, move the camera, or alternate exposure by 0.5 EV. All seeds,
other source fields, sun and quality remain fixed. Thirty untimed warm-up frames
precede 30–10,000 timed updates. Every timed frame must submit the latest accepted
Document revision at the requested dimensions. No adaptive downsampling or
artificial frame delay is used. Closing/minimizing the window invalidates an
incomplete run rather than silently reducing its sample count.

For each edit, CSV separates command recording CPU elapsed, submission CPU
elapsed, fence wait CPU elapsed, edit-to-successful-submission and edit-to-GPU-
completion wall time. GPU completion includes a fence; physical scan-out is not
measured. One frame is in flight, intentionally bounding measurement ownership;
this serial workload does not model pipelined interactive throughput. Total
measurement-wall-time update Hz includes inter-frame work. The separately named
serial-work rate excludes those gaps. Neither claims monitor presentation FPS.
Nearest-rank p50/p95 is ceil(q*N)-1 in sorted samples, tested at rank boundaries.
The existing UI latency logger now uses that same explicit percentile rule.

A cached request may render current procedural density while a replacement bakes.
CSV records the actual density mode and whether HDR was recomputed per sample;
summary counts expose fallback and exposure-only HDR reuse. Camera/exposure tracks
must not rebuild density. Background bake logs separate CPU elapsed excluding
fences from submit-and-fence wall time. These values are not pure GPU timestamps.
Validation/readback and the final settled screenshot occur outside timed samples.

`benchmark-editor.ps1` runs each trajectory with direct, 128³ and 256³ cache modes.
Hosted CI uses 60 updates at 160×90/64 view/8 shadow steps with validation enabled.
For the deferred physical test use its defaults: 300 updates at 640×360, Release,
and explicitly opt into `-DisableValidation` after a validation-enabled run.
The adapter, seed recipe hash, arguments, CSV and final real screenshots are kept.
Actual residency, GPU timestamps, scan-out and longer lifetime checks remain
unmeasured. The user authorized subsequent phases while physical checks are deferred;
this benchmark does not fabricate a Go result for those missing measurements.
