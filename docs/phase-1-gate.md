# Phase 1 renderer gate — Issue #26

Assessment recorded 2026-09-19: **Hold for physical acceptance; implementation may
continue under the user's explicit instruction to defer physical-machine review.**
The reference contracts have numerical evidence. The preview multiple-scattering
candidate remains **OFF by default** and is rejected for default adoption. This
assessment permits no claim of a physical Go, a production Final renderer, a
finished denoiser, or an accepted target-machine performance result.

This report distinguishes the earlier verified Windows Actions runs below from
the combined Issues #22–27 integration snapshot. That snapshot still requires
its own Actions build, runtime logs and screenshot review; earlier artifacts
must not be presented as verification of a later commit.

## Fixed comparisons and what they establish

All numerical image comparisons use linear FLOAT EXR RGB and camera transmittance
in A, with the same Scene, seeds, exposure, camera and light. The simple background
is already composited into normal RGB. Density and transport models must also
match: the MC reference uses the frozen R32F grid and protected trilinear sampling,
not an unbaked continuous procedural field.

| Coverage | Fixed input / executable | Evidence and scope |
| --- | --- | --- |
| Homogeneous slabs, ordered segments, step refinement | `white_optical_contract_tests`, GPU optical cases | Independent analytic source/extinction tests; separate midpoint convergence from floating-point tolerance. |
| Forward/back light, isotropic and signed HG, absorption | `tests/fixtures/phase/` | [Seven reviewed Windows captures](evidence/phase/README.md); CPU/GPU RNG exact, cosine error <= 1.57724e-5 and PDF relative error <= 0.000123273. |
| HDR values, channels, orientation, metadata | `white_hdr_tests`, the same phase scenes | [Seven decoded EXR bundles](evidence/hdr/README.md); RGB maxima 7.51358 and 301.414 remain above one. Display images do not substitute for linear comparisons. |
| Empty, homogeneous, cut boundary, low sun, thick cloud | `tests/fixtures/sun/`, `capture-sun.ps1`, `capture-majorant.ps1` | [Sun cache](evidence/sun/README.md) and [empty skipping](evidence/majorant/README.md) compare against the same cached density. |
| Fixed versus settled pixel accumulation | `capture-progressive.ps1` | [Repeated 64-sample images](evidence/progressive/README.md) match every RGB/T pixel exactly. Fixed versus jittered output differs because pixel footprints differ. |
| Majorant bounds, thin features, faces and noncubic grids | `white_majorant_tests`, `white_delta_tracking_tests`, `white_ratio_tracking_tests` | Full halo hierarchy validation; malformed underestimates fail before sampling. Zero-direction, boundary, transform and internal-origin cases are tested. |
| Analytic single scattering and multiple-scattering uncertainty | `white_reference_tests`, `capture-reference.ps1` | [CPU contract evidence](evidence/reference-contract/README.md); fixed empty/absorbing/internal/thick fixtures and independent image seeds. The new image script still needs Actions artifact review for the integration commit. |
| Thin/thick, forward/back approximation comparison | `white_preview_reference_tests` | [24-case CPU report](evidence/preview-approx/report.md), including 12 held-out rectangular boxes, fixed model constants and four seeds. |
| Editing, Undo, persistence and update work | `capture-window.ps1`, `capture-gate.ps1`, `benchmark-editor.ps1` | [Prior nine-track benchmark](evidence/benchmark/README.md) and [Phase 0 gate](phase-0-gate.md). The integrated build must repeat these checks. |

The fixtures are currently split among these focused suites. They are not yet a
single accepted all-renderer image matrix: heterogeneous MC/preview image error,
GPU approximation cost, and final physical captures remain pending. This is an
explicit gate limitation, not evidence inferred from central-ray box tests.

## Error categories and measured results

**Transport and floating-point error.** Analytic optical tests and HG
normalization/sampling have independent contracts. The single-scattering
reference covers 24 analytic seed/case combinations at 8,192 samples; its
largest measured discrepancy is 1.735 estimated standard errors. Deterministic
step refinement is tested against the analytic solution separately.

**Bake/interpolation and cache approximation.** Every sun-cache voxel is checked
against an independent CPU reconstruction. The recorded worst T difference is
0.000924357. Relative to direct shadow rays, view T is exactly unchanged, while
worst linear RGB differences are 0.0913365 at 32 cubed and 0.0344393 at 64 cubed
in the thick fixture. Sun caching remains opt-in. These errors are not MC variance.

**Conservative acceleration.** All 4,096 GPU leaf maxima match the CPU hierarchy
exactly. Skipping OFF/ON gives identical RGB/T for every pixel of all five tested
fixtures. Empty bricks are 4,096 for vacuum, zero for homogeneous density, 3,366
for the cut scene and 3,306 for low-sun/thick scenes. No sampled majorant violation
was accepted; deliberate underestimates are rejected. This describes the tested
fixed-grid model and does not prove a bound for arbitrary future field operators.

**Sampling uncertainty.** Delta tracking's homogeneous survival is compared with
`exp(-2.4)` using four seeds and increasing sample counts. Ratio tracking means
for homogeneous, piecewise and oblique media are within 1.165, 0.847 and 0.898
estimated standard errors respectively of their analytic/refined references.
The oblique quadrature discrepancy is reported separately. Tests use predefined
six-standard-error gates, while reported 95% intervals are descriptive; individual
seed confidence intervals need not always contain the analytic value. Roulette
weighting, chunk reproducibility and partial-result status have dedicated tests.

**Approximation/model error.** The full comparison uses 6,291,456 completed paths.
OFF versus ON RMSE is 0.0394770 versus 0.0317715 over the 24 specified central
rays; the held-out values are 0.0293000 versus 0.0245096. The six-SE diagnostic
resolves 16 improvements, one worsening and seven unresolved orderings. One
held-out thick case increases absolute error from 0.000262735 to 0.000421715.
Another retains a 0.0722773 deficit, about 280 estimated standard errors. Maximum
approximation quadrature change is below 5e-8. The remaining failures are therefore
not explained by that numerical refinement or measured MC noise. Constants were
not retuned after seeing the results. Keep the control explicitly approximate,
opt-in and OFF in baseline comparisons; exposure must not hide discrepancies.

Progressive pixel accumulation reduces pixel-sampling noise. It does not remove
view/shadow quadrature bias, density bake error or lighting-model error. Likewise,
a small MC standard error cannot validate the procedural-to-grid approximation.

## Acceleration, memory and latency

| Mechanism | Benefit and measured work | Cost / condition that can worsen |
| --- | --- | --- |
| Sun tau cache | Reuses shadow integration for occupied view samples; 32 cubed = 128 KiB, 64 cubed = 1 MiB. | Synchronous generation sits inside the consuming frame fence; frequent edits can hurt latency. Old/new replacement reserves up to 2 MiB. Thick/edge/low-sun error requires comparison. |
| Empty-brick skipping | Preserves the original global midpoint lattice while bypassing exactly empty bricks. A 128-cubed density has 16 KiB of leaf maxima. | A full homogeneous medium has no empty bricks; traversal/build overhead still exists. Nonzero approximate thresholds are not enabled. |
| Local tracking majorants | Sparse-slab test records 333,202 local versus 837,814 global candidates. | Lower candidate count is CPU algorithm work, not a measured GPU speedup; dense/loose bounds and interval overhead can reduce the benefit. |
| Progressive accumulation | Repeatable linear mean with a finite sample budget. | Two extra RGBA32F targets cost 460,800 bytes at 160x90; full-quality work can still make one frame expensive. More samples do not fix systematic bias. |
| Density cache / worker | Camera/exposure changes do not rebake; new shape renders directly until the matching cache is ready. | The prior 256-cubed edit workload records requested peak GPU buffers of 203,954,592 bytes. Requested allocation is not actual residency. |

The prior hosted benchmark has 60 updates per track at 160x90 and 64/8 integration
steps. Exposure has zero HDR updates and zero bakes; camera has 60 HDR updates
and zero bakes. The density/256-cubed track records 6.37846 completed updates/s
and edit-to-fence p95 159.3323 ms on WARP. These numbers characterize that run,
not the RTX target, hardware GPU timestamps, scan-out latency, or the current
integration commit. Per-frame CSVs and separate CPU-record, submit and fence
measurements remain available. Physical 640x360 performance, driver, residency
and long-session resource lifetime remain unverified.

## Execution tiers and diagnosis

1. **Bounded CPU CI:** compile and run CTest contracts, including analytic optics,
   phase, majorants, tracking, reference sanity, schema/Undo and Field Graph
   conversion. Use the fixed predefined seed budgets. Retain test output on
   failure. A statistical failure must be diagnosed, not retried until green.
2. **Hosted Windows runtime:** run actual editor smoke/gate images, EXR round trips,
   cache/skip/progressive/diagnostic comparisons and 60-update edit tracks. Record
   the tested checkout commit and adapter. Compare raw EXRs and inspect the
   attached screenshots. WARP establishes this backend's functional behavior.
3. **Explicit reference study:** run `white_preview_reference_tests 65536` and the
   independent reference-image seed/checkpoint script. Preserve CSV, all seeds,
   variance, completion flags and protocol hashes. These heavier studies remain
   explicit runs; do not silently multiply the lightweight CI sampling budget.
4. **Deferred physical review:** repeat Release captures and at least 300 updates
   per track on the target machine, with 640x360 internal rendering, actual GPU
   residency and timing evidence. The original performance requirements remain
   recorded in the Phase 0 report.

Diagnostic modes separate local density, optical depth, sun T, source radiance,
ray support, majorant/occupancy, density evaluation counts, skipped intervals and
invalid density/bounds. Producer and rendered revisions make stale inputs
inspectable. A wrong silhouette in density/occupancy is a field problem; correct
density with wrong sun T suggests shadow integration/cache; correct transport
values with wrong displayed intensity suggests display/metadata. This is a
practical debugging sequence, not a substitute for a failing numerical test.

## Decision and follow-up

- **Continue implementation:** explicitly authorized by the user while physical
  checks are deferred. Issue #27's typed graph preserves the old density algorithm
  and uniform bytes; it does not alter this acceptance status.
- **Retain baseline:** physical single scattering with approximation OFF. Sun
  caching and progressive sampling remain explicit choices with their own error
  and reset contracts.
- **Hold acceptance:** attach the combined commit's Actions results, inspect its
  new diagnostic/reference/approximation screenshots, finish the same-grid image
  matrix and record physical memory/latency before issuing a physical Go.
- **Revise if reference contracts fail:** majorant underestimates, incomplete paths
  represented as complete, or unexplained analytic discrepancies require fixes.
  User authorization to continue phases does not convert incorrect references
  into valid evidence.

Production Final jobs, denoising, atmosphere/terrain, animation, general GPU
support and external DCC acceptance are outside this gate's completed scope.
