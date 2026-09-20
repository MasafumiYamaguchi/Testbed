# 0012: Optical units and segment integration (Issue #15)

World distances are metres. Camera rays have normalized world direction; the
inverse cloud transform changes the local direction without renormalizing it,
so their parameter remains world distance. Nonnegative dimensionless density rho
and extinctionScale (m^-1) give sigma_t = rho * extinctionScale. Albedo in [0,1]
gives sigma_s = albedo * sigma_t and sigma_a = sigma_t - sigma_s. Invalid inputs,
non-finite values, negative lengths and unrepresentable products are diagnosed.

RGB transport is linear radiance; exposure and display transforms are absent
from these tests. A camera ray points from eye toward the scene. Incoming light
propagates opposite the direction-to-sun vector; light scattered toward the eye
propagates opposite the camera ray. Phase-function sign convention is specified
in Issue #16. In the current isotropic direct-sun model the source function is
S = albedo * phase * incidentRadiance * lightTransmittance, in radiance units.
The per-world-metre source is j = sigma_t * S. A constant-source fixture may
supply j independently to test the mathematical vacuum limit, not to add cloud
emission to the product.

For a segment of length d and optical thickness tau = sigma_t*d:
T = exp(-tau), L = j * (1-exp(-tau))/sigma_t. At sigma_t=0, L=j*d.
CPU uses expm1 for the small-opacity numerator; GPU uses a cubic opacity series
below tau=1e-3. Source-function integration instead uses S*(1-exp(-tau)).
T multiplies exp(-tau) directly rather than reconstructing it from 1-opacity,
which would round thick-segment transmission prematurely to zero.
Front-to-back composition is (T_a*T_b, L_a+T_a*L_b). T commutes, radiance generally
does not. Splitting a homogeneous interval preserves both within rounding error.
Scaling world length by k and sigma_t and j by 1/k preserves both quantities.

CPU fixtures cover vacuum, homogeneous and absorption-only slabs, tiny optical
thickness/source, ordered piecewise-constant segments, fixed source, partitioning
and world-scale compensation. A quadratic extinction profile has an independently
integrated optical thickness; 16/32/64/128 midpoint steps should reduce error by
about four per doubling. CSV-like tables are emitted in CTest logs.

CPU normal values use abs 1e-13 + relative 2e-12. Tiny emitted radiance uses
abs 1e-24 + relative 2e-11 (the additional fixed tiny-source check uses abs 1e-27).
Transmission near zero is judged absolutely, not by meaningless division by an
underflowed value. GPU segment T uses abs 2e-5; L uses abs 2e-5 + relative 2e-5,
except the near-vacuum source fixture uses abs 1e-14 + relative 2e-5. This prevents
a zero answer passing just because its expected value is small. GPU midpoint
convergence allows FP32 accumulation noise (ratio .35 plus abs 2e-6).
The earlier homogeneous GPU suite retains its separately declared abs 1e-3 bound.

The same HLSL segment routines are used by rendering and GPU tests. Windows CI
executes these on hosted WARP; CPU-only tests need no GPU. Physical RTX testing
remains deferred under the user's explicit authorization to advance phases.
No exposure adjustment or density retuning changes a failing fixture.

Mathematical background: [PBRT transmittance](https://pbr-book.org/4ed/Volume_Scattering/Transmittance)
and [equation of transfer](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/The_Equation_of_Transfer).
The implementation and fixtures are original; no source implementation is copied.
