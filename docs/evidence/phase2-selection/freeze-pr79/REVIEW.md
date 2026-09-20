# PR #79: selected field, Freeze and native lifecycle evidence

Reviewed 2026-09-20 UTC. The Freeze comparison and early evidence upload steps
passed in [Native run 35535097045](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35535097045),
[Release job 106142588957](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35535097045/job/106142588957).
The complete Release job was still running when this checkpoint was reviewed.
These results do not claim that the later regression steps have completed.

## Exact source and retained data

- PR head: `99b585adb1bf84b8f6f27b818895570e5a54f1e0`.
- Tested merge, recorded in all 11 HDR metadata files:
  `5cdcad3bb1de6d0c8be23c4e7a2d3071a3c33cdf`.
  GitHub describes it as the merge of that head into
  `ff6ed399700f8925a0929bdec3f0a6f9291f1881`.
- Artifact: `10612767943`, `windows-Release-selected-state-evidence`,
  2,164,112 bytes. Downloaded ZIP SHA-256 was checked against GitHub's digest:
  `bbc6fbd0b4c860724c3e88168e17724b3c6cf4c9396df1ab55d4973d0b5ca376`.
- Backend: Windows D3D12, Microsoft Basic Render Driver, SDL 3002028,
  validation layers enabled, as recorded in the original logs.
- This evidence uses saved Frozen `contract_version = 1` / schema 10. It does
  not validate the later contract-v2 envelope changes, schema-11 finishing
  modifiers, preset workflow, or any newer PR head.

The 89 retained original artifact files comprise the Freeze PNGs, recipes,
stdout/stderr and contract logs, comparison reports, and all 11 HDR exports
(EXR, display PPM and metadata). Their bytes were copied without rewriting.
The workflow-produced PNGs are retained; duplicate BMP encodings and the
already separately reviewed growth directory are omitted. The three lifecycle
BMPs existed in the verified archive, each 2,160,138 bytes. `provenance.json`
records the API artifact/job snapshots and tested merge. `verification.json`
records the independent checks. `SHA256SUMS` covers every retained file.

## Numeric and lifecycle findings

The original `contract.log` reports exact CPU selected/Freeze density equality
for all four top/anvil enable combinations and the history-free lifecycle
contract. On the native GPU, all calm/wind and front/side selected/frozen EXR
pairs are byte-identical. The local `white_hdr_compare --strict` independently
reported `linear_RGB_max=0 mean=0 T_max=0` for each pair.

| View | EXR byte identity | RGB maximum difference | T maximum difference |
|---|---|---:|---:|
| calm-front | exact | 0 | 0 |
| calm-side | exact | 0 | 0 |
| wind-front | exact | 0 | 0 |
| wind-side | exact | 0 | 0 |

All 11 fixed exports are 192 x 108, 80 view steps and 8 shadow steps. Metadata
reports Direct density, sun cache resolution 0, and empty-space skipping false,
including all three reopens that requested density and sun caches. The largest
reported full-image CPU/GPU transmittance error is `4.24602e-06`, below the
application's Direct tolerance of `0.001`.

The actual `input.stdout.log` and lifecycle screenshots establish:

- Adoption: completed candidate, one Undo/Redo transaction and 1 generation job.
- Detail/camera/light editing: stable structural content hash, one Undo and
  0 additional generation jobs. The stage-0.800 screenshots at frames 100 and
  140 display the same structure and generation count 1.
- Cancellation/retry: fixed cloud retained and retry started; the test checks
  that the cancelled attempt increments the generation counter.
- Explicit replacement: one Undo retains the prior successful fixed state;
  Save/Open preserves it with 0 reload generation jobs. Frame 200 displays
  stage 0.450, the new structure, generation count 3 and Saved status.

Fresh processes reopen `freeze-first`, `freeze-detail` and `freeze-smoke` with
`frozen_reload_generation_jobs=0`. Each exported metadata scene is exactly equal
to its saved JSON scene. First/detail share content hash `8170491388461520691`
while their payload hashes differ; the replacement content hash is
`877277292391849608`.

## Direct image review

The reviewer opened the original `freeze-100.png`, `freeze-140.png`,
`freeze-200.png`, and the four calm/wind front/side frozen PNGs. An independent
review also opened the three lifecycle images and four corresponding HDR PPMs.
The selected versions are backed by the exact EXR identity above.

Frame 100 shows the adopted leaning column, upper lobe and rightward anvil
inside the viewport. The stage, structure and generation counter are readable.
The four fixed views show the complete cloud within the viewport; no new
viewport truncation or geometry discontinuity attributable to Freeze is visible.
Wind changes the front silhouette, while the side projection compresses that
horizontal displacement. Blue dotted primitive overlays remain visible, and
the low internal render resolution produces coarse silhouette pixels.

Frames 140 and 200 are very dark and mostly reveal the wire overlays. This is
consistent with the explicit test inputs: irradiance changes from `[15,15,15]`
to `[0.9,1.0,1.1]`, exposure from 1.0 to 0.4, and detail-frame albedo to 0.85.
Exposure and green-channel illumination alone reduce brightness by about
22.7 times. They are not evidence that Freeze erased the field: the readbacks,
transmittance and saved/reopened states all pass. Their darkness also prevents
a useful visual judgment of the changed detail. The lower inspector controls
continue below the scrollable pane; visibility of every control was not tested.

The well-lit views remain strongly primitive-shaped: a rounded, pinched column,
small upper bump and thin disk-like anvil. They are suitable for checking
preservation and projection; they do not establish natural cloud appearance.

## Acceptance limits

This is native Windows application execution with GPU rendering and readback.
`freeze_test_step` injects lifecycle commands through the same generation,
adoption and EditorSession paths used by the UI. It does not drive a human's
mouse/keyboard sequence through every widget. The screenshots show the native
UI state; they do not establish human interaction or usability acceptance.

**Naturalness remains Hold. Physical RTX validation remains Hold.** Additional
lighting, close-up/high-resolution appearance review, direct human interaction,
and physical-GPU checks remain separate. Later finishing/preset features need
their own exact-head evidence.
