# ADR 0029: direct evaluation of a selected development stage

Status: implements the revised Issue #29 generation contract and connects it to
the Issue #30 developed cells and Issue #31 top hierarchy. Appearance and GPU
evidence remain separate acceptance gates. This is an art-direction model.

## Decision and small comparison

We compared two bounded structural models before adding a solver:

| Model | Work for two developments | State needed | Reproducibility |
|---|---|---|---|
| Direct selected-stage evaluation | Up to 12 guide points, 16 profile knots, 3 top lobes | Initial source, stage, wind knots, versions | Independent of frame rate and evaluation order |
| Fixed-step structural advection | Those points multiplied by a selected step count | Initial source, dt, count, integration convention | Must also validate step refinement and maximum steps |

The initial product needs one selected state. A direct law provides vertical
extension, evolving radius/density profiles and altitude-dependent deformation
without a numerical integration error or an arbitrary time step. We choose it
alone. This table compares algorithmic work and state, not measured solver
performance. There is no fluid, pressure, buoyancy, condensation or weather
prediction solver. Time integration can be reconsidered if the shape review
demonstrates a concrete deficiency.

## Units, reference frame and constraints

`stage` is dimensionless in [0,1]; it is not seconds. Wind knots are horizontal
object-local displacement vectors in metres **at stage 1**, not m/s. Knots use
a saved reference base and height, independent of the generated bounds. Values
are linearly interpolated and clamped outside the reference interval. Two to
six knots, >= .01 normalized separation, <=500 m vector magnitude are accepted.
Object transforms rotate and scale both geometry and this local reference.

Each stable development ID may have a stage onset [0,.8] and an amount [0,1].
Its effective stage is `clamp((stage-onset)/(1-onset),0,1)*amount`. The smoothstep
of that value controls height between the initial fraction and the source's
mature height. Width is not scaled with height. Radius profiles mature more at
the top than at the base; density profiles mature independently. This is not a
uniformly scaled or translated mature cloud. The source height retains the
existing 10 m minimum; extremely small profiles can be rejected by the existing
bounded curve/field contract rather than silently changing that contract.

At a guide's normalized height t, relative displacement is
`wind(base + evaluated_height*t + cell_translation.y) * effective_stage*t*t`.
The t² exposure taper anchors the base even under a constant wind. An optional
whole-object horizontal reference translation is applied separately, scaled
by stage, through the object's transform. It does not change local fields.

Existing nonzero manual guide offsets are pinned by default when capturing a
starting shape. Explicit pinned control IDs preserve their original X/Z
positions; normalized guide heights follow the column's development. The UI
can pin or release each guide. Unpinned guides retain their source offset and
receive wind displacement. Manual primitive adjustments, cuts, cloud base and
noise origins retain their saved local values and existing precedence. Invalid
curvature, precision, radii or support is rejected before candidate publication.

The top hierarchy uses the evaluated target curve, the same effective stage and
its tangent. Parent radius grows with the stage; children retain stable branch
IDs and parent relations. Wind is applied to the column once; the top anchor
follows that curve and does not add the same displacement again. Direction uses
the curve tangent in addition to the explicit top growth direction.

## Ownership and cancellation

Generation takes immutable copies of the initial Scene and settings. A bounded
worker evaluates structures once; ray/voxel samples use the existing shared
CPU/HLSL field. The candidate includes provenance, settings/version, input hash,
evaluated Scene and CPU duration. Repeated evaluation starts from the same
initial source, so returning to a stage never accumulates drift.

The editor keeps the current Document until explicit adoption. Generation has
completed, failed and cancelled outcomes; only success publishes a candidate.
Cancellation is checked before validation, between development units, before
publication and after progress callbacks. Existing candidates survive failed
or cancelled retries. A result cannot silently overwrite edits made while it
was being generated. Adoption uses the existing single EditorSession Undo
transaction. View settings at generation start are retained for comparison.

This initial selected-state Scene can be saved and evaluated without running
growth again. Native provenance persistence and an explicit FrozenCloudState
with finishing layers are Issue #33/#38; the generation draft itself is not yet
a saved document field. The draft does not become a second live edit authority.

## Validation and limitations

The core suite compares two stages, calm/constant/sheared wind, bulk drift,
independent onset, pinned guides, stable IDs/noise origin, bounded support,
flat-base exclusions, deterministic retries, invalid inputs, cancellation,
adoption Undo and selected-state round-trip. Windows fixtures compare calm and
sheared candidates from the same seed with front/side views under identical
light. Generation CPU time is reported separately from GPU submit/fence wall
time. WARP comparisons do not establish RTX performance or natural appearance.

Wind is sampled at the existing finite curve controls, so finer wind features
than those controls can be missed. The existing lobe/primitive budget and
single-development active top hierarchy limit still apply. A sphere-bundle
appearance is a valid reason to revise the model, even when tests pass.
