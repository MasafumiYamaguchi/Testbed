# Issue #34 checkpoint — 2026-09-20

Status: **WIP, paused at the user's handoff request.** Do not close Issue #34 or
treat this checkpoint as accepted. No GitHub push or PR was made by this agent.

Worktree: `/workspace/scratch/59c2b798b94d/modifiers`  
Branch: `codex/phase3-modifier-integration`  
Foundation: local `bc80e12`, equivalent remote tree at
`9f7c06400394188d73fc0d6d0e9e49c97b7193c2`.

Implemented in this checkpoint:

- Four ordered stable-ID layers: ellipsoid cut, local density multiplier and
  detail protection. Object/field targets resolve by ID; missing targets reject
  publication. Full-strength hard cut interiors stay zero after later layers.
- Separate `Scene.finish_stack`; layer edits preserve the exact frozen payload,
  structural content hash and generation job counter. Valid references retain
  all layers when adopting a new candidate; broken references preserve the old
  document with a diagnostic.
- Shared CPU evaluation and packed `GpuFinishedParams` (2288 bytes). GPU code
  adjusts per-field noise and density coefficients before one `anvilDensityAt`
  call. Active finishing stacks use Direct preview.
- Strict schema 11 `cloud.finishing` alongside the frozen source, with schema
  1–10 migration to an empty stack, exact uint64 IDs and Undo/Redo commands.
- Editor controls for enable, strength, masks, targets, reorder, duplicate,
  delete and solo. `start_modifier_test()` / `modifier_test_step(frame)` hooks
  finish their assertions at frame 105 and save `modifier-smoke.white.json`.
- `white_modifier_tests <output-dir>` can generate six comparison recipes:
  base, cut, density, protected, protected-seed and solo.

Verification completed before pausing:

- Release CPU build succeeds; **all 37 CTest contracts pass**, including new
  mask/order, stable references, hard cut, detail protection, exact frozen
  identity, zero regeneration, migration and adoption tests.
- GCC C++20 syntax checks pass for `modifier_editor.cpp`, `editor_ui.cpp` and
  `generation_editor.cpp` against the installed SDL/ImGui headers.
- `git diff --check` passes.

Resume work:

1. Integrate root's later Freeze commits, including reviewer commits `824a856`
   (frozen numeric bounds) and `58e2bc6` (cut-ID validation), plus root's native
   CLI/workflow/capture wiring. This branch intentionally starts before those
   followups; it must not overwrite them. The changed `FrozenEvaluationPlan::at`
   and `shape_sample` bodies are independent of their validation edits.
2. In `density_allowance`, multiply both profile and anvil density tolerances by
   `finish_density_bound(scene.finish_stack)`. Verify modifier-mask numeric
   precision against actual GPU readbacks; do not widen a failing tolerance
   without an explicit bound.
3. Wire `--modifier-test` to the two EditorUi hooks, run at least 120 frames,
   capture after frame 105, and update native capture schema expectations to 11.
   Existing scripts in this foundation still expect older schema versions.
4. Compile HLSL with DXC and run Windows Debug/Release GPU readback and native
   screenshots. These have **not run** for this checkpoint. Also exercise the
   six-recipe fixture generator and inspect its captures.
5. Coordinate the one-line `adopt_frozen_with_finish` guard with the separate
   Issue #37 preset work in `generation_editor.cpp`; preserve its new behavior.

The current scope is object-local masks only. No world-fixed mask choice or
paint interface is exposed. Physical RTX performance, final visual naturalness
and later VDB export acceptance remain outstanding.
