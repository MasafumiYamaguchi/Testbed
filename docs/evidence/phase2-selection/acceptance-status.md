# Issues 29–31: implementation and acceptance status

This record concerns PR #76 head `864e0e5c6e540bce3dc0d39ba9e67f0cb77e8a4e`
and PR #77 head `1b1566654fea282c838aae89944d728ea8f1ed61`. Later anvil or
Freeze work is outside these tested commits. Passing a contract test does not
close an Issue or establish natural appearance or physical GPU performance.
The cited contracts are the [tests at the PR #77 tested head](https://github.com/MasafumiYamaguchi/Testbed/tree/1b1566654fea282c838aae89944d728ea8f1ed61/tests),
not an assertion about subsequent source revisions.

## Issue 29: selected development stage and altitude wind

| Requirement | Evidence at the tested commits | Remaining scope |
|---|---|---|
| Editable upright, tilted and curved sources | Existing typed centerline commands, viewport handles and saved height/radius/density profiles; inherited centerline CPU and Windows input tests | No new claim of a continuously swept tube |
| Shared growth and wind input | `GenerationSettings` owns a dimensionless stage, finite altitude knots and per-development onset/amount; [ADR 0029](../../adr/0029-selected-growth-and-wind.md) fixes units and coordinates | The generation draft is not persisted in schema 8 |
| Growth beyond uniform scale/translation | `generation_tests.cpp` compares height, relative top/base radius, lobe radius, anchored base and independent onset | Visual growth comparisons require image review |
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
| Contact and coincidence with separate masks | PR #77 adds X=35 and X=0 fixtures with separate base/cut/profile data, full GPU grid samples, 256 direct points and CPU image transmittance checks | Review the actual captured comparisons before appearance acceptance |
| Shared stage, onset and wind reference | Generation tests compare distinct onset, fixed guides and stable IDs against the same initial source; settings expose per-cell amount | Native Freeze handoff is tracked separately |

## Issue 31: bounded cloud-top hierarchy and early shape review

| Requirement | Evidence at the tested commits | Remaining scope |
|---|---|---|
| Top-only change with exact lower protection | CPU tests compare unchanged density below/on the top-mask boundary, continuity above it, finite support and OFF packet identity; GPU probes include mask and lobe points | Appearance of the transition must also be reviewed |
| OFF / parent / children under matching light | `capture-top-lobes.ps1` records front/side/slice images and strict Direct versus requested-cache output comparison | Image review is separate from numeric success |
| Structure/detail seed separation | CPU and Windows top-lobe tests preserve node geometry, parent/depth and stable path IDs while detail seed changes | At most one parent and two children, maximum depth two |
| Finite cost and invalid radius/depth rejection | Bounded hierarchy tests cover capacity, invalid settings, density maximum and conservative world support | Active top hierarchies across multiple developments are explicitly rejected |
| Shared stage/wind direction | Parent radius evolves with stage; lobe anchor follows the generated centerline; direction uses its tangent without applying wind displacement twice | Finite art-direction law, not a weather solver |
| Early isolated-cloud comparison | PR #77 generates calm/shear × stage 0.35/1 × front/side from the same initial source and seed | Image review and up to three shape/lighting findings are required; successful generation alone is not naturalness acceptance |

## Validation boundary

The Windows runner uses D3D12 on Microsoft Hyper-V Video. Logs report CPU work
and submit-to-fence wall time; GPU hardware timestamps are unavailable. RTX
performance and physical-device acceptance remain deferred. No main merge or
Issue closure is part of this evidence record.
