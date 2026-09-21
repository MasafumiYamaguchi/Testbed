# Verified CPU reference Actions evidence

[Workflow run 35425066150](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35425066150)
completed successfully on Ubuntu 24.04 with GCC 13.3: Release build, **22/22
CTest contracts**, the PowerShell capture script, and the held-out approximation
comparison all passed.

- PR #73 head: `cabbbb5f4e004fc1dfbe84717f90f29db74cdda7`.
- Tested pull-request merge: `09395c6b3410f7be88f5d90ddcdd365929951ffb`.
- Artifact: `cpu-reference-evidence`, ID `10579088384`.
- Downloaded ZIP SHA-256: `5d5f6fe0f4520ce930b7cfb765b3c8ae729dcdb35f9d2c175a970357a74f5425`.

All 31 image/checkpoint bundles were checked after download: source commit,
64-bit seed, 32³ density layout, requested/completed sample budget and path counts
agree; no event, bounce or numerical limits were hit. Ten final bundles are
preserved here with original EXR, original PPM, metadata and a lossless PNG
conversion. All ten PNGs were opened for visual review. The full workflow
artifact additionally contains intermediate sample checkpoints.

The seven fixed cases are cloud single scattering, cloud multiple scattering,
empty, absorption only, internal camera, albedo one and thick medium. Their
images are intentionally **32×18** at 32–128 spp. Three further cloud runs use
independent seeds 17, 99991 and 4294967313 at **16×9**, 256 spp. The tiny images
and visible Monte Carlo noise are diagnostic evidence, not final image-quality
acceptance. Empty and absorbing cases behave as expected; the internal-camera
image has substantial noise at its deliberately low sample budget.

`approximation.csv` contains the independent 24-case, 6,291,456-path comparison.
All paths completed. The fixed approximation improved 23/24 point estimates;
16 improvements and one regression were resolved by the descriptive six-SE
comparison, with seven unresolved. Aggregate RMSE changed from 0.0394770 to
0.0317715. The resolved regression remains explicit evidence against treating
this preview approximation as a generally accurate reference.

`ctest.log`, `job.log`, `commit.txt` and `files-sha256.json` preserve test,
provenance and file-integrity evidence. Physical GPU review remains deferred.

## Fixed-head repeat

[Run 35425272605](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35425272605)
also passed for PR head `8e5c02b4e327df7dfa1976da876ed526362049ac`, tested as merge
`6503ea6a3885b3cdf6fbb8c135b9157c1e71efcc`. Artifact `10578034649` was downloaded
and its ZIP SHA-256 verified:
`c9fc9bf62080b62ff8e5ecb14286e1dfaa394927e2b4c575a748c119af070f63`.
All 31 EXRs are byte-identical to the first reviewed run archived above. All
24 approximation numerical rows are also identical; their measured elapsed
times differ as expected. The repeated metadata agrees with the new tested
merge, 64-bit seeds and completion status. This repeat includes the Windows
test-identifier portability fix and camera-independent sunlight fix; the CPU
reference image results remained unchanged.
