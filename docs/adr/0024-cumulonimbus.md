# ADR 0024: Cumulonimbus source contract and parameter commands

Status: source, Scene persistence, Inspector and parameter gizmo implementation
for Issue #28. Windows Actions screenshot acceptance is recorded separately.
Physical GPU acceptance is deferred under the user's explicit instruction.

## Source of truth

`CumulonimbusGroup` owns contract version 1, the cloud ID, five stable generated
cell IDs, public parameters, stable-ID local cell adjustments and modifiers.
It does not own a second editable generated graph. Recipe and typed six-node
Field Graph snapshots are derived on demand through the Issue #27 lowering
contract; CPU density and GPU uniforms therefore share the existing evaluator.

The initial group contains five overlapping ellipsoids: a base, a rising core,
a crown and two upper lobes. This is a basic editable prefab, not a claim of
finished cauliflower, anvil or physically simulated cloud morphology. Structure
seed enters the existing stable-cell warp key; detail seed enters the existing
local-space density/erosion noise. Seeds are full unsigned 64-bit integers.

| Public value | Unit and accepted range | Meaning |
| --- | --- | --- |
| Width | local metres, 10–2000 | Nominal horizontal lobe dimensions; tilt and local edits may enlarge total bounds |
| Height | local metres, 10–4000 | Nominal vertical extent above the base before local edits |
| Cloud base | local metres, −100000–100000 | Vertical reference for generated centers and the base plane |
| Growth direction | unit vector, y ≥ 0.2 | Upward growth; centers lean by x/y and z/y while ellipsoids remain axis aligned |
| Density | dimensionless multiplier, 0–1000 | Global density multiplier before optical extinction |
| Structure seed | uint64, 0–18446744073709551615 | Structural warp seed |
| Detail seed | uint64, 0–18446744073709551615 | Density detail and erosion seed |

`cumulonimbus_parameter_info()` supplies labels, units, ranges and value kinds.
The command API rejects invalid values and wrong variant types, including a
nonunit growth direction; it never silently normalizes or clamps user values.
Changing one public value changes only that value in the source model.

## Preservation of local editing

Generated-cell adjustments are keyed by stable Cell ID. Their center offsets
are cloud-local metres, radius multipliers are dimensionless, and an optional
per-cell structure seed overrides only that cell. Width/height edits do not
rescale the offset or discard a local seed. IDs are never reassigned on a
parameter edit. Adjustments referencing missing or duplicate IDs are rejected.

Modifiers retain cloud TRS, optical properties, noise parameters and origin,
blend width, overlap, base transition/enabled state, three additional manual
cells and up to eight cuts. These objects are copied unchanged when a public
parameter changes. The limits reflect the current eight-cell/eight-cut GPU
kernel. The envelope is derived after these edits so its finite bounds include
manual cells; the expansion accounts conservatively for smooth union and warp.
Incompatible combined local edits fail validation before document publication.
An arbitrary graph edit requires explicit conversion to Custom Cloud rather
than hidden replacement of this source model.

## Commands, reset and Undo

Inspector, whole-cloud gizmo and automation use the same pure
`CumulonimbusCommand` / `command_cumulonimbus` path. `scene_with_cumulonimbus_command`
returns a validated source plus its derived render snapshot. The application
publishes it through the existing `EditorSession`, which owns its only Undo
history. The separate headless `CumulonimbusDocument` is not instantiated by the
application and is not synchronized with the Scene.

Every public parameter has a target-only Reset. Width/height use the whole-cloud
X/Y scale handles, and cloud base uses its Y translation handle. Gizmo matrix
rounding below 0.0001 metres does not alter an untouched parameter. The Inspector
accepts numeric width/height/base/density, full unsigned 64-bit seeds, and a growth
orientation that is normalized once before the shared unit-direction command.
Drag activation/deactivation groups the existing EditorSession transaction;
Escape cancels it. Undo restores prefab creation, source edits and explicit
Custom conversion using the same Scene history.

Existing local primitive, Cut, noise and optical widgets use
`edit_cumulonimbus_recipe` to write changes back into stable-ID source adjustments
and modifiers. Generated cells cannot be silently deleted; the editor requests
explicit conversion to Custom Cloud. The three manual cell slots remain directly
addable/removable. Public height changes preserve those adjustments, manual cells,
Cuts and detail seed. Optical-only changes leave the density input hash unchanged.

Scene schema 5 saves a prefab as `cloud: { kind: "cumulonimbus", source: ... }`.
The generated Recipe is not serialized as another authority. The in-memory
`Scene.cloud` is a derived render snapshot; validation rejects a snapshot that
differs from its source. All rendering and asynchronous snapshots therefore see
validated values. Custom scenes retain the earlier Recipe-shaped cloud JSON.
Versions 1 through 4 migrate to Custom Clouds, without reverse-classifying shapes
as prefabs. Source IDs and seeds remain decimal uint64 strings. Save/Open uses
the established atomic publication and unsaved-change behavior.

## Versioning and conversion

The public prefab contract is version 1, separate from Field Graph version 1
and density algorithm version 2. Topology, cell role semantics and parameter
interpretation cannot silently change under this version. A future incompatible
implementation must retain a version-1 evaluator or explicitly offer migration
or conversion using that evaluator. Unknown versions are rejected, never
reinterpreted as the current version.

`cumulonimbus_to_custom_cloud` returns an exact graph snapshot without mutating
the source. The editor can present that snapshot before changing the object's
type and store the original source for Undo. Conversion is one way: no automatic
graph-to-prefab inverse is specified or attempted.

## Verification and remaining integration

The standalone CPU test checks a nonempty initial cloud, exact Recipe/graph
density and GPU uniform equality, combined parameter extrema, leaned growth,
stable IDs and local-edit preservation, full-width seeds, shared command values,
one-step drag Undo/Redo, target-only reset, cancellation, bounded history,
specific invalid-source diagnostics and exact Custom Cloud conversion.

The persistence contract checks schema 1–4 migration, exact source/Recipe round
trips, full-width IDs/seeds, strict versions/member sets, stale snapshot refusal,
existing EditorSession history, atomic-save faults and failed-load rollback.

`capture-prefab.ps1` launches `white_app --prefab-test --frames 260` and captures
five actual Windows frames. It injects ImGui mouse events into ImGuizmo height,
width and base handles. The resulting Scenes must equal the same typed parameter
commands, with one-step Undo/Redo and unchanged local modifiers/IDs. The script
also checks source-only Save/Open and explicit Custom conversion Undo, retaining
PNG/BMP images and logs. A Windows Actions pass and visual review must still be
recorded before claiming those runtime checks passed; local C++ compilation is
not a substitute. Physical-device review remains deferred.
