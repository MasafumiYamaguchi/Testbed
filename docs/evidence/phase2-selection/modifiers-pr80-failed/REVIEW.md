# PR #80: failed modifier lifecycle and passed Freeze v2 checkpoint

Reviewed 2026-09-20 UTC. [Native run 35536694061](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35536694061)
failed in [Release job 106146891429](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35536694061/job/106146891429),
step **Frozen finishing layers and native lifecycle comparisons**. The preceding
Freeze step passed. CPU Debug/Release and Windows Debug passed; later Release
comparison suites were skipped. This record preserves the failed checkpoint,
not a completed modifier suite or validation of a newer head.

## Provenance

- PR head: `57af3d530c4cdfe71f65ffac82fccec27597b6e7`.
- Tested merge: `d485e2fbd795e1e96d28718b26c64966f8b5290e`, merging that head into
  `99b585adb1bf84b8f6f27b818895570e5a54f1e0`. All 11 Freeze HDR metadata files
  name this merge; its tree is `426599fbfb34d4ce48f236695c05ace4516a87f9`.
- Early artifact: `10613770136`, `windows-Release-selected-state-evidence`,
  2,305,858 bytes. Download SHA-256 matched GitHub's artifact digest:
  `4902ed57e9c1b80d1b4fd1514f538458d4fbdd10807c1336fbd94b8d7f71762c`.
- Windows D3D12, Microsoft Basic Render Driver, driver `10.0.20348.5386`,
  SDL `3002028`, validation layers enabled.

`provenance.json` retains the artifact/job snapshots and tested merge.
`verification.json` records original-file and screenshot hashes plus the
Freeze comparisons. `SHA256SUMS` covers every retained file except itself.
The 53 original artifact files are copied byte-for-byte, including CRLF logs
and JSON. The two BMPs are represented by lossless PNGs: decoded 900 x 600 RGBA
pixels were verified identical, with source BMP and decoded-pixel hashes saved.

## What passed before failure

The native command path added the cut with Undo/Redo, then changed detail and
added density/protection layers. Its logs report both intermediate operations
as PASS, with Direct density, no density/sun cache and no majorant skipping.

| Capture | Layers / boundary probes | Hard interiors zero | Field density error / tolerance | HDR CPU T maximum error |
|---|---:|---|---:|---:|
| Frame 55 | 1 / 12 | true | `6.79493e-06 / 0.000940126` | `2.21663e-06` |
| Frame 85 | 3 / 36 | true | `1.28746e-05 / 0.0174658` | `1.84155e-06` |

HDR readback was 160 x 138, 64 view steps and 8 shadow steps. The full modifier
density error bounds were `0.000555692` and `0.0149243`. These are successful
readback checks of the two displayed states, not evidence that the subsequent
operation or all fixture renders succeeded.

The reviewer opened both [frame 55](modifiers/modifier-55.png) and
[frame 85](modifiers/modifier-85.png). The full cloud is inside the viewport.
The intentional concave cut on the right of the column remains visible after
detail/density changes, alongside the thin rightward anvil. Frame 85 shows
stronger tonal/detail variation. Blue primitive overlays and coarse internal
render pixels remain visible. The UI retains stage `0.800` and structure
`30b1579138e92089`, while the detail seed/strength/erosion change. Its displayed
generation count is zero since the fixture baseline; that does not mean the
process never performed its initial generation. The modifier controls are below
the visible scroll area, so these images do not establish widget usability.

## Failure and separate local diagnosis

The original stdout ends after the frame-85 HDR check. Stderr then records:

```
ERROR: Scene validation failed:
- Finishing masks/detail exceed the 0.5% GPU density precision budget
```

The preserved job excerpt records `capture-modifiers.ps1` reporting
`input failed: 1` at `2026-09-20T21:13:57Z`; this was an exit-code failure,
not a timeout. No frame-110 screenshot, modifier Save/Open result, modifier
HDR export metadata/EXR, six-fixture GPU comparison, or requested-cache fallback
result was produced. The six retained recipe JSONs and manifest were generated
before native input; they are not saved versions of the frame-85 scene.

The archive does not itself name frame 105. The separate local CPU reproduction
in `local-cpu-diagnosis/` executes the actual ImGui/app lifecycle with GPU calls
stubbed and fails at frame 105. Its `modifier_editor.cpp` Git blob is identical
to the Windows head. The failing intermediate candidate duplicates protection
layer 3: bound `0.0270609` exceeds limit `0.0230016`; the prior/reordered state
has bound `0.0149243`. The core probe source and raw bounds log are retained.

The local fix `f3693c8c0dbb53abb6fbdabcdb7c89f4e0121ac1` explicitly checks that
the over-budget duplicate is rejected without changing current state,
revision or Undo/Redo, then exercises legal duplicate/delete of cut layer 1
(bound `0.0165893`). The recorded local after-log passes all 125 frames and
Save/Open. This keeps the precision guard; GPU-stubbed success requires a
separate exact-head Windows rerun before claiming the native failure is fixed.

## Verified Freeze v2 checkpoint

The preceding Freeze step uses schema 11 and Frozen contract version 2.
All four calm/wind front/side selected/frozen EXR pairs are byte-identical;
the retained comparison reports show RGB and T maximum difference zero.
All 11 exports use Direct density, 192 x 108, 80 view steps and 8 shadow steps,
with maximum logged CPU T error `4.24602e-06`.

Every EXR and display PPM is also byte-identical to its corresponding file in
the [previously preserved PR #79 evidence](../freeze-pr79/REVIEW.md), despite
the new schema/contract metadata. The verification manifest references those
existing bytes, so this record retains only v2 metadata/logs, comparison reports
and the three saved lifecycle recipes. Native UI screenshot identity is not
claimed. The historical dark-lighting limitation remains applicable.

Native logs report adoption with one generation job, finishing with zero
additional jobs, cancellation/retry preservation, replacement Undo, and zero
reload jobs. Each of the three fresh-process metadata scenes exactly equals
its saved JSON. First/detail retain v2 content hash `8847715720294864584`;
replacement uses `4508914605705131407`. This validates the recorded v2 fixtures,
not every possible authored clipping envelope.

These are injected native workflow commands with GPU readback, separate from
human mouse/keyboard acceptance. **Naturalness and physical RTX validation
remain Hold.** The precision rejection, successful preceding readbacks and
local CPU fix evidence must remain distinct from the pending native rerun.
