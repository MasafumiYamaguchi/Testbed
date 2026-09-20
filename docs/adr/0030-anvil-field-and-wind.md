# ADR 0030: bounded anvil connected to the selected development

Status: Issue #32 implementation. Physical RTX performance and appearance
acceptance remain separate gates. This is a shape model, not a tropopause or
fluid simulation.

## Field and authority

Schema 9 adds an `anvil` source containing exactly one top/developed source and
independent anvil settings. There is no second editable proxy. Existing schemas
1–8 migrate to the same static fields without inferred growth or wind.

The anvil is an oriented flattened ellipsoid. Its attachment samples the target
curve at `(start + thickness/2 - base)/height`. The center extends forward by
half the extension, with along radius `(width + extension)/2`, cross radius
`width/2`, and vertical radius `thickness/2`. Shear subtracts
`direction * shear * relative_y` before evaluation. The support includes that
shear displacement and all axis projections, then unions with the trunk/top
support. The same object transform maps every component to world space.

Density uses the trunk's density, local noise origin, detail seed, altitude
profile, flat base and cuts. Medium modulation and micro erosion only subtract
density. Anvil geometry is not macro-warped; the bounded sheet is already shaped
by its width, direction and shear. The final union is a maximum, so overlapping
the trunk cannot sum densities. Its rho_max is the maximum of the trunk bound
and `trunk_density * anvil_density_scale * altitude_profile_max`.

At or below the start altitude, evaluation returns the exact original trunk/top
density. No anvil setting changes the trunk parameters, cloud base, stable IDs
or noise reference coordinates. Enabled positive-density anvils require a
positive-density neck on a nonempty trunk. A cut through the attachment is an
invalid request rather than a silently floating detached sheet.

The first packet supports one active developed trunk, the optional three top
lobes, and one anvil. OFF keeps the existing two-development behavior exactly.
Active anvil renders through the common full-scene density entry point in
Preview, dense sampling and numeric verification. Density and sun-cache requests
fall back to Direct until a complete multi-component cache is supported. The
2016-byte CPU/HLSL packet retains the prior top packet as its prefix.

## Selected stage and common wind

The shared Issue #29 stage remains dimensionless. Before stage .35 the anvil
density contribution is zero. Thereafter smoothstep of `(stage-.35)/.65`
controls density, horizontal reach and extension. Vertical start and thickness
follow the target's evaluated height ratio; horizontal width has its own law.
No timestep integration occurs during density sampling.

The attachment follows the already bent evaluated curve. Under `follow_wind`,
wind sampled in the saved common height reference sets the horizontal direction;
35% of its stage-scaled displacement adds relative forward extension. The wind
gradient across the sheet contributes shear, limited to [-4,4]. This is relative
extension, not another translation of the attachment. Zero wind retains manual
direction and the user-specified horizontal expansion. Whole-cloud reference
translation remains in the common object transform.

Editing direction in the Inspector or through the direction handle turns off
wind following. This manual override leaves the column/top wind unchanged and
uses the user's direction, extension and shear. Width and direction handles
call the same validated scene command as Inspector edits. A gesture is one Undo
step, and invalid geometry leaves the last accepted state unchanged.

## Invalid inputs and evidence

Thickness must be 1–2000 m, width 8–10000 m and at least twice thickness. Extension
is 0–4 widths (at most 40000 m) to preserve a substantial neck. Start is within
50–90% of target height, and the complete thickness must fit below its top.
Fade is positive and at most one eighth of thickness. Directions are unit XZ
vectors. Source validation and the GPU precision budget reject unsupported
coordinates, including insufficient initial height during generation.

`anvil_contract` checks exact lower density, positive necks, support and upper
bounds across rotations/shear, protected cuts, shared wind, manual override,
growth onset, top integration, source-only persistence, migration and one-step
Undo. `capture-anvil.ps1` records actual Windows handle input plus OFF/narrow/
wide/tilted/stage/wind/manual/top comparisons from front and side, with a slice,
full CPU/GPU density checks, HDR transmittance and Direct-fallback equality.
The generated images require human shape review; numeric agreement alone does
not establish a natural-looking cumulonimbus.
