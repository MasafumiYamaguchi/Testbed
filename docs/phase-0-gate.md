# Phase 0 gate: Hold (Issue #14)

Decision as of 2026-09-19: **Hold**. Phase 1 remains unstarted. Functional
shape/editing and cache checks have evidence, but no RTX 5070 Ti 16 GB result,
pure GPU timestamps, actual GPU residency, or sustained edit performance exists.
Hosted Windows uses Microsoft Basic Render Driver (WARP), driver
10.0.20348.5386 with validation enabled. Its measurements cannot establish the
physical target. OpenVDB spike #63 is being verified separately.

## Fixed inputs and repeatable comparison

`tests/fixtures/gate/` contains complete version-2 Scene files: tall, wide,
fusion, flat base, local cut, detail seeds 17 and 18. Five shape cases also have
front and side views. Camera, sun, exposure, envelope and all random seeds are
saved. The detail pair differs only in detail seed (checked by CPU tests). No
seed search selects the images. Existing editor smoke tests exercise four
shape operations, actual gizmo Move/Scale input, one-step Undo/Redo, save/reload,
front/side/top/inside views and opposite lighting.

`capture-gate.ps1` launches the actual editor for each fixed Scene, records
seven cases in direct/128³/256³ modes and ten additional direct views. It copies
and SHA-256 hashes every recipe, retains the logs and GPU framebuffer PNGs, and
asserts the requested render dimensions and zero pending jobs at capture.
The viewport is 640×360 screen pixels; hosted runs render internally at 160×90,
64 view steps and 8 shadow steps. Each independent process runs 90 frames and
captures after 60 warm-up frames. The scene is static: these are correctness
comparisons, not a sustained frame-rate benchmark. CPU transmittance is checked
for every image pixel, with an independent trilinear reference for cached mode.
Cache-vs-direct T errors are reported separately from sampler-reference errors.

For a physical-machine capture at the proposed resolution:

```powershell
./scripts/capture-gate.ps1 -Executable ./build/windows/Release/white_app.exe -InternalWidth 640 -ViewSteps 64 -ShadowSteps 8 -OutputDirectory evidence/gate-physical
```

This command alone does **not** satisfy the performance gate. Retain the commit,
GPU/driver, adapter memory and exact arguments. Then measure a fixed repeated
edit trajectory after warm-up, at least 300 displayed updates, with direct,
128³ and 256³ modes separately. Record completed frame intervals and accepted
edit-to-present latency, plus total and application GPU residency. Timestamp
generation, raymarch, transfer and CPU wait separately; the current portable
SDL instrumentation only supplies CPU recording and submit-to-fence wall time.
Do not relabel these as GPU time. Repeat a longer resource-lifetime run and
inspect validation messages before considering Go.

## Queue review correction

Gate review found an A → B → A race: the worker's completed hash could outlive
its texture when main-thread publication rejected that result. The queue no
longer assumes a finished result remains resident. A same-hash request remains pending until active success clears it, because an
active job may already have decided to cancel. Camera-only updates still never
request a density bake. Regression tests cover completed and already-cancelling
results followed by B and return to A.

## Existing evidence and remaining limitations

- [Editor CI](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35417894669)
  verifies Move/Scale input, grouped Undo and save/reload. Screenshots are in #59.
- [Noise CI](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35418642188)
  checks CPU/GPU agreement, seed separation and hard-region preservation (#60).
- [Cache CI](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35419280016)
  passes Debug/Release (#61). At fixed 96×82/32/4, 128³ direct-T error max
  0.0132346 / mean 0.000151141; 256³ max 0.0041149 / mean 0.0000449832.
  Raw filtered base/cut leakage is measured separately; protected sampling is zero.
- [Revision CI](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35419622233)
  ends at revision 120 with zero pending work; 59 stale jobs discarded (#62).
  Last reported acceptance-to-submission p95 is 8.9503 ms across 104 samples,
  which excludes physical scan-out and cannot substitute for target-machine latency.
- Requested cache resource sizes are bounded; they are not actual residency.
  WARP's last high-quality idle frame took 87.7998 ms submit-to-fence, at an
  internal width of 256, not the required 640×360.
- Low-resolution edges visibly alias; aggressive erosion can remove much of a
  thin cell. Existing bounded noise constraints protect forbidden regions but
  do not guarantee an artist's preferred silhouette. Revise existing quality/
  parameter choices if the fixed comparisons are unacceptable.
- VDB results, dependency versions and distribution notices live with #63.
  External DCC compatibility, Unicode export paths and production-scale export
  are not established by the small write/reopen spike.

Go requires evidence for the provisional target: Release on RTX 5070 Ti 16 GB,
internal 640×360, one cloud with up to eight cells, 128³–256³, at least 10 fps
while editing, p95 update latency no more than 200 ms and app GPU memory no more
than 2 GiB. No new backend, sparse structure or Phase-1 feature is introduced to
hide missing measurements. The Hold can be revised only from recorded evidence.
