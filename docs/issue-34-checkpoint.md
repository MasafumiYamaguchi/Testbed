# Issue #34 implementation checkpoint — 2026-09-20

Work resumed after the handoff pause. The implementation is ready for native
Windows integration and evidence collection; do not close Issue #34 until those
checks and the remaining acceptance review complete.

Worktree: `/workspace/scratch/59c2b798b94d/modifiers`

Branch: `codex/phase3-modifiers-resume`

Foundation: `addf15b1aa8134d18529dc116ab34216f587727d`, plus the reviewed frozen
clipping-envelope fix `f5b615d` and the frozen-test lifetime fix `4b6d124`.

## Implemented

- Up to four stable-ID, ordered layers: ellipsoid cut, density multiplier and detail
  protection. Object/field targets resolve by stable ID; missing targets and
  field-role collisions reject adoption without changing the old document.
- `Scene.finish_stack` is outside `FrozenCloudState`. Layer edits preserve the
  entire frozen payload, structural content hash and generation job counter.
- Full-strength hard cuts use a final smooth feather. Their zero interiors and
  continuous boundaries survive any later amplification. Partial and soft cuts
  retain reproducible order with density multipliers.
- CPU and packed GPU entry points apply per-field coefficients before one
  density kernel. Detail protection attenuates warp, medium modulation and
  erosion; it does not snapshot regional density. Direct preview preserves all
  final constraints.
- Schema 11 stores `cloud.finishing` beside the authoritative frozen source.
  Scene schemas 1–10 migrate to an empty finishing stack; frozen contract v1
  migration and v2 clipping metadata remain independently preserved.
- Editor commands provide enable, strength, masks, targets, reorder, duplicate,
  delete and solo with Undo/Redo. Masks use cloud-local metres.
- Precision bounds include mask coordinates, anisotropy, sample-frame rounding,
  density gain, warp/erosion sensitivity, medium modulation, fusion bridges and
  the anvil feather. Uncertainty above 0.5% of the conservative density maximum
  is rejected. Every layer has 12 GPU readback probes across its boundary and
  feather, including explicit hard-interior zero checks.

## Verification and native contract

Release CPU contracts passed 37/37. Modifier and frozen contracts also passed
AddressSanitizer and UndefinedBehaviorSanitizer with leak detection disabled
because LeakSanitizer cannot run under the executor's ptrace environment.
C++20 UI/GPU syntax checks and all 11 strict DXC shader compilations passed.
The float mask boundary/anisotropy sweep measured a maximum absolute error of
`1.57168e-05`, within the coordinate-derived bounds. The pinned official Linux
DXC archive was checksum verified before use. Native
Windows GPU readbacks and screenshots remain the authoritative pending evidence.

`start_modifier_test()` and `modifier_test_step(frame)` finish assertions at
frame 105 and save `modifier-smoke.white.json`. Run at least 120 frames; useful
capture checkpoints are frames 55, 85 and 110. The native log prefix is
`modifier_native=`; numeric mask probes use `modifier_gpu_probes=`.

`white_modifier_tests <output-dir>` produces six schema 11 recipes plus
`manifest.json`: base, cut, density, protected, protected-seed and solo. All six
retain the same frozen structural hash. Root owns the CLI, capture-script and
workflow wiring, including existing native schema expectations.

Issue #37 preset integration must reject preview/adoption of a fresh preset
while the current finishing stack is nonempty: newly created presets may reuse
numeric source IDs with a different meaning. The user must explicitly clear the
layers. Ordinary variation or stage changes can call `adopt_frozen_with_finish`
from the shared candidate-view/adoption path to preserve valid references.

Physical RTX performance, final naturalness and later VDB export acceptance are
separate outstanding checks. No world-fixed masks or paint interface are exposed.
