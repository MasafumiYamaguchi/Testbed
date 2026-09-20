# ADR 0022: opt-in preview multiple-scattering candidate

Issue #25. Physical GPU evaluation is deferred by the user's instruction. This
candidate defaults **OFF**. It is an appearance approximation, not an unbiased
estimator, a physical reference, or an energy-conservation guarantee.

## Candidates and decision

Hillaire's SIGGRAPH 2020 cloud presentation describes approximating multiple
scattering through several single-scattering octaves and compares the appearance
with path tracing (PDF pages 27–29, counting from one). It also discusses Beer-Powder/density-dependent
edge darkening and explicitly distinguishes those controls from multiple
scattering. We select the octave family because the existing sun optical-depth
field can be reused; we retain the physical single-scattering term unchanged.
Beer-Powder would change the direct response and is not selected as an added
scattering model. This implementation is an original, restricted variant, not a
verbatim reproduction of Hillaire's or Wrenninge's implementation.

Sources:

- [Hillaire, SIGGRAPH 2020 course slides, cloud discussion PDF pages 27–29](https://blog.selfshadow.com/publications/s2020-shading-course/hillaire/s2020_pbs_hillaire_slides.pdf).
- [Schneider and Vos, The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn, SIGGRAPH 2015](https://www.guerrilla-games.com/read/the-real-time-volumetric-cloudscapes-of-horizon-zero-dawn) documents a real-time cloud lighting approach with Beer/Powder appearance controls.

## Exact implemented model

Let `tau` be nonnegative optical depth from a sample toward the sun, `a` the
physical albedo, `g` the physical HG parameter, `mu` the cosine between photon
propagation directions, and `E` a linear RGB irradiance component. The existing
single-scattering source function (radiance per extinction) is

`S1 = E a HG(mu,g) exp(-tau)`.

The candidate adds exactly two terms:

`Sextra = E (1-exp(-tau)) sum[n=1..2] a^(n+1) strength^n HG(mu,g*0.5^n) exp(-tau*0.5^n)`.

`S = S1 + Sextra` is integrated using the **unchanged physical view-ray
extinction**. The optical-depth gate is an explicit project heuristic. It makes
the added source first order in vanishing medium optical depth, and its integrated
radiance second order when both view and sun optical depths shrink together. It
is not claimed to estimate the actual probability of a second scattering event.

The fixed attenuation/anisotropy scales are 0.5; `strength` is in [0,1], default
0.5. These constants were fixed before evaluating the Monte Carlo comparisons.
CPU evaluation uses `expm1` for the small-depth gate; HLSL uses its cubic expansion
below 1e-3. Only two extra exponential/HG evaluations and one gate are required
per occupied view sample. No new shadow traversal, density fetch, history
buffer, or cache is required. GPU timing still needs measurement; counting
operations is not a timing claim.

Zero sun or zero albedo gives exactly zero source. Vacuum still integrates to
zero radiance. Infinite sun optical depth gives zero. OFF and zero strength
retain the single-scattering formula. The primary background remains separate
from this source and does not illuminate the medium.

## Scope and known failures

Only one directional sun and spatially uniform achromatic albedo/HG are covered.
The model has no lateral transport or geometry-dependent escape probability.
Two locations with identical sun optical depth receive the same added source,
even if one is beside a thin edge. Deep shadows eventually become dark, although
true multiple scattering can enter from other directions. Strong anisotropy,
near-unit albedo, sharp boundaries and low-angle lighting are particularly
uncertain. The approximation can overshoot or undershoot reference radiance and
may worsen an image. It cannot recreate Mie halos or atmosphere coupling.

## Settings, invalidation and integration contract

`PreviewApproxSettings { enabled=false, strength=0.5 }` is separate from `Optics`.
UI must label it “Preview multiple scattering (approximation)” and keep an OFF
comparison. Saving a new version must place this in a separate preview settings
object and migrate older scenes to OFF. Changing it resets rendered HDR and
progressive accumulation; it does not alter density, physical optics or the sun
optical-depth cache. Export metadata must include the enabled flag and strength.

`preview_approx_source` receives **optical depth**, not transmittance. Preserve
the sun-cache tau or the direct shadow-march tau through the shader. Converting
an already-underflowed transmittance with `-log(T)` destroys information for the
extra octaves and must not be used. The shader helper requires `phase.hlsli` to
be included first. Evaluate with sun irradiance 1 and multiply the resulting
scalar by RGB irradiance to avoid repeating the two-octave arithmetic per color.

## Validation and adoption

`preview_approx_tests` checks 1,250 parameter combinations in both OFF and ON modes, OFF, zero-source
limits, thin-medium second-order scaling, shadow decay, exact linear sun
scaling and invalid inputs.

`preview_reference_tests` predeclares 24 central-ray comparisons using the same
frozen R32F constant-density grid as Issue #24. Twelve box cases use optical
depths 0.2/5, albedo 0.2/0.8/0.98, HG g=0.7 and opposite sun directions. Twelve
held-out rectangular-box cases use depths 0.35/7, albedo 0.35/0.9/0.995, g=0.3,
and the same two sun directions. No fitting takes place. All model parameters
remain identical. Each row records the analytic single-scattering baseline,
2048-step approximation, four-seed MC mean and standard error, each absolute
error, approximation quadrature delta, seed spread, individual seed means and
standard errors, and elapsed CPU time. A
1024/2048-step comparison separates quadrature error from MC noise. The MC path
budget is 256 bounces; incomplete paths fail the report instead of being silently
counted as dark samples.

The report also compares the MC mean with the midpoint of the two deterministic
models. Since ON is never below OFF, ON is closer precisely when the true mean
is above that midpoint. A descriptive band of plus/minus six estimated standard
errors distinguishes a resolved change from an unresolved, noise-level ordering.
This is not a confidence guarantee, and the point-estimate RMSE across the 24
cases is not a scene-distribution accuracy claim.

### Recorded decision

The [CPU report](../evidence/preview-approx/report.md) covers 6,291,456 paths:
65,536 samples for each of four seeds in each of 24 cases. All paths completed.
Across these cases the single-scattering linear-HDR RMSE is 0.0394770 and the
candidate RMSE is 0.0317715. On the 12 held-out cases they are 0.0293000 and
0.0245096. There are 23 point-estimate improvements, but the six-SE diagnostic
resolves only 16 improvements and one worsening; seven orderings are unresolved.

The held-out thick rectangular box at tau=7, albedo=0.35, g=0.3, mu=+1 is a
resolved worsening: single-scattering absolute error 0.000262735 increases to
0.000421715. At tau=7, albedo=0.995, g=0.3, mu=-1, the candidate still misses the
MC mean by 0.0722773, approximately 280 estimated MC standard errors. This is
model error, not explained by the measured sampling noise. The largest measured
1024/2048-step quadrature change is below 5e-8, much smaller than these failures.

**Decision: retain the candidate as an explicitly labelled opt-in preview
control; reject default adoption.** The consistent thick-medium deficits, one
resolved worsening, and missing GPU image/cost review do not support replacing
single scattering by default. The constants have not been fitted or adjusted
after inspecting these results. Physical GPU timing and image review remain
separate tasks; these CPU central-ray measurements do not satisfy those gates.
Improvement is not a test assertion: worsening cases remain in the report.

The default comparison executable uses 4,096 samples per seed (393,216 total
paths) for quick CI evidence; passing `65536` reproduces the recorded report.
Runtime depends on the CPU and medium: the recorded full run took about 22
seconds of sampling on the Linux host. This is neither GPU cost nor editor
latency. Sample count changes uncertainty only; the candidate is unchanged.
