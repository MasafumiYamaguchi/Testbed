# 0013: HG scattering convention and deterministic sampling (Issue #16)

Incoming and outgoing directions describe photon propagation. Their dot product
is cosine of the deflection angle. Positive g favors continuing forward; g is
the expected cosine. The viewer ray points eye-to-scene, whereas scattered light
travels toward the eye. Sun direction points sample-to-sun, whereas incoming
photons travel from the sun. Negating both gives phase cosine dot(viewRay,toSun).
This differs from conventions that define both directions pointing away from a
scattering event; formulas must not be mixed without changing the sign.

HG evaluate and pdf are (1-g²)/(4*pi*(1+g²-2*g*cosine)^(3/2)). The denominator is
rearranged into sums around the forward/back peak to reduce cancellation.
The range is restricted to [-0.95,0.95]. g=0 is exactly isotropic. Sampling uses
an algebraically expanded inverse CDF with no division by g, so very small
nonzero g does not silently switch to a different PDF. An orthonormal frame
maps the cosine/azimuth sample around the incoming propagation direction.
The same expressions are implemented independently in CPU double and HLSL float.

A counter-based integer hash maps pixel/sample/dimension and a 64-bit seed to
24-bit uniform values in [0,1). Each dimension has its own mixed key. This is a
reproducible rendering sampler, not cryptographic randomness or a claim of a
low-discrepancy sequence. GPU diagnostics compare 104 CPU/GPU samples, PDF values
and exact random values. CPU tests numerically integrate normalization and first
moment, then compare sample means and eight-bin CDF counts for four independent
seeds and seven g values. Statistical tests use six-sigma bounds plus small
rounding allowances; they do not assert that every possible seed must pass.

Inspector exposes density, extinction per metre, albedo and g separately. g and
albedo modify optical state/HDR while preserving density cache identity. The
version-3 schema stores g; version-1/2 files migrate to g=0, preserving their prior
scattering. Density algorithm version remains 2. Old malformed optical objects
are rejected before adding the default. Camera exposure remains a display change.

Seven fixed saved scenes compare forward/back sun, isotropic counterparts,
negative g, the supported g bound and absorption-only behavior. Actual screenshots
and CPU transmittance checks are captured by Windows Release CI. Physical GPU
performance remains deferred, and no detailed Mie or spectral model is claimed.

Mathematical reference: [PBRT phase functions](https://pbr-book.org/4ed/Volume_Scattering/Phase_Functions).
No implementation source is copied; the propagation-vector convention is explicit
so comparison with outward-vector references does not invert the physical lobe.
