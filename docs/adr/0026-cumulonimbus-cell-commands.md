# ADR 0026: bounded Cumulonimbus v1 cell commands

Status: partial implementation for Issue #30. This adapter does not complete the
issue's independent developed centerline cells or native GPU acceptance. It
uses the existing five generated ellipsoid roles and up to three
additional manual ellipsoids without changing their source contract.

## Supported commands

`command_cumulonimbus_cell` accepts typed add, duplicate, remove, absolute local
move, absolute local radius and explicit local reseed commands. Every operation
first validates the source and returns a validated replacement. The application
publishes that replacement through its existing document/session history; this
adapter owns no second history or independently editable derived Recipe.

`scene_with_cumulonimbus_cell_command` preserves either authoritative source.
For centerline scenes, absolute move/scale commands refer to displayed deformed
geometry and update the underlying local adjustments. Duplication captures that
displayed geometry as a manual ellipsoid; the same object altitude-density
profile still applies. The shared Inspector helper exposes Duplicate with
explicit seed policy and Reseed, each published once through `EditorSession`.
Reseed advances the local uint64 seed by the fixed odd increment
`0x9e3779b97f4a7c15`, wrapping as unsigned arithmetic; it is deterministic and
always changes the value. Capacity and validation failures remain visible in
the Inspector status. Actions are disabled while another drag is active.

Generated cells keep their original role IDs. Move and radius commands update
only that ID's offset/scale; reseed updates only its local seed override. Manual
cells are edited directly. Parameters, other IDs, cuts, noise origin, optical
coefficients and source modifiers survive. Capability queries expose the
generated/manual distinction and remaining manual slots. Deleting a generated
role fails with an explicit Custom Cloud conversion requirement. No automatic
conversion, hidden suppression or zero-radius deletion is performed.

Add and duplicate require a nonzero ID unused by the cloud, any cell or cut.
`next_cumulonimbus_cell_id` provides the next ID above all live IDs and rejects
uint64 exhaustion. A duplicate is a manual ellipsoid snapshot, even when its
source was generated. It therefore keeps its captured local geometry when later
high-level width/height parameters change. This behavior is deliberate and must
be visible in an Inspector that offers the operation.

Duplicate seed policy is mandatory: retain copies the numeric local seed;
regenerate takes an explicit, different uint64 value. Density algorithm 2 mixes
both the stable ID and local seed into a cell's random key. A new duplicate ID
therefore has a distinct random sequence even with a retained seed. Retain does
not promise identical warped geometry. An exact random-pattern clone would need
a separate saved random-identity field or a revised generator contract; this
adapter neither changes the global seed nor conceals that distinction.

Selection remains an application concern. The helper retains a selected ID only
while it exists and otherwise returns no selection; it never retargets another
cell. An application can retain the previous selected ID in its existing Undo
record when it needs deletion Undo to restore selection.

## Ordering, density and limits

Adjustment and manual-cell storage are normalized by stable ID. The five
generated ID slots are not sorted: their order defines the generator roles.
Density evaluation already sorts cells/cuts by ID, fixing the meaning of the
order-dependent smooth union. An unrelated addition, move or reseed does not
alter another cell's descriptor or local random key.

The existing eight-cell and eight-cut shader limits remain enforced. Support is
re-derived after every edit, including far-away manual additions and enlarged
cells. Shape fusion width and density overlap remain separate source modifiers.
Density stays bounded by `density * (1 + overlap * (cell_count - 1))`; fusion
width alone cannot increase this bound. This is shape composition within one
medium, not addition of independently parameterized optical media.

## Required source evolution for full Issue #30

The current group has one growth direction and fixed generated roles. Issue #29's
centerline source deforms those roles along one curve; manual ellipsoids do not
become developed curve sources. Full independent cell behavior requires a
versioned collection of developed-cell records with their own stable IDs,
centerlines, growth/radius profiles, local transforms and random identities.
The object-level fusion/density operation must then combine those independent
evaluators within a declared evaluation budget. Migration must preserve the
current five-role source's density meaning and exact Undo provenance.

Until that source evolution and UI/CI work are complete, only the supported
ellipsoid-level operations should be presented as available. Issue #30 stays
open.

## Verification

`cumulonimbus_cells_tests` checks generated/manual edits, target-only full-width
reseeding, explicit duplicate policy, stable random keys for unrelated cells,
generated-delete rejection, missing selection, ID collisions/exhaustion,
eight-cell rejection, conservative support growth, canonical ordering,
grouped Undo/Redo, and bounded complete overlap for independent fusion widths.
The scene adapter additionally checks actual centerline geometry capture,
target-only deformed move/scale, profile preservation, one-step session Undo,
and source-only save/load after duplication and reseeding.
The test does not establish developed-curve independence or GUI acceptance.
