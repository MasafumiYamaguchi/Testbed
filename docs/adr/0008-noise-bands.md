# 0008: Fixed local-coordinate noise pipeline

Issue #10. Density uses saved cloud-local metre coordinates `p - noise.origin`,
never coordinates normalized by the current envelope or cell-set bounds. Changing
bounds cannot slide the noise pattern. Transforming the entire cloud carries its
local pattern with it. The initial policy is intentionally simple and versioned:

1. Base/envelope exclude forbidden regions immediately.
2. Each macro ellipsoid optionally evaluates at a bounded, structure-seeded
   displacement. Its stable ID and cell seed derive an independent random key.
   Smooth union and overlap retain #7's stable ID order.
3. Medium modulation multiplies density by `1 - strength * fBm01(p * frequency)`.
4. Micro erosion adds a nonnegative local-metre distance offset before edge
   coverage. Medium multiplication and this replacement coverage commute.
5. Base transition and cuts apply to the final result; complete cuts return zero.

Value noise is original integer-hashed 3D lattice noise with quintic interpolation;
two octaves use weights 2/3 and 1/3. No external noise license/code is copied.
Hash arithmetic is unsigned 32-bit, matching HLSL. CPU folds saved 64-bit seeds
into GPU keys, so upper seed bits are retained. Detail and structure use separate
keys; medium and micro have separate salts. Adding a remote cell neither renumbers
IDs nor changes existing random keys. Overlap can still change combined density.

Medium strength is [0,1]; micro erosion and warp amplitude are [0,20] local metres.
Frequencies are [0.0001,2] cycles/local metre. Warp components lie in
[-amplitude/sqrt(3), +amplitude/sqrt(3)], so displacement magnitude is at most the
specified amplitude. Warping can change the silhouette and extend macro support
by up to that displacement. Micro erosion can shrink the silhouette. Neither
stage is claimed to preserve the original contour. The exact protected regions
are the saved envelope, forbidden base half-space, and complete cut interiors.
The conservative support remains the explicit envelope. The density upper bound
is unchanged: density * (1 + overlap * (cell_count - 1)), or zero for no cells.
Both detail factors are nonnegative and at most one; final density is clamped.

The Inspector separates medium strength, erosion, warp bound and all three
frequencies. Regenerate detail only increments detailSeed, preserving cells,
structureSeed and warp keys. Noise off zeroes amplitudes without losing frequency,
origin or seed settings. Commands participate in Undo/Redo and persistence.

Schema/algorithm version 2 records all noise settings. Version-1 files migrate
with zero amplitudes, retaining their original field. Unknown versions still fail;
noise is never silently enabled during migration.

CPU tests cover seed separation, remote-cell and envelope changes, forbidden
regions, multiple seeds including uint64 max, parameter extrema, displacement/
density bounds, serialization and v1 migration. Windows captures compare noise
off, medium only, added micro, added warp, detail reseed and maximum amplitudes at
fixed camera/Recipe. To bound hosted WARP cost, those six captures all use the
same 96-pixel internal width, 32 view steps and 4 shadow steps. All voxels and
HDR transmittance are read back against CPU references. Physical-GPU speed and
noise aliasing at production resolutions remain future validation.
