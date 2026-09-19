# CPU tracking and reference contracts

Captured from the Release CPU targets on 2026-09-19 with GCC 13.3. These logs are
numerical CPU evidence; Windows Actions and image artifacts are reported
separately. Each executable completed successfully. The focused CTest run passed
all three contracts in 1.40 seconds on this runner.

| Contract | Result |
| --- | --- |
| Delta, homogeneous 4 m at sigma 0.6/m | Analytic survival 0.09071795329; final 32768-sample estimates 0.0883484–0.0925598 across both modes and four seeds |
| Delta, sparse trilinear slab | Analytic survival 0.2008895554; final estimates 0.196716–0.206390; 837814 global vs 333202 local candidates |
| Ratio, homogeneous | Analytic T 0.01831563889; 16384-sample means 0.0178225–0.0184494; maximum 1.165 measured standard errors from the analytic value |
| Ratio, piecewise voxel slab | Analytic T 0.2865047969; means 0.283963–0.288142; maximum 0.847 standard errors |
| Ratio, oblique trilinear medium | Refined midpoint T 0.2610773201; means 0.258656–0.262294; maximum 0.898 standard errors; quadrature discrepancy recorded separately |
| Single-scattering reference | 24 analytic cases: two sun directions × three g values × four seeds, 8192 samples each; maximum 1.735 standard errors |
| Roulette weighting | Four independent 16384-sample comparisons of early versus delayed roulette; largest mean difference 0.001230 with combined standard error 0.000906 |

The reference tests additionally verify primary clipping, untruncated sun and
secondary transport, exact chunking reproducibility, incomplete checkpoint
metadata, empty/absorbing media, unit albedo, and explicitly invalid event,
bounce and numerical limits. Tracking tests reject underestimated bounds rather
than silently clamping acceptance, and the ratio thick-medium tests retain
finite log T after its exponential underflows.

Commands:

```sh
cmake --build --preset cpu-release --parallel 2 --target white_delta_tracking_tests white_ratio_tracking_tests white_reference_tests
ctest --preset cpu-release -R '^(delta_tracking_contract|ratio_tracking_contract|reference_contract)$'
```

`delta.log`, `ratio.log`, and `reference.log` contain the per-seed sample counts,
estimates, event work, and uncertainties. Six-standard-error gates are used for
the deterministic test seed set; reported 95% intervals are diagnostics and are
not required to contain every analytic value.
