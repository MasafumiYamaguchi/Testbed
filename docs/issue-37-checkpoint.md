# Issue 37 work-in-progress checkpoint

This branch stops at a reusable, tested core module. It does not complete Issue 37.

Implemented in `cloud_presets.hpp/.cpp`:

- Four version-1 preset records: cumulonimbus, wide, two developments and anvil.
- Presets produce typed source Recipes and existing GenerationSettings. Calm and
  upper-shear examples use identical initial sources and seeds. No new growth
  algorithm is introduced.
- Whole-cloud and selected-development structural seed drafts preserve existing
  IDs, cuts and source adjustments. They do not publish to the current Document.
- Fixed-detail seed regeneration uses the existing frozen-detail command and
  preserves immutable content identity without invoking generation.
- Comparison-view helpers share camera, sun, exposure and preview approximation
  while keeping candidate geometry separate. These helpers are not wired to a
  visible candidate preview yet.

Drafts are ordinary owned values; copying one does not alias its source or wind
arrays. Their IDs remain document-local and unchanged. Global clone-ID reissue
and reference remapping are not implemented.

Still required: preset/variation UI, actual Current/Candidate GPU comparison with
distinct render revisions and disabled edit gizmos, bounded comparison-slot UX,
explicit destructive-reset confirmation, finish-layer adoption integration,
clone-ID reissue, and native Windows evidence. The modifier-stack branch was
being developed separately and is not a dependency of this checkpoint.

Validation at this checkpoint: Release CPU target `white_cloud_preset_tests`
built successfully and `ctest --preset cpu-release -R cloud_preset_contract`
passed. No native Windows/UI verification was run for this WIP.
