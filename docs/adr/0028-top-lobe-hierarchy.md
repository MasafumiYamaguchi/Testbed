# ADR 0028: finite top-only lobe hierarchy

Status: bounded single-development implementation for Issue #31, with Scene,
UI and GPU integration. Native comparisons are a CI gate; active hierarchies on
multiple developments remain unsupported, so Issue #31 stays open. No
cauliflower appearance, silhouette quality or hardware GPU timing claim is made
by the existence of this generator.

## A real hierarchy with explicit limits

`TopLobeSource` owns an unchanged `DevelopedCloud` trunk, one target developed ID,
four reserved IDs (field, parent and two child paths), and versioned lobe
settings. The generated records carry explicit parent ID and depth. Parent-only
mode emits one primitive; children mode emits a parent and up to two attached
children, with maximum depth two and total count three. OFF emits none.

The parent is anchored near the upper sample of the target's saved centerline.
Its radius and lateral placement derive from the target ID and structure seed.
Each child derives its center from its parent's center/radius and a stable
branch path. The attachment distance is 0.72 parent radii; child radius is a
bounded fraction of the parent radius, preserving substantial contact instead
of placing unrelated random spheres in space. Upward growth and the curve's
local frame control the branch directions. A bounded branch probability
(`hierarchy_density`) changes which stable child paths exist. Child limit one
versus two never renumbers the retained child or changes the parent.

Detail seed never enters layout, branch occupancy, node identity or radius
generation. It still controls the inherited fine density noise. Structure seed
changes layout and, at intermediate hierarchy density, child occupancy. All
comparison modes retain the same reserved IDs and trunk source. No mutable RNG,
recursive evaluator, unbounded subdivision or redraw-dependent generation is
used.

Parent radius is 0.5..500 local metres and child ratio is 0.2..0.8. Hierarchy and
material density scales are in [0,1]. Requests for depth greater than two, more
than two children, degenerate radii or invalid upward growth are rejected.

## Top mask and exact fixed lower region

The mask boundary is the target's local base plus height times a declared
normalized top start in [0.5,0.9], translated into object-local coordinates.
The top field has a hard zero at/below this boundary and a positive-width
smoothstep transition above it. Its own cut masks and altitude-density profile
are inherited from the target, with coordinates translated consistently.

`TopLobeEvaluationPlan` explicitly calls the unchanged trunk evaluator below
the boundary. The HLSL wrapper likewise calls the legacy trunk branch there.
This preserves the lower field's arithmetic and clipping, not merely the
mathematical result of multiplying a recomputed field by zero. OFF and a
zero-maximum top field preserve the trunk packet, support and density exactly.

Above the boundary, the existing developed-group geometric bridge formula
combines trunk and top coefficients. Inter-group overlap is zero, and the top
group's internal overlap is zero. Shape fusion can add a bridge without
unbounded density addition. Its maximum is `max(trunk_max, top_max)` regardless
of whether one or three top primitives are generated. Both group coefficients
continuously gate the bridge as the top mask fades to zero.

The top transition uses the existing altitude-profile float error analysis,
multiplied by smoothstep's maximum derivative of 1.5. A transition exceeding the
0.0001 modulation error budget is rejected with a widening/altitude diagnostic.
The exact-zero mask branch remains explicit in both CPU and GPU code.

## Budget and renderer contract

A five-role trunk plus a parent and two children uses exactly eight primitives.
Parent-only uses six. Capacity reserves the worst case requested by child limit,
so changing structure seed cannot unexpectedly exceed the budget. Existing
trunk roles are never removed to make room.

The current packet supports two groups. Enabled top lobes therefore require one
trunk development; enabling them on a two-development source is explicitly
rejected even if an individual primitive count might fit. OFF can wrap that
source unchanged. Supporting top hierarchies on multiple independent trunks
needs an expanded grouped representation and separate performance/shape
acceptance. This limitation is not hidden by recasting a top primitive as a
three-role developed centerline.

`GpuTopLobeParams` contains the existing 1904-byte grouped packet plus a 16-byte
top-mask/mode/count record. The new HLSL wrapper is separate from the existing
development kernel. Support includes generated geometry, bounded warp and both
internal/inter-group smooth-union margins with anisotropic radius expansion.
The CPU bake calls the same evaluator. Scene schema 8 saves only the source;
older sources preserve exact density semantics. The Inspector edits typed top
settings and the existing trunk through one EditorSession transaction history.
Source hierarchy settings, reserved IDs and trunk parameters survive Save/Open.
Active top density uses Direct rendering with caches and majorant skipping
disabled; OFF retains the previous single/grouped path. Reference MC explicitly
rejects active top hierarchies until a frozen grouped-grid contract is defined.

The early Windows capture compares OFF/parent/children at the same Direct
quality, camera and light, including front, side and slice views. A separate
requested-cache run is strictly compared with Direct output. GPU checks cover
full voxel grids, direct sample coordinates, the exact mask boundary, transition
points and generated lobe centers. Logs report source-generation CPU time and
render recording/fence wall time; hardware GPU timestamps are unavailable.
These checks do not complete multi-development or physical appearance acceptance.

The Issue #30 followup exposes separate typed object fusion-width and overlap
density controls. Each setting validates atomically and uses the same source
command/Undo path; neither rewrites the independent cell records.
