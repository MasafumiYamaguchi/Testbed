# 0003: Scene is the editing source of truth

Status: implemented for Issue #5. No serialization or renderer dependency.

Scene schema/algorithm versions begin at 1. A Scene contains a camera, sun and
single CloudRecipe. Recipe owns stable IDs, cells, cuts, transform, envelope,
base constraint, structure/detail seeds and optical parameters. CPU Document
exposes only const Scene access. Validated replacement publishes a monotonic
session revision and a change category; invalid replacement changes nothing.
Revision is not a persistent object ID and Undo must not decrement it.

## Coordinates and units

Right-handed world: +X right, +Y up, +Z completes the basis. World distances are
metres. Camera looks from position toward target; up must be unit and not parallel
to that direction. Projection FOV is vertical in degrees. Clip conventions and
API Y inversion belong exclusively to the renderer, not the stored Scene.

Cloud-local positions precede TRS: positive component-wise scale, unit quaternion
rotation, then translation. Negative/zero scale and non-unit rotations are
rejected instead of silently repaired. Cells/cuts are axis-aligned ellipsoids in
cloud-local space. Cloud base is local-Y and moves with the cloud. Envelope is
local; it is an explicit shape constraint, not an automatically recentered noise
coordinate system. Changing the envelope must not remap procedural coordinates.

Grid bounds are outer voxel faces. Center of index (i,j,k) is
`min + ((index + 0.5) / extent) * (max-min)`. Thus the minimum face maps to -0.5.
X is contiguous, Y next, Z last. Fractional indices are valid. Grid extent and
bounds are derived cache settings, not editing identity.

Density rho is a bounded nonnegative control value. `sigma_t = extinction_scale *
rho`; extinction_scale is per world metre. Ray distances must therefore be world
metres even when field evaluation uses inverse cloud transforms. Albedo is [0,1].
Exposure EV is a display parameter and cannot change extinction or density.
Sun direction points toward the light, independently of its nonnegative RGB
irradiance. Numerical coordinates/coefficients have explicit finite limits in
validation; radius and scale in [1e-4,1e4], at most 8 cells and 8 cuts.

## Identity, versions and dirty categories

IDs are nonzero uint64 and globally unique across the cloud, cells and cuts.
`next_id` returns max+1 with overflow rejection; array positions are never IDs.
Deletion/reordering leaves surviving IDs untouched. Structure random keys use
only cloud structure seed, cell stable ID and cell structure seed through a fixed
64-bit mixer; detail seed and vector position never enter that stream.

Density, optics, sun, camera and display dirty flags are independent. An optics
change does not dirty the field. Actual cache invalidation and asynchronous job
publication are Issue #12, not implemented by these flags alone. Reordering may
conservatively dirty density; it must not change the stored IDs or random keys.

Future persistence saves all Scene fields, preserving uint64 IDs/seeds exactly.
It does not save Document revision, last-change flags, GPU texture handles,
readback buffers, caches or accumulated images. Unsupported schema/algorithm
versions are rejected until an explicit migration exists (Issue #6).
