# Same-grid heterogeneous single-scattering comparison

This diagnostic closes the CPU image-comparison portion of Issue #26. It compares
Monte Carlo single scattering with deterministic ray marching through the exact
same frozen heterogeneous voxel array. It does not claim native GPU equality or
physical-device acceptance.

```
white_reference_image_tests tests/fixtures evidence/reference/image-matrix
```

The optional positional settings are samples per seed, width, height and grid
resolution. Defaults are 8192, 16, 9 and 32. Four saved recipes are used: fusion,
cut, detail seed 17 and low sun. Each recipe is baked once into an owning R32F
snapshot, then shared unchanged by all estimators. The exact little-endian voxel
bytes, their FNV-1a checksum, the full saved scene and local grid bounds accompany
the outputs. The snapshot includes the same trilinear interpolation and hard
base/cut/envelope mask used by the fixed-density reference.

Every estimator traces the same pixel-center ray at `((x+.5)/width,
(y+.5)/height)` with aspect `width/height`. There is no pixel jitter, filtering,
exposure feedback, sun cache or preview multiple-scattering approximation.
Camera near/far clipping applies only to the primary segment. Sun visibility
extends to the medium boundary.

The deterministic reference is evaluated twice: 512 view / 64 shadow steps and
1024 view / 128 shadow steps. Their difference is reported independently as a
quadrature-refinement diagnostic. This difference is not a rigorous upper bound
on integration error. The Monte Carlo estimator uses independent seeds 17, 42,
99991 and 4294967313, with statistics at 1024 and 8192 samples per seed. Every
attempted path must complete; event, bounce or numerical limits fail the run
instead of silently contributing a truncated reference value.

Each seed produces a linear EXR, display PPM, metadata and per-pixel CSV. CSVs
retain RGB and T means, variance of each estimated mean, and both deterministic
references. The RGB-average variance is measured directly from each sample so
cross-channel covariance is retained. Four-seed means and variances combine with
equal weights; separate per-seed rows remain available for diagnosis.

Acceptance uses image-level engineering uncertainty envelopes:

- Absolute image-mean bias ≤ 6 aggregate standard errors + mean absolute
  quadrature discrepancy + 0.00002.
- Image RMSE ≤ 3 expected Monte Carlo RMSE + quadrature RMSE + 0.0002.

Both apply to RGB average and T. Per-pixel errors and variances are diagnostics;
there is no independent six-SE assertion for every pixel. The envelopes are not
simultaneous confidence guarantees. No requirement forces every particular
random estimate to improve monotonically as sample count grows.

The default run contains 18,874,368 paths, 80 comparison rows and four cases.
The executable limits the total budget to 67,108,864 paths. An optional future
64×36, 128³ run can match the native preview's available image/grid dimensions
with a lower sample count. Native comparison must additionally use the same
camera/quality settings and account for CPU/GPU bake roundoff, or load the exact
saved voxel array. Agreement here measures MC/quadrature behavior for the frozen
medium; it does not measure the procedural-to-grid bake error.
