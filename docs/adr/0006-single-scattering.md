# 0006: Direct-density single-scattering preview

Status: implemented for Issue #8; hosted/physical validation reported separately.

The ray marcher calls the same `densityAt` used by #7's compute generator; it does
not sample the low-resolution diagnostic cache. Clip rays against the explicit
local envelope. Transform the world ray without renormalizing its local direction:
the parameter remains world metres even under nonuniform scale. Near/far and
camera-inside paths use a slab intersection with explicit zero-direction handling.

Integrate midpoint constant segments front to back: opacity `1-exp(-sigma_t*ds)`,
`L += T * source * opacity`, `T *= 1-opacity`. A third-order small-tau expression
avoids cancellation. Source is sun irradiance * albedo / (4*pi) * shadow T;
shadow T uses a separate midpoint march toward the sun. No ambient term or
multiple-scattering approximation is added. Background is a constant radiance
and is attenuated by final T. There is no early termination cutoff in this
baseline, so opaque skipping cannot hide quadrature errors.

GPU output is RGBA32F with RGB linear HDR radiance and A transmittance. Readback
rejects NaN/Inf/negative channels and T outside [0,1]. A separate pass applies
2^EV exposure, Reinhard display mapping, and the sRGB transfer curve. Changes to
exposure do not recompute density, light or the HDR image. This display mapping
is not a claim of production color management (Issue #17).

For hosted software GPU, the default internal width is 160, view steps 64 and
shadow steps 8. The static image is reused until shape/sun/quality/size changes.
This is a minimal local image cache, not #12's revision/job system. The UI keeps
density slices and exposes quality/sun/exposure. Initial camera is fixed for
fixtures; interactive camera/editor integration is #9. HDR allocation is capped
at 16 MiB and the output target at 64 MiB.

GPU compute tests use the same segment integral on eight homogeneous cases and
compare T and a known source integral against analytic values, absolute tolerance
1e-3. CPU tests cover vacuum, thin/thick slabs, pure absorption, zero distance,
inside/axis-parallel/boundary rays and invalid values. The self-test captures 64
and 128 view-step outputs, reads raw HDR+T and reports maximum/mean differences
at fixed density, camera, shadow steps, sun and exposure. One step-halving result
is evidence, not proof of convergence or correctness of shadow quadrature.

GPU timestamp duration and physical RTX performance remain unmeasured. Hosted
CI screenshot/validation success is not a performance acceptance result. No
hardware ray tracing, path tracing, atmosphere, terrain or denoising.

Visual verification caught an all-background regression despite finite HDR and
isolated compute tests passing. GPU probes showed correct camera and density
uniforms, but the dynamic-vector-index slab loop returned a miss for the central
ray on hosted WARP. The slab implementation now uses explicit scalar axes. Six
GPU ray/box regressions cover signed, diagonal, inside, parallel and boundary
rays. Every captured HDR pixel's transmittance is additionally compared with an
independent double-precision CPU field/ray integration (absolute tolerance
0.001). This validates the camera-to-density-to-transmittance path; it does not
establish correctness of single-scattered radiance on physical hardware.
