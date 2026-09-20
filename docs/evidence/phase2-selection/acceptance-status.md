# Issues 29–32: implementation and acceptance status

This record concerns PR #76 head `864e0e5c6e540bce3dc0d39ba9e67f0cb77e8a4e`
and PR #77 head `1b1566654fea282c838aae89944d728ea8f1ed61`, plus the separately
identified partial anvil evidence from PR #78 head
`ff6ed399700f8925a0929bdec3f0a6f9291f1881`. Formal Freeze work is outside these
tested commits. Passing a contract test does not
close an Issue or establish natural appearance or physical GPU performance.
The cited contracts are the [tests at the PR #77 tested head](https://github.com/MasafumiYamaguchi/Testbed/tree/1b1566654fea282c838aae89944d728ea8f1ed61/tests),
not an assertion about subsequent source revisions.

PR #76 and #77 native runs now pass all four Windows/CPU jobs. The PR #77
[early shape review](growth-top-pr77/REVIEW.md) has been completed and preserved;
naturalness remains **Hold**. PR #78's [partial review](anvil-pr78-partial/REVIEW.md)
distinguishes its completed 16 fixture comparisons from its failed cache-request
run. No failed or pending comparison is promoted to a pass.

## Issue 29: selected development stage and altitude wind

| Requirement | Evidence at the tested commits | Remaining scope |
|---|---|---|
| Editable upright, tilted and curved sources | Existing typed centerline commands, viewport handles and saved height/radius/density profiles; inherited centerline CPU and Windows input tests | No new claim of a continuously swept tube |
| Shared growth and wind input | `GenerationSettings` owns a dimensionless stage, finite altitude knots and per-development onset/amount; [ADR 0029](../../adr/0029-selected-growth-and-wind.md) fixes units and coordinates | The generation draft is not persisted in schema 8 |
| Growth beyond uniform scale/translation | `generation_tests.cpp` compares height, relative top/base radius, lobe radius, anchored base and independent onset; all eight actual growth views inspected | Growth is visible; naturalness remains Hold for the documented large-shape/lobe/detail findings |
| Relative wind versus whole-cloud motion | Wind deforms unpinned guide X/Z; saved reference base/height remain fixed; bulk translation is a separate transform | Finite guide spacing limits high-frequency wind variation |
| Reproducibility and stable structure | Deterministic retry/return-to-stage tests preserve cell and lobe IDs, primitive seeds and noise coordinates | No continuous-time or frame-rate animation guarantee |
| Failure, cancellation and constraints | Invalid stage/wind/knots/IDs and nonfinite input are rejected; mid-generation cancellation publishes no candidate; retry and one-step adoption Undo pass CPU tests | The new asynchronous generation buttons do not yet have a dedicated injected-input Windows test |
| Immutable result for later stages | `GenerationCandidate` contains initial source, settings/version, input hash, evaluated Scene and elapsed CPU time; density samples do not run growth | Formal persisted FrozenCloudState and finishing are tracked in Issues 33/38 |

## Issue 30: independent developments and bounded fusion

| Requirement | Evidence at the tested commits | Remaining scope |
|---|---|---|
| Move, stretch, remove and duplicate one development | `developed_cells_tests.cpp` checks isolated edits, unrelated local random keys, duplicate seed policy and ID remapping; Windows script checks move-command equality, Undo/Redo, stretch, delete and Save/Open | Limited to two developments and eight total primitives |
| Separate shape fusion and overlap density | Typed `DevelopedSetFusion` plus Inspector controls preserve independent sources; width changes support, overlap changes density bound | Artistic composition in one shared optical medium |
| No unbounded density or order dependence | Tests enforce `max(MA,MB) + overlap*min(MA,MB)`, stable-ID ordering, finite support and no distant ghost density | Two developments use Direct rendering; shared caches are unsupported |
| Contact and coincidence with separate masks | PR #77 X=35 and X=0 fixtures pass separate base/cut/profile checks, full GPU grid samples, 256 direct points and CPU image transmittance; actual views inspected | Functional/numeric evidence does not establish natural-cloud appearance |
| Shared stage, onset and wind reference | Generation tests compare distinct onset, fixed guides and stable IDs against the same initial source; settings expose per-cell amount | Native Freeze handoff is tracked separately |

## Issue 31: bounded cloud-top hierarchy and early shape review

| Requirement | Evidence at the tested commits | Remaining scope |
|---|---|---|
| Top-only change with exact lower protection | CPU tests compare unchanged density below/on the top-mask boundary, continuity above it, finite support and OFF packet identity; GPU probes include mask and lobe points; actual slice inspected | No clipping is visible in these views; image inspection is not an all-input proof |
| OFF / parent / children under matching light | Six actual front/side views and a density slice inspected; strict Direct versus requested-cache linear RGB/T differences are zero | Added upper protrusions are small; side-view parent/children difference is weak at this framing |
| Structure/detail seed separation | CPU and Windows top-lobe tests preserve node geometry, parent/depth and stable path IDs while detail seed changes | At most one parent and two children, maximum depth two |
| Finite cost and invalid radius/depth rejection | Bounded hierarchy tests cover capacity, invalid settings, density maximum and conservative world support | Active top hierarchies across multiple developments are explicitly rejected |
| Shared stage/wind direction | Parent radius evolves with stage; lobe anchor follows the generated centerline; direction uses its tangent without applying wind displacement twice | Finite art-direction law, not a weather solver |
| Early isolated-cloud comparison | All eight calm/shear × stage 0.35/1 × front/side views inspected from the same initial source and seed; inputs, evaluated Scenes and three shape/lighting findings preserved | Early review recorded; Issue #31 naturalness and multi-development active-top acceptance remain open |

## Issue 32: anvil evidence at PR #78 tested head

| Requirement | Evidence at the tested commit | Remaining scope |
|---|---|---|
| Upper ON/OFF, width/extension/direction and lower protection | Core field tests pass; all 16 actual OFF/narrow/wide/tilted/wind/manual/young/top views inspected | Smooth lens/column construction remains conspicuous; naturalness Hold |
| Neck, finite support and density bound | CPU contracts and completed GPU grid/direct-point/HDR checks pass; no detached sheet is visible in the saved views | Snapshot inspection cannot prove every accepted combination |
| Boundary/invalid geometry safety | Height/thickness/radius/precision/coordinate cases reject invalid sources without replacing prior state in core tests | Physical-device validation remains deferred |
| Common stage/wind and manual override | Completed fixture views show shared bending, extension and differing manual direction; actual handle/Inspector/Undo/Save/Open markers pass | One active developed trunk only |
| Requested caches and slice | Direct fixtures completed with caches disabled | `wide-fallback` timed out after 240 seconds; strict equality and the later slice did not run and require a corrected CI result |

## Validation boundary

Renderer logs identify Microsoft Basic Render Driver / D3D12 with validation
layers; the OS adapter listing reports Microsoft Hyper-V Video. Logs report CPU work
and submit-to-fence wall time; GPU hardware timestamps are unavailable. RTX
performance and physical-device acceptance remain deferred. No main merge or
Issue closure is part of this evidence record.
