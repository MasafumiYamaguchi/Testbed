# Sun optical-depth cache (Issue 18)

Opt-in `--cache 128 --sun-cache 32` (or 64); zero selects direct shadow marching.
The Inspector exposes the same fallback. The cache is R32F tau at voxel centers,
linearly interpolated then converted with exp(-tau). Interpolating T would bias
in a different direction; no equivalence is assumed. Nonnegative interpolated
tau preserves T in [0,1]. Clamp-to-edge in the half-voxel boundary region is an
explicit approximation. Outside the envelope T=1. Noncubic local bounds use
normalized coordinates; light directions retain inverse-TRS scale so distances
remain world metres. Low sun and exactly zero direction components use the
existing robust box intersection.

Generation samples the current trilinear dense field with the same hard cuts,
base and envelope protection as direct shadow marching. Direct shadows remain
available at all times. While interacting or awaiting density, the renderer uses
direct shadows. Settled generation is one compute dispatch on the same command
buffer immediately before the consuming frame, so an old shadow cannot be
consumed with a new density/optical state. This bounded implementation does not
claim background generation: its dispatch cost is included in the frame fence
and edit latency. It can hurt editing latency; default remains off.

The key contains density/TRS hash, density extent, normalized sun direction,
extinction, far clipping distance, shadow steps and cache resolution. Camera
position/orientation, exposure, albedo, g and irradiance reuse the cache. Density
publication invalidates the HDR frame, recomputes the key and generates the
matching shadow before drawing. No post-filter noise is applied.

32 cubed costs 128 KiB and 64 cubed 1 MiB; density working budget reserves 1 MiB
for this resource. Allocation replacement may briefly own old and new shadow
textures (up to 2 MiB); the overall preview budget has headroom, but the density
reservation conservatively becomes 2 MiB. Logs expose build revision/key,
resolution, bytes and CPU recording time. GPU execution is included in the
existing frame submit/fence wall measurement, not mislabeled as GPU timestamps.

CPU tests cover empty/homogeneous, noncubic bounds, axis-aligned and low sun,
key invalidation and view-only reuse. GPU validation reads every tau voxel and
compares independently with CPU dense sampling (T max error <= 0.005). Windows
CI captures five recipes at direct/32/64, exports linear EXR and records RGB
errors. View T must remain unchanged within 1e-6. Approximation RGB error is
reported, not silently accepted as a physical reference. MC must retain its
own exact estimator. Physical timing and DCC review remain deferred.
