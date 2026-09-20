# Issue 37 preset and candidate checkpoint

Four version-1 preset records cover cumulonimbus, wide, two developments and
anvil. Each creates an owned source Recipe plus the existing GenerationSettings;
no additional growth algorithm is used. Calm and upper-shear variants start from
identical source data and seeds. Freeze persists the full typed generation input,
so reloading a result does not depend on looking up the preset catalog again.

The Presets and seed scope panel prepares a draft without changing the current
Document. Whole-structure and selected-development seed variations preserve
source IDs, cuts and manual adjustments. After generation, matching fixed-field
noise/detail seeds, layer switches and optics are retained from the job's current
snapshot; a missing target or changed noise frame is rejected. Fixed-detail regeneration is an
undoable command that preserves frozen content identity and selected stage and
does not call generation. Scene edits after a job began prevent adoption;
camera, sun, exposure and preview-approximation changes remain shared view edits.

Current/Candidate displays the actual cached frozen candidate through the same
GPU renderer. The Document remains Current throughout comparison. Candidate
render revisions use a disjoint high-bit namespace; cache publication still
checks the renderer's density-input identity. Geometry, optical and finish
editing controls and gizmos are disabled in Candidate view. Camera/light remain
editable and the latest shared view is used on explicit adoption.

There is one current cloud and one completed comparison candidate. A successful
job replaces the candidate; failed or cancelled work retains the last successful
candidate and Current. Discard can be undone until the next successful job.
Candidate input and output obey the existing scene/primitive budgets, and both
views share one render target. New-preset adoption requires an explicit reset
confirmation and is one Undo command; seed variations preserve source edits.

`generation_candidate_view()` integrates Issue 34's
`adopt_frozen_with_finish(current, candidate)`. The combined branch rejects
a new-preset replacement while finishing layers exist, because reused numeric
IDs do not imply the same semantic target across independent presets. Ordinary
stage/seed variations preserve semantic IDs and may retain the stack through
that strict helper. No automatic layer clearing or rebasing is permitted.

Drafts are ordinary owned values and never alias source or wind arrays. Normal
stage/seed variations keep their logical IDs. The explicit Clone current as
candidate command instead creates a complete fixed-state copy with fresh IDs,
remapped internal and finish targets, and zero generation jobs. The evaluated
appearance is retained exactly; future growth follows the clone's new identity
and can differ from the original even if the displayed generation seed is kept.
Its candidate kind is distinct from a generated candidate. It is compared,
discarded/restored and explicitly adopted through the same bounded slot; adoption
is one Undo command. A cloned finishing stack is already remapped and must never
be overwritten with the original stack at preview/adoption. Unknown generation
versions and history-free fixed states remain copyable. Paint and region-specific
detail editing are outside this change.

Validation: all 39 Release CPU contracts pass, including nonempty finishing
stacks through selected/whole variations, exact adoption/Undo/Redo and fresh-ID
clone remapping. The application sources compile and link in the headless
lifecycle harness.
The preset contract checks all preset versions/seeds, calm/wind source equality,
selected scope isolation, source-cut retention, value-copy independence,
detail-only zero-growth/content identity, and view-only adoption normalization.
Native Windows rendering evidence remains a separate required check.
The actual 300-frame ImGui/app lifecycle also passes in a local headless harness with GPU
calls stubbed; that checks command/state transitions and UI stack balance, not
GPU pixels or cache execution.
The combined native fixture creates cut, density and protection layers, rejects
a fresh preset while those targets exist, and checks exact retention through
detail/stage/scoped changes and remapping through clone/adoption. Its GPU
captures must match independent process reloads under strict HDR comparison.

`white_cloud_preset_tests <output-directory>` writes eight frozen calm/wind
recipes and `preset-manifest.json`. Entries contain `name`, `recipe`, `preset`,
`wind`, `initial_hash`, `content_hash` and `payload_hash`. The initial fingerprint
covers the canonical serialized source, and is identical within every calm/wind
pair. The test verifies every fixture's exact persistence round trip.

For the native hook, call `start_preset_test()` once, then
`preset_test_step(frame, gpu)` before `draw()`, for at least 300 frames. Captures
at frames 100, 125, 180 and 235 cover wind candidate, Current, scoped candidate
and adopted result. Saved recipes are `preset-current.white.json`,
`preset-candidate.white.json`, `preset-detail.white.json`,
`preset-stage-preserved.white.json`,
`preset-scoped.white.json` and `preset-adopted.white.json`. The hook tests view
changes after generation, distinct render revisions, guarded/confirmed reset,
discard/Undo, detail-only, cancelled retry, adoption/Undo/Redo and exactly five
growth jobs. A confirmed preset replacement consumes its reset intent. After
detail/layers/optics edits, the subsequent stage-only job preserves those edits
and adopts without another reset confirmation (frames 130 through 150).
An overflow-checked draft token follows each launched/completed/discarded
candidate. Adopting an older completed candidate preserves a newer prepared
preset's pending reset intent, including when the inputs are identical. Stage
and wind edits within the existing draft keep its token; successful adoption
consumes only matching intent. Discard/Undo retains the token.
Ordinary drafts also retain their prepared Current structure and provenance.
Before starting a job, a changed source, evaluated geometry, identity or history
rejects the stale draft while retaining Current and the last candidate; Undo
and same-ID replacements cannot silently reuse an unrelated starting shape.
Only a matching successful adoption advances this baseline. Detail, layer,
optics, view, finishing and instance edits remain compatible. Frames 128 and
136 check Undo, cut and provenance rejection with zero new jobs.
Ordinary adoption keeps the current instance rotation and scale, adding the
new-minus-old local reference-motion displacement in that current frame. This
preserves manual placement and the algorithm's existing stage-dependent bulk
motion; disabled growth contributes zero displacement. Unsupported or missing
generation history rejects preservation instead of inferring an old motion.
CPU coverage includes nonzero old/new motion, rotated nonuniform scaling,
disabled growth, expanded detail sampling bounds and unknown/history-free data.
Capture the renderer's active scene; do not resynchronize it to the
Document while Candidate is visible.
At frame 240 the test creates a fresh-ID clone without growth; frame 245 tests
clone discard/Undo, frame 255 captures its independent preview, frame 270 tests
clone adoption/Undo/Redo, and frame 285 captures the adopted clone. Additional
recipes are `preset-clone.white.json` and `preset-clone-adopted.white.json`.
