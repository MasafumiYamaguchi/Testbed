# ADR 0027: independently developed cells within eight primitives

Status: full source, Scene, renderer and editor integration implemented. CPU validation passes; Windows capture verification is a required CI check. Physical device review remains deferred.

## Versioned source and explicit cost

`DevelopedCloud` contract 1 is a new source alternative, not a reinterpretation
of manual ellipsoids. It owns one object transform and optical material, plus
zero to two independent `DevelopedCell` records. Every record owns a complete
saved `CenterlineShape`, local translation, width/height/growth parameters,
radius/density profile, noise settings, local seeds, adjustments and cut/manual
children. Its curve and profile IDs remain local to that record. Development,
reserved primitive and cut IDs are globally unique within the object.

Each record explicitly activates three to five of the existing generator roles.
The base, middle and upper roles 0/1/2 are mandatory so a new development can
represent its own bent curve. Roles 3/4 are optional lateral lobes. All five role
IDs remain reserved when a role is inactive. Existing manual children also
consume primitive slots. The total is at most eight ellipsoids, so a legacy
five-role development plus a new three-role development is valid; two legacy
five-role developments are rejected. Role changes affect only the selected
record and never silently resample another development to make capacity.

Migration retains all five roles, manual children, curve/profile, noise and
cuts. The old object transform/material move to the new object; the sole
development's local transform is identity. A single development evaluates via
the existing `DensityField`, giving exact density arithmetic and exact legacy
Recipe/GPU parameters at zero translation. Moving a development carries its
entire local field, including noise, profile and cuts. All developments share
one optical material; this model does not sum independent optical coefficients.

Add/remove operate on complete developed records. Width/height/growth, curve
points, profiles and structure reseeds use the existing typed centerline
commands on the selected record. Duplicate copies a complete curve/profile and
allocates new development/primitive/cut IDs, remapping local adjustments. Seed
retention/regeneration is explicit. Retention preserves seed values; new IDs
still choose distinct local random streams under the existing density algorithm.

## Shape fusion and density overlap

Each development first merges its active primitive distances using its saved
intra-development fusion width and computes its existing noise, local density,
intra-development overlap, base, cut and altitude-profile factors. Call the
resulting implicit distance `d_i`, nonnegative density coefficient `c_i`,
coverage `q_i = coverage(d_i)`, and density `rho_i = c_i q_i`.

For two developments, the object fusion width `k` has units of local metres:

```
d_f = smooth_min(d_A, d_B, k)
q_f = coverage(d_f)
bridge = max(0, q_f - max(q_A, q_B)) * min(c_A, c_B)
rho = max(rho_A, rho_B) + bridge + overlap * min(rho_A, rho_B)
```

The same polynomial smooth-min and two-metre coverage transition as the existing
kernel are used. `overlap` is dimensionless in [0,1]. Fusion adds only geometric
bridge coverage; overlap separately controls density in actual overlap. The
bridge is continuously gated by both density coefficients, so a cut/profile
fading one development to zero cannot hide or abruptly reveal another. This
avoids both positive-density smooth-max ghosts and coefficient-weighted SDF
occlusion. Two zero-density developments and points outside finite support
remain exactly zero.

If each development has maximum `M_i`, the result is bounded by
`max(M_A,M_B) + overlap * min(M_A,M_B)`. Shape fusion cannot increase that bound.
The evaluator clamps ordinary floating-point overshoot to the declared bound.
Developments are ordered by stable ID before evaluation and GPU packing; local
primitive ordering remains the existing stable-ID order. This rule is a bounded
artistic composition within one medium, not a claim of independent-medium
transport or meteorological convection.

## Support and renderer representation

Individual primitive supports retain the existing anisotropic implicit-distance
bound, including intra-development smooth fusion, bounded domain warp and edge
transition. Two-development support additionally expands each primitive by
`k/4` in implicit-distance units with the same per-axis radius scaling. The
union is translated to object-local coordinates and transformed conservatively
to world space. The evaluator returns exactly zero on/outside that support.
During grouped fusion, sampling must not clip to the original individual
envelopes, because the deliberate geometric bridge can extend them.

`GpuDevelopedParams` packs two complete existing density-parameter blocks,
two local translations, object support and count/fusion/overlap/maximum metadata.
The shader must take the exact legacy path for one group. Two groups use the
formula above and preserve separate noise and altitude density profiles.
Concatenating the primitives into a single old Recipe would lose those profiles
and is explicitly rejected. The CPU bake uses the same grouped evaluator.

Nonzero local Y translation adds float-coordinate cancellation before profile
sampling. Validation conservatively requires twice the sum of the existing
local and translated altitude-profile error bounds to remain within 0.0001.
Both bounds matter when a large local base is moved back near object zero.
Disabled/constant profiles have no coordinate sensitivity. Zero Y translation
keeps the existing accepted migration domain exactly. A narrow otherwise-valid
profile moved to Y=100000 is rejected with an actionable band/translation error.

## Verification

The dedicated tests cover exact single-group migration with nonuniform profile,
noise, cuts and manual children; independent height/curve edits and reseeding;
stable unrelated generation; five-plus-three and three-plus-three capacity;
deletion without conversion; duplicate remapping; explicit role changes;
transparent empty objects; order invariance; per-coefficient fusion bounds;
the `d_A=-10, d_B=-1, c_A -> 0` continuity regression; absence of distant ghosts;
finite support; grouped uniform layout; and bake/evaluator agreement.

## Scene, editor and rendering integration

Scene schema 7 stores `cloud.kind = developed` and only the canonical source.
Older schemas retain their existing source kinds and exact density semantics.
For zero/one development the derived Recipe and legacy density cache key remain
exact. Two developments use the full 1904-byte grouped packet and canonical
source density hash; edits to the second profile, curve, seed or position reset
progressive accumulation. Shared optical changes preserve the density key.
The two-group Recipe is validated metadata and support only, never an evaluator.

The Inspector selects stable development IDs and exposes Add, Delete, Duplicate
with explicit seed policy, role budgets, local movement, width/height stretching,
growth, seeds, curve control points and profiles. All controls and viewport
handles use the same typed development commands and existing EditorSession
transaction/Undo history. The source remains the sole parameter store. Size
handles use translated top/right controls and delta matrices, preserving
fractional dimensions on orthogonal edits and working in a front view.

One development preserves the dense cache, sun cache and empty-space skip paths.
Two developments explicitly render Direct density and Direct sun, with no shared
hard-mask clipping, cached sun or majorant skip. Requested cache flags are
retained for returning to a single development; EXR metadata reports the actual
rendered path. CPU reference MC constructors explicitly reject two developments
until a frozen grouped-grid transport contract exists.

`capture-developed.ps1` runs immediately after the initial Windows app capture.
It checks actual mouse movement against the identical command and one Undo/Redo,
independent stretching, source Save/Open, deletion and the empty state. It saves
zero/one/two-development captures and validates Direct fallback metadata despite
requested density/sun/skip acceleration. The GPU comparisons include the full
baked voxel grid and 256 direct points at GPU-reported coordinates. These are
numerical and UI checks, not physical acceptance or a claim of a swept tube.
