# Editing/settled progressive preview (Issue 20)

Opt-in `--progressive 64 --frames 200 --capture image.bmp --export-hdr NEW_DIR`.
The fixed renderer remains the default and benchmark/self-test reference.
Editing uses at most 96 internal pixels, 32 view steps and 4 shadow steps.
Settled mode restores configured quality (default 160 pixels, 64/8 steps),
then integrates one new sample per UI frame up to a 1..256 budget. A 250 ms
quiet interval after the last input/key change prevents boundary oscillation.
Pause and sample count are visible in the Inspector. No multi-frame batch
blocks the UI; one configured-quality frame can still be expensive on WARP.

Each pixel samples a reproducible jitter inside its footprint using the shared
counter RNG, seed 42, sample index and separate X/Y dimensions. Ray-march steps
retain their midpoint lattice. This improves pixel sampling, not the systematic
ray-march or shadow integration bias. Increasing accumulation samples is not
claimed to fix discretization error. No reprojection or moving-image history
is reused.

Two RGBA32F ping-pong targets average linear composited RGB and transmittance T
with mean += (sample-mean)/(n+1). The first sample does not read undefined old
storage. Tone mapping/exposure only reads the current mean. CPU known-value
averaging checks preserve HDR >1 and T. Resource budgeting reserves two extra
HDR targets; logs record their exact allocation bytes and each sample revision.

The reset key contains full Scene excluding exposure, window/internal size,
view/shadow quality, density mode/resolution, actual current-cache availability,
sun-cache mode, skipping and sample budget. Input changes reset sample count
before rendering the new shape. Density publication changing direct fallback
to cached also resets. Drag/restart enters Editing and clears history. Exposure
changes do not reset or reintegrate. There is no accumulation across revisions
with differing transport inputs.

EXR exports the displayed mean and records actual sample count, jitter seed,
quality and full Scene. Center-ray CPU transmittance validation remains active
for the fixed path; it is explicitly inapplicable to a jittered pixel average.
Finite/range checks still apply. Windows CI captures fixed, 8, 64 and repeated
64 samples, checks repeated EXR equality within 2e-5 RGB/1e-6 T and reports the
fixed-vs-averaged difference without demanding equality. These are preview
comparisons, not MC radiometric-reference validation. Physical review deferred.
