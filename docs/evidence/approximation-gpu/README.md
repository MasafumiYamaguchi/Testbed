# Verified Windows approximation and short-far sunlight

[Native run 35425272616](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35425272616)
passed Linux Debug/Release and Windows Debug/Release for PR #73 head
`8e5c02b4e327df7dfa1976da876ed526362049ac`. The tested merge is
`6503ea6a3885b3cdf6fbb8c135b9157c1e71efcc`. Release artifact `10579240206` was
downloaded and its ZIP SHA-256 checked:
`d8bbabe5c93c7c954cf15859f66ca4f308b431ea2a7fa993209e029c7dc386c3`.

The actual SDL adapter is **Microsoft Basic Render Driver**, driver
10.0.20348.5386, D3D12 with validation enabled. This is hosted WARP evidence;
the separate OS adapter inventory reports Microsoft Hyper-V Video. No physical
RTX performance conclusion follows from these results.

All seven new screenshots were opened: approximation OFF/ON, sunlight cache
OFF/32³/64³ with a one-metre camera far plane, and short-far empty skipping
OFF/ON. Original linear EXRs, display PPMs, screenshot PNGs, complete metadata
and their runtime logs are preserved. Every bundle reports the tested merge,
128³ density, 160×90 image, 64 view / 8 shadow steps and a single fixed sample.

| Comparison | Linear RGB maximum difference | Primary T maximum difference |
| --- | ---: | ---: |
| Approximation OFF → ON, strength 0.5 | 0.146986 | 0 |
| Short-far sun rays → 32³ sun cache | 0.0000465605 | 0 |
| Short-far sun rays → 64³ sun cache | 0.0000135973 | 0 |
| Short-far empty skipping OFF → ON | 0 | 0 |

Approximation ON visibly brightens the cloud while preserving the silhouette
and T. The screenshot proves functional application of the control, not the
accuracy of its multiple-scattering model; the independent CPU comparison
still rejects default adoption. Metadata confirms the saved OFF/ON state and
strength. The first captured-work frame logs 1150.53/1132.38 ms submission-to-fence
for OFF/ON on this runner. These single cold frames with validation enabled do
not establish a stable approximation performance cost.

The short-far camera is inside a homogeneous volume and integrates only 0.9 m
from near=0.1 to far=1.0. The almost uniform displayed region is expected, not a
missing-cloud failure. Sun visibility must nevertheless reach the distant box
boundary. Independent homogeneous-box integration of that full light path
agrees with the direct-shadow EXR to maximum RGB error **1.49240e-7**. Analytic
T is `exp(-0.02*0.9)=0.982161032358`; the GPU's 64-step float result differs by
3.98080e-6, also reported by its existing CPU-T validation. The optical-depth
cache's 32³/64³ voxel checks have maximum T errors 4.38631e-8/4.84264e-8; image
interpolation error is recorded separately in the table.

The independent short-far equation uses the active box exit distance
`d_sun(t)=a+b*t`. For each pixel, the same face is active over its primary
segment. Scattered radiance is
`E * albedo * HG * exp(-sigma*(a+b*near)) *
[-expm1(-sigma*(1+b)*(far-near))]/(1+b)`, plus the fixed background times T.
This checks the removed camera-far shadow cutoff independently of the cache.

Latest regression logs were also checked: all six empty-skip fixtures match
RGB/T exactly, the repeated 64-sample progressive images match exactly, all sun
cache comparisons preserve primary T, and the six analytic diagnostic images
pass. All nine benchmark tracks contain 60 updates; camera/exposure perform no
density bakes and exposure performs no HDR updates. The current density/256³
track records 4.91639 updates/s and edit-to-fence p95 207.8522 ms on this WARP
runner; physical performance remains deferred. Summary logs accompany the new
images; the complete workflow artifact contains the other regression captures.
