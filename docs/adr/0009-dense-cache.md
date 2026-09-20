# 0009: Dense preview cache and explicit measurement limits

Issue #11. The Document remains procedural. The optional dense path bakes the
shared GPU evaluator into 128³ or 256³ R32F, then samples that same field for both
view and shadow rays. No preview-only detail is added. Direct evaluation remains
selectable. Density edits rebake; camera, sun, optics and exposure do not rebake.
The initial diagnostic grid remains noncubic 65×67×69. A complete replacement is
published only after its bake fence, preserving the old texture on failure.
The editor catches preview failures and retains its Document/history; retry is
explicit. An out-of-budget request is rejected before GPU allocation.

Index/local rules reuse GridLayout: index (0,0,0) is the first voxel center,
local = min + (index + 0.5) * size / extent. The texture coordinate is
(local - min) / size. Hardware normalized trilinear clamp operates only inside
the saved envelope. Outside and exactly on the envelope boundary density is zero.
Filtered data can bleed across the base/cut surface. We record the raw leakage,
then explicitly clip the forbidden base half-space and complete cuts after
filtering. Soft transition profiles and fine contours still incur interpolation/
aliasing error; neither clipping nor extra noise hides it.

The checked 256 MiB GPU resource budget includes the old and replacement textures,
a 256-byte-padded readback transfer buffer, and current HDR/output targets. CPU
reference bytes are reported separately. 256³ R32F is exactly 64 MiB; replacing
a same-sized cache plus readback requests 192 MiB before render targets. These
are requested payload estimates, not driver-reported residency; SDL/driver/ImGui
allocation overhead is not measured. Compute submits at most 16 z-layers per
fence to bound each hosted software-GPU dispatch. This is a full rebake, not sparse
or local updating.

Validation reads every voxel against the CPU evaluator, samples 256 deterministic
GPU positions against direct evaluation, reports interpolation max/mean and raw
base/cut leakage, and compares full-frame cache transmittance against an
independent CPU trilinear sampler. The latter has a 0.005 absolute tolerance to
allow hardware fractional-weight quantization; direct rendering retains 0.001.
Direct-vs-cache full-frame T error is separately reported rather than hidden in
that sampler tolerance. CPU ramp/impulse tests cover noncubic bounds, half-voxel
alignment and clamp/boundary behavior. CI compares the same detailed Recipe and
camera in direct, 128³ and 256³ modes, then checks camera-only edits and a rejected
512³ allocation preserve bake count and Document.

Measurement limitation: pinned SDL 3.2.28 exposes no GPU timestamp-query API.
Logs separate CPU recording/allocation milliseconds, submission-to-fence wall
milliseconds (GPU work plus CPU wait/scheduling), requested resource bytes, and
CPU reference bytes. Timed frame fences include the complete render/UI submission,
not just the cloud shader, and are not display-present timestamps. They must not
be labeled pure GPU bake/draw times. GPU timestamps, actual residency and physical
RTX performance remain outstanding acceptance items; Issue #11 stays draft until
that gap is resolved. No private SDL internals or unverified timing estimates are
used as GPU measurements.
