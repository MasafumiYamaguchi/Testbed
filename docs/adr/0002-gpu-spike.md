# 0002: D3D12 field validation spike

Status: implemented; acceptance awaits CI/visual inspection and target GPU tests.

Use SDL3 GPU with one backend, `direct3d12`, and Dear ImGui's SDL3/SDLGPU3
backends. Compute writes R32_FLOAT 3D textures; the same textures are sampled by
compute and fragment shaders. There is no substitute CPU renderer when GPU
initialization fails. Unsupported format/usage combinations fail explicitly.

HLSL compiles offline to DXIL with Microsoft DXC v1.8.2505.1, downloaded by
`scripts/fetch-dxc.ps1` with the release's SHA256. The shadercross candidate stays
pinned but deferred: cross-compiling DXIL to other backends is unnecessary for
this single-backend spike. Direct DXC removes SPIRV-Cross from the required
toolchain without introducing another GPU abstraction. Revisit only when a
second output format is required. This decision concerns shader compilation,
not a change in the selected SDL GPU API.

## Shader and transfer contracts

- Compute write textures/buffers: u0, space1; uniform buffer: b0, space2.
- Compute sampled texture and sampler: t0/s0, space0.
- Fragment sampled texture and sampler: t0/s0, space2; uniforms: b0, space3.
- `uint3 extent; uint fixture` occupies exactly 16 bytes. Dispatch threads are
  4x4x4; out-of-range invocations return before writing. Sampling kernel is 1x1x1.
- CPU index order is X contiguous, Y next, Z last. Voxel centers sample at
  `(index + 0.5) / extent`. R32F linear sampling clamps to the edge.
- Texture download row bytes are padded to 256; `rows_per_layer = height`.
  Transfer buffers stay alive until submitted fences complete. CPU reads occur
  only after fence wait, then unmap before resource release.
- Resize/regeneration waits for idle before replacing resources. This simple
  synchronous spike intentionally measures no interactive-performance claim.
- Offscreen RGBA8 includes the real ImGui draw; it is blitted to the swapchain
  and independently downloaded for screenshot. Desktop capture is also kept.

## Verification

`white_app --self-test --lifecycle-test --frames 180 --capture client.bmp` runs
1x1x1, 17x19x23 and 32x32x32 ramp/impulse cases and checks every voxel, including
corners and final depth slices, for finite values and absolute error <= 1e-6.
The asymmetric ramp distinguishes all three axes. The 17-wide case tests row
padding and dispatch overhang. Linear filtering checks voxel centers, a
fractional interior point and boundary clamping. Its threshold is the ramp's
7/256 gradient sum times 1/256 fractional precision, plus 1e-6 rounding allowance.

Lifecycle automation shrinks, minimizes, restores and returns to initial size
before screenshot. Device/adapter/driver logs and Windows adapter inventory
accompany the artifact. `debug=true` requests the validation layer but does not
prove it was installed or enabled: inspect the logs. Repeated launch and memory
trend/target RTX 5070 Ti testing remain required for full Issue #4 acceptance.
Hosted adapter correctness is distinct from physical GPU performance.

Public API references: SDL 3.2.28 `SDL_gpu.h`, Dear ImGui 1.92.1 backend headers
and example_sdl3_sdlgpu3. Original application code; dependencies unmodified.
