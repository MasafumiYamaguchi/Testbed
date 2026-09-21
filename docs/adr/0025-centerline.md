# ADR 0025: bounded centerline and altitude profiles

Issue #29. This version adds a source modifier over the version-1 Cumulonimbus
prefab. It preserves its five generated Cell IDs and three optional manual
cells. It is not an unrestricted tube primitive or an independent centerline
for every lobe. Native source persistence and Inspector/gizmo integration use this same source
model. GitHub Actions images and CPU/GPU comparisons remain the acceptance
evidence; physical GPU performance is deferred by user instruction.

## Source and geometric meaning

`CenterlineShape` owns the existing `CumulonimbusGroup`, two to six stable-ID
control points, and two to eight stable-ID profile knots. Point and profile IDs
have separate namespaces from generated Cell/Cut IDs. No separately editable
lowered graph is stored in this source model.

The curve parameter `t` is normalized cloud-local height, not arc length. Its
position is the prefab growth axis at height `base + height*t` plus an XZ offset
in absolute cloud-local metres. The base is fixed in **cloud-local coordinates**
and moves in world space only when the cloud transform changes. Curve controls
cannot change its plane, and their offsets never acquire a Y component.

The five existing cell roles sample the curve at t=0.18, 0.42, 0.70, 0.72 and
0.76. Their original lateral offsets and axis-aligned ellipsoid axes are
preserved. The curve adds its lateral displacement and scales each role's
three radii by the radius profile at that role. Thus this is a bounded lobe
placement model, not the exact swept support of a continuous tube. A narrow
radius-profile band that contains no role will not reshape a lobe. Additional
independent developed cells require a later source topology, not silently
reinterpreting the five existing roles.

After this deformation, stable-ID local center offsets, radius multipliers and
seed overrides are reapplied unchanged. Manual cells, cuts, cloud TRS, optics,
base transition, overlap, blending, noise coordinates and seeds remain in the
same source model. Zero offsets with a unity profile derive the exact existing
prefab Recipe, including for a tilted growth axis.

## Interpolation, endpoints and bounds

The XZ offsets use cubic Hermite interpolation on each interval. Endpoint slopes
are one-sided secants; an interior slope is the secant between its two neighbors.
For interval length h, endpoint offsets p/q and slopes m/n, the coefficients in
u=(t-t0)/h are:

- A = 2p - 2q + h(m+n)
- B = 3q - 3p - h(2m+n)
- C = hm; D = p
- offset(u) = ((Au+B)u+C)u+D

Each interior slope is shared by both neighboring intervals, so position and
first derivative are continuous at control points. Endpoints are evaluated
exactly. Duplicate or reordered parameter values are rejected; adjacent points
must differ by at least 0.001 in normalized height. Control offsets are bounded
to 10,000 metres per XZ component. Sampling outside [0,1] is explicitly rejected.

The local tangent has a strictly positive Y derivative equal to `height`, so
there is no zero tangent or centerline self-intersection in height. The frame
uses `N=normalize(cross(T,+Z))` and `B=cross(N,T)`. Positive T.y makes the
reference-axis cross product nonzero; there is no branch that abruptly switches
reference axes. This frame defines handle orientation. It does not rotate the
existing axis-aligned ellipsoids.

The conservative curvature bound is
`max_interval_endpoints |offset''(t)| / height^2`, required to be at most
0.1 per local metre. It follows from |r'| >= height and
|r' cross r''|/|r'|^3 <= |r''|/|r'|^2. A Hermite second derivative is affine on
an interval, so its norm is bounded by the endpoint maximum. This bound is
conservative; an edit can be rejected even when its exact maximum curvature
would be smaller. It is not a nonintersection guarantee for the overlapping
ellipsoid lobes, which intentionally fuse.

Profiles use continuous piecewise linear interpolation, clamped to the endpoint
values below/above the normalized height range during density evaluation.
Radius multipliers lie in [0.125,4], density multipliers in [0,1]. Actual derived
ellipsoid radii must also pass the existing Recipe radius limits. These combined
constraints are validated before source publication; invalid combinations are
never silently clamped.

Finite support is recomputed after curve changes and local edits, including
manual cells. It uses the same conservative anisotropic-ellipsoid expansion as
the prefab for smooth union, bounded warp and the fixed edge band. Density
multipliers cannot enlarge support; their maximum multiplies the existing
conservative density upper bound. Base and cut masks are unchanged.

## Density profile and lowering

`CenterlineEvaluationPlan` evaluates the fully lowered `DensityField`. The
Recipe owns `AltitudeDensityProfile { enabled, base, height, knots }`, with at
most eight `{t, scale}` knots. CPU and HLSL apply this modulation once, after
shape/noise/base/cut evaluation; their conservative maximum multiplies the
maximum knot scale. Profile coordinates use saved local base and height, never
current AABB coordinates. The density profile does not alter support or regrow
masked regions.

Full `lower_centerline_to_recipe` and `lower_centerline_to_graph` retain
nonuniform profiles. `centerline_geometry_recipe` remains an explicitly named
pre-density-profile diagnostic, not the render snapshot. All-unity source knots
canonicalize to the disabled/default Recipe profile, preserving the previous
prefab density exactly. Disabled profiles return one and have maximum one.

The shared GPU density uniform grows from 768 to 912 bytes: one float4 contains
base, height, enabled knot count and maximum scale, followed by eight float4
knots. The field compute shader and direct volume shader include the same
profile helper. Baked density already contains the profile; cache interpolation
and hard clipping do not multiply it a second time. Density hashes include the
saved profile, while optical/exposure-only changes retain the density cache.

Enabled varying profiles must satisfy an additional float-precision policy:
`altitude_density_error_bound(profile) <= 1e-4` in density-multiplier units.
The estimate uses the largest piecewise slope, actual base/height/knot/scale
packing errors, normalization and interpolation roundoff, and a four-float-ULP
allowance for local Y at the profile's largest coordinate. Varying adjacent
knots must also have distinct packed physical heights. Validation rejects
unresolvable bands before source publication, with instructions to widen the
band, reduce contrast, increase height or move the local base toward zero.
It never flattens or silently clamps such a profile.

This is a profile-modulation budget under the stated local-coordinate error
assumption, not a proof about all ray/grid coordinate construction. Actual GPU
readback remains the acceptance check. Its tolerance adds only the computed
profile bound times the **unmodulated** density upper bound to the existing
density/noise tolerance, and logs that addition. Disabled, unity and constant
profiles add zero; ordinary scalar coefficient rounding remains covered by the
existing kernel tolerance. The reproducible failures at base=1e6/height=1e-4
and at base=100000/height=10 with a t=0.5..0.501 transition are now rejected.
Broad high-coordinate profiles and resolved local bands remain supported.

This extension is density algorithm version **3** and Field Graph version **2**;
`FieldDensity` carries both global density and the altitude profile. Unity and
disabled profiles retain algorithm-2 arithmetic, but the new bounded operation
is not silently assigned the older contract version. Legacy Scene schemas 1–5
migrate to schema 6 / algorithm 3 with the profile disabled. Centerline source
serialization retains the source alone; its Recipe is a validated derived
snapshot, mutually exclusive with a separate prefab authority.

## Shared editing and reproducibility

`CenterlineDocument` accepts existing Cumulonimbus height/direction/width commands
and detailed control/profile commands through one validated source/history path.
Moving an interior point changes its normalized height and XZ offset, preserving
its stable ID. Crossing another point or changing an endpoint plane is rejected;
height is edited through the high-level height command. Horizontal endpoint
movement preserves its exact t=0/1 instead of recovering that value through
subtraction/division, which can round incorrectly for fractional base/height.

Point insertion samples the old offset at the requested height and assigns the
supplied unique ID. Recomputing neighboring Hermite slopes can change the curve;
this is an explicit topology edit, not an exact knot insertion algorithm. It
does not redefine noise origin, noise scale, cell IDs or random sequences. Height
and direction edits likewise preserve absolute local control offsets and local
cell adjustments.

Drags group into one Undo snapshot; failed/no-op edits preserve revision;
Undo/Redo/cancelled edits advance revisions. Cancelled and net-zero gestures
preserve redo history. History is bounded to 128 source snapshots.

## Verification

The standalone test exercises straight, tilted and gently curved clouds, exact
legacy compatibility, C1 control-knot behavior, orthonormal frames and numerical
curvature below the analytic bound. It checks both radius limits, a local
altitude-density band, zero density, finite support and flat base, stable IDs,
manual edits/cuts/noise preservation, exact GPU parameter equality for uniform and nonuniform profiles, disabled/unity
legacy equality and profile cache invalidation. It also exercises invalid/collapsed points,
source versions, profile limits, bounded bakes, shared Inspector/gizmo commands,
compound Undo/Redo/cancel and the history bound. The recorded core run compares
69,825 CPU samples with their float bake values exactly. The separate altitude
profile suite checks 7,429 bounded cache samples, eight-knot packed-float
interpolation, the 912-byte layout, masks, and no double application after bake.
DXC successfully compiles both the field compute and volume fragment shaders;
actual GPU readback and screenshots come from the Windows Actions run.


## Scene and native editor integration

Scene schema 6 stores `cloud: { kind: "centerline", source: ... }`, containing
one versioned prefab source and its stable curve/profile controls. It never
serializes a second generated Recipe. The in-memory Recipe is a validated render
snapshot. Old schemas 1–5 migrate with the altitude profile disabled; old plain
prefabs retain their original source type. Custom Cloud conversion preserves the
full lowered altitude profile. Unknown versions, mixed source kinds and stale
snapshots fail before publication.

`scene_with_centerline_command` is the common command route for control-point
position, radius/density profile and existing width/height/direction values.
`EditorSession` remains the application's sole Undo history. Local Cell/Cut/noise
widgets preserve curve/profile source through `edit_centerline_recipe` rather
than rebuilding away local adjustments. The same source is used by the common
GPU preview and asynchronous bake uniform path.

The Inspector starts with open source headers, selectable control IDs, local
positions, point insertion/removal and four initial height-profile controls.
Radius controls affect the five generated lobes at their center heights; the
continuous local-height density profile is evaluated independently. This
limitation is also displayed in the UI. Endpoints have X/Z gizmos with their
exact height plane restored after float-matrix decomposition; intentional
numeric vertical edits are rejected. Public size handles avoid front-view
scale picking degeneracy and apply local deltas to the original double values.

`capture-centerline.ps1` records actual injected control-gizmo input, command
identity, one-step Undo/Redo, preserved base/noise/source, Save/Open, front and
side views in Direct/128/256 density modes, and a density diagnostic slice.
Every GPU density sample validation remains active. Raw EXR cache comparisons
report interpolation differences; they do not demand unchanged view T when
the density sampling model differs. Existing shadow-cache T checks retain
their strict behavior. These new Windows captures require their own Actions
pass and visual review; local compilation and CPU tests are not that evidence.
