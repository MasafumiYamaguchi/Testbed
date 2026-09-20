# Versioned presets and candidate/clone evidence

The Windows Release preset checkpoint passed for PR #81 at
`ccc291fafd140bbfc947b9694cfca73f006a2230` (tree
`bb618a7132527e7fb97bfc18fe2c2b063dac6584`). This records the completed
preset suite, independently of later suites in the same workflow.

- [Native workflow run 35537528522](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35537528522)
- Artifact: `windows-Release-preset-evidence`, ID `10613123612`, uploaded
  `2026-09-20T21:18:38Z`.
- Downloaded ZIP SHA-256:
  `5c89f007bc2781afb5d51acfec1c80e7d39617c8bffdf517b30bf63cd37bbe14`.
  It matches the GitHub artifact digest, whose workflow metadata identifies the
  exact head above. See [artifact-provenance.json](artifact-provenance.json).
- D3D12, Microsoft Basic Render Driver, Windows validation layers enabled.
  Viewport capture: 1110×540; cloud render: 192×108, 80 view steps, 8 shadow
  steps. Density cache, sun cache and empty-space skipping are disabled.

Original PNG screenshots, EXRs, complete Scene JSON, stdout/stderr logs and
comparison reports are retained. Redundant BMP/PPM files and additional reload
window screenshots are omitted. [files-sha256.json](files-sha256.json) records
the retained artifact bytes. No screenshot was redrawn or generated.

## Native lifecycle and exact reloads

Every checkpoint below has exact saved-Scene metadata, a fresh-process reload
with zero growth jobs, and a strict HDR comparison. All seven reported
`linear_RGB_max=0 mean=0 T_max=0`. The strict gate permits at most `2e-5` RGB
and `1e-6` transmittance error; the observed values are zero.
All nine strict EXR comparisons were independently recomputed from the retained
files on Linux with `white_hdr_compare`, with the same zero results.

| Frame | View | Document revision | Render revision | Total growth jobs | Actual screenshot | Strict report |
| --- | --- | ---: | ---: | ---: | --- | --- |
| 100 | Wind candidate | 5 | 9223372036854775810 | 2 | [Candidate](preset-100.png) | [Reload](native-reload-candidate.txt) |
| 125 | Current after Undo | 7 | 7 | 2 | [Current](preset-125.png) | [Reload](native-reload-current.txt) |
| 140 | Detail-edited Current | 11 | 11 | 3 | [Detail](preset-140.png) | [Reload](native-reload-detail.txt) |
| 180 | Scoped candidate | 12 | 9223372036854775811 | 4 | [Scoped](preset-180.png) | [Reload](native-reload-scoped.txt) |
| 235 | Adopted scoped result | 16 | 16 | 5 | [Adopted](preset-235.png) | [Reload](native-reload-adopted.txt) |
| 255 | Fresh-ID clone candidate | 16 | 9223372036854775813 | 5 | [Clone](preset-255.png) | [Reload](native-reload-clone.txt) |
| 285 | Adopted clone | 19 | 19 | 5 | [Clone adopted](preset-285.png) | [Reload](native-reload-clone-adopted.txt) |

The high-bit revisions belong to actual candidate rendering. The Document
remains Current; the current and candidate recipes have identical camera, sun,
exposure and preview settings. Returning to Current restores its revision.
The screenshots visibly distinguish `CURRENT` and `CANDIDATE PREVIEW`.
Candidate controls are disabled, and its image has no Current-only geometry
overlay. The original cloud remains visible when returning to Current.

[input.stdout.log](input.stdout.log) contains all 20 successful lifecycle
markers. It covers guarded/confirmed preset reset, discard/Undo, cancellation,
adoption/Undo/Redo, consumption of a matching reset intent, preservation of a
newer identical-input reset intent, and clone preview/adoption. There are five
global growth jobs, including the initial fixture. The UI counter shows four
editor-launched jobs at the end. Detail edits, clone operations and all fresh
reloads add zero jobs.

Three nonempty finishing layers retain exact IDs, masks, targets, order and
values through detail/stage/scoped generation and adoption. A new preset is
rejected while these targets exist. Clone creates fresh cloud/field/layer IDs
and remaps internal targets while preserving the complete GPU parameter packet.
The [adopted/clone comparison](adopted-clone-same-appearance.txt) and
[clone adoption comparison](clone-adoption-same-appearance.txt) both report
zero RGB and transmittance difference. These checks include the nonempty stack.

The largest recorded density-reference error across the reloads is
`1.38283e-5`; the largest native direct-point error is `3.57583e-6`.
All reported density errors stay within their unchanged, scene-derived
precision bounds. The largest HDR CPU transmittance error is `3.14859e-6`.

## Four presets, two wind states

[preset-manifest.json](preset-manifest.json) records preset version 1,
generation algorithm version 1, structure seed 42, detail seed 17 and stage
0.8. Each calm/wind pair has the same full initial-source fingerprint and
different evaluated content hashes. All eight frozen recipes reload with
zero growth and exact persisted Scene metadata, and pass density/HDR checks.

| Preset | Calm screenshot | Upper-shear screenshot | Maximum RGB difference | Maximum transmittance difference |
| --- | --- | --- | ---: | ---: |
| Cumulonimbus | [Calm](cumulonimbus-calm.png) | [Wind](cumulonimbus-wind.png) | 0.437342 | 0.524669 |
| Wide | [Calm](wide-calm.png) | [Wind](wide-wind.png) | 0.566385 | 0.644481 |
| Two developments | [Calm](multiple-calm.png) | [Wind](multiple-wind.png) | 0.450381 | 0.530634 |
| Anvil | [Calm](anvil-calm.png) | [Wind](anvil-wind.png) | 0.537811 | 0.611193 |

All 15 retained screenshots were opened and reviewed. The calm/wind silhouettes
visibly differ; the wide, two-development and anvil arrangements are present.
The cut/protection fixture's curved hollow is retained through clone and
adoption. Images remain coarse geometric validation fixtures at this render
resolution. This evidence does not establish cloud naturalness, final artistic
quality or physical RTX performance. Native hooks exercise the actual editor
command/state paths; they do not click every panel control with a mouse.

## Known acceptance gaps at this head

This checkpoint does not complete Issue 37. Separate review of the same PR #81
head reproduced three paths outside the recorded native sequence:

- Prepare an ordinary draft, then add or change a Current source cut while
  retaining IDs. Launch/adopt can silently lose that cut.
- Adopt a preset, Undo, then vary its stage. The prepared draft can substitute
  the former preset's structure for the restored Current structure.
- Change the instance translation/scale after preparing a draft. Launch/adopt
  can revert that transform.

A separate follow-up must reject stale prepared structure/provenance before
starting jobs and rebase the latest instance transform correctly, including
the new-minus-old local reference motion through the current rotation/scale.
Frozen finishing, detail, optics and shared view must remain preserved. Those
fixes and their regression results are not represented by the source SHA or
screenshots in this directory. The successful paths above remain useful
evidence within that boundary; naturalness remains on hold.

## Recheck retained evidence

Run `python docs/evidence/presets/verify-evidence.py docs/evidence/presets`
from the repository root. [verification.json](verification.json) records the
independent result: 20 markers, seven native states, eight presets, five total
jobs, nine strict comparisons, exact metadata and finish/clone remapping.
The original EXRs also permit rerunning `white_hdr_compare --strict` for each
native/reload pair and the two clone comparisons.
