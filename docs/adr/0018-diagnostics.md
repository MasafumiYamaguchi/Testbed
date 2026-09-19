# Diagnostic views and snapshot evidence (Issue 21)

`--diagnostic 0..11` or Inspector Diagnostics selects:

| Mode | Raw RGB | Display white point |
|---|---|---|
| 0 | Background-composited radiance | Exposure/Reinhard |
| 1 | Local Z-midplane density | 5 density units |
| 2 | Integrated view optical depth | 8 |
| 3 | Density-distance-weighted sun T along view | 1 |
| 4 | Single-scattering radiance, no background | Exposure/Reinhard |
| 5 | Ray intersects envelope AABB | 1 |
| 6 | Midplane conservative brick maximum | 5 |
| 7 | Midplane brick occupancy | 1 |
| 8 | View density evaluations actually executed | 64 |
| 9 | View plus direct-shadow density evaluations | 576 |
| 10 | Empty-brick skip intervals | 64 |
| 11 | Invalid density or sampled majorant violation | Magenta flag |

Diagnostics use raw RGBA32F output with view T in A; the sidecar explicitly
marks RGB as diagnostic data, not composited radiance. Scalar display divides
by the documented white point, clamps and applies sRGB without exposure or
Reinhard. Mode 4 keeps the radiance display transform. Local slices are local
XY at normalized Z=0.5, independent of camera; support is the camera ray AABB.
Existing editor wire geometry and axis slice controls remain available.

No statistics are downloaded during normal drawing. Counters are shader-local
and only exposed for diagnostic modes. Mode 0 skips counter updates; the
normal nine-track benchmark remains in CI to measure the resulting build's
wall timings rather than asserting zero overhead. Diagnostic commands explicitly
request readback/EXR. GPU pass timestamps remain unavailable in this SDL backend;
frame CPU record and submit/fence wall times are logged separately. This does
not claim GPU profiler timing. Inspector shows Document/rendered revision and
density/sun/majorant producer revisions, with current-input status. Older
producer revision can be valid after exposure-only edits because input keys
match; stale mismatched cache is never selected.

Mode 11 checks nonfinite/negative density and a 2e-6 numerical margin above
conservative maxima. It disables empty skipping to avoid hiding a zero-bound
violation. This is an extension point, not a complete GPU exception system.
Scene validation rejects invalid optical inputs; explicit export readback
rejects nonfinite pixels. No external telemetry is sent.

CI captures eleven cut-volume modes plus six analytic empty-volume modes.
Every empty diagnostic pixel must equal its known scalar (zero, or one for
sun T), independently checked after EXR decode. Screenshots, raw values,
full Scene, settings/revision and numeric logs are saved together. Physical
GPU review remains deferred by user request.
