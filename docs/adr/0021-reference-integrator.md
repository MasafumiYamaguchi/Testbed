# Small frozen-volume reference integrator — Issue #24

Status: CPU implementation; physical-device checks are deferred by the user's instruction. This is a small image diagnostic renderer, not a product Final renderer.

`white_reference` owns an immutable copy of a 2..128³ R32F density grid, scene and conservative majorant. The reconstructed medium matches the cached preview: trilinear interpolation inside the saved grid, followed by the original hard base, cut and envelope constraints. Procedural noise is baked; preview sun caches, empty-space approximations and multiple-scattering approximations are never called.

## Transport contract

Camera rays march backward through the medium. Delta tracking samples real **extinction** collisions. At every real collision throughput is multiplied by `sigma_s / sigma_t = albedo`; absorption is accounted for by this weight, without a second absorption roulette. The sun contribution is `beta * irradiance * HG(dot(backward_ray, to_sun), g) * T_sun`. Independent ratio tracking estimates `T_sun`. HG continuation multiplies throughput by `phase/pdf`; the current matching sampler makes that factor one.

The only light is a directional sun evaluated by next-event estimation. A phase-sampled ray does not separately hit/add the delta sun. The fixed background `(0.015, 0.022, 0.035)` contributes only `T_primary * background`, using an independent ratio stream. Secondary escapes add no background, and the background does not illuminate the medium. This matches the preview's simple compositing contract and avoids double counting.

The camera's near/far planes clip only the original camera segment. Shadow and continuation rays run to the finite frozen medium's exit, even when its remaining chord exceeds the camera's far distance. Applying camera clipping to those rays would omit extinction and additional scattering while falsely reporting a completed path. Both deterministic and stochastic references preserve world-distance parameterization under affine transforms and reject non-unit rays.

After three scattering events Russian roulette uses survival `clamp(beta, .05, .95)` and divides surviving throughput by that probability. Single-scattering mode deliberately stops after one NEE event. In multiple-scattering mode, a further real collision beyond the configured bounce limit marks that path partial; a legitimate escape remains complete. Event-budget or numerical/majorant failures also mark partial results. The mean includes all attempted paths, and partial images are explicitly approximate, never promoted to complete references. CLI exit code 2 accompanies a saved partial result.

## Reproducibility and output

Seeds are 64-bit values. Independent streams are pixel jitter 0, primary ratio 10, delta 20, sun ratio 30, roulette 40 and HG 50, additionally keyed by pixel, sample and bounce. Chunking sample planes changes neither image bytes nor per-path random streams. All planes use a fixed snapshot and quality.

The application emits increasing-sample CSV rows and optionally exports intermediate snapshots. Each EXR/display/metadata bundle uses the existing no-overwrite atomic HDR export. RGB is linear Rec.709 with primary background already composited; A is camera transmittance, not opacity. Metadata includes the full scene, seed, sample count, limits, RNG contract, grid/layout hash, a 9³ fixed-probe procedural-versus-baked density error comparison, path counters, pixel estimator variance and complete/partial status. CPU execution is independent of SDL and a GPU.

An intermediate checkpoint reports an incomplete requested sample budget even if every attempted path has completed. Overall `complete` requires both the requested sample planes and all path-completion checks. Tracking-event counters cover successfully returned tracking calls; work consumed by a throwing call is not included, and this limitation is explicit in metadata.

Example:

```
white_reference --output evidence/reference/cloud --width 32 --height 18 --grid 32 --samples 128 --seed 42 --checkpoint-every 32
```

## Verification and interpretation

`reference_contract` compares single scattering with independent homogeneous-box analytic solutions for both sun directions and g = -0.6, 0, 0.6, using four fixed independent seeds and 8192 samples per case. A midpoint implementation of the same ray-march equation independently converges to the analytic values; Monte Carlo estimates must fall within measured six-standard-error bounds. Empty media, absorption only, albedo one, an internal camera, thick media, reproducible chunking and explicit event/bounce/numerical failures are covered. A shortened camera far plane leaves otherwise identical shadow and secondary estimates unchanged while primary clipping remains effective. Four preselected seeds produce 512/4096-sample multiple-scattering means, variances and standard errors. Early roulette and delayed roulette are additionally compared with independent 16384-sample sets per seed, checking survival weighting with their combined standard errors. Confidence tests do not demand monotonic improvement from any one random seed.

`capture-reference.ps1` saves seven fixed 32×18 images at 32..128 spp plus three independent 16×9, 256-spp runs. These deliberately small images expose Monte Carlo noise. No denoising, adaptive sampling, reprojection or image-quality acceptance is implied. Compare the EXRs rather than tone-mapped screenshots for numerical errors.

Error categories remain separate: the grid introduces density bake/interpolation error; the grey HG medium and background contract are optical-model choices; deterministic quadrature/float arithmetic introduce numerical error; finite paths produce MC variance. A low MC standard error does not validate the bake, the optical model, or physical-device performance.

Background reading: [PBRT volume integrators](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators). Implementation is original; no PBRT source was copied.
