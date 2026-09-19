# Conservative fixed-grid majorants (Issue 19)

Opt-in `--cache 128 --empty-skip`; off remains the baseline. Only the fixed
trilinear density grid is bounded. The procedural evaluator is never bounded
by a coarse set of samples. No positive density threshold is used.

An 8-voxel spatial brick covers edge coordinates [8b,8b+8]. Voxel centers lie
at i+0.5, so trilinear reconstruction may use indices 8b-1 through 8b+8 in
each axis, inclusive. Taking the maximum of these 10³ clamped values bounds
every interpolant (a convex combination). Hard cuts/base/envelope only reduce
density. No noise is added after grid filtering. CPU builds this level plus
2x2x2 maximum parents down to one root. GPU builds the matching leaf bricks;
the preview uses one level, while the small CPU hierarchy is available to
subsequent MC work. No average mip is interpreted as a majorant.

For an empty brick the shader advances along the existing midpoint sample
lattice, conservatively leaving a full sample before its exit face. Every loop
advances at least one index. Zero direction axes are excluded from division;
noncubic extents and nonuniform TRS retain world-distance ray parameters. View
samples with positive bounds follow the original path. Shadow samples are not
skipped in this initial implementation. CPU interval traversal uses explicit
face events, handles coincident boundaries together and checks progress.

Density/TRS/extent changes rebuild the GPU leaf texture before the consuming
frame; pending density falls back to direct rendering without skipping.
Optical/camera/display changes reuse the structure. The interpolation convention
is a compiled constant; changing it requires changing this builder too.

At 128³ the leaf texture is 16³ R32F (16 KiB), at 256³ it is 32³ (128 KiB).
CPU parent storage is less than 8/7 of the leaf storage. Resource accounting
reserves an additional MiB for old/new leaf allocations and validation. GPU
build recording time and bytes are logged; execution belongs to the frame
fence wall time, so acceleration includes build cost rather than hiding it.

Tests exhaust interpolation-cell support, include impulse-on-brick-halo and
random values, check maximum parents, zero axes, exact boundary starts and
fixed-sample equality. GPU readback checks every leaf maximum exactly against
CPU. CI compares empty/homogeneous/cut/thick/low-sun images with skipping off/on:
linear RGB max <=2e-5 and view T <=1e-6, with original EXR/screenshots retained.
Sparse and dense timing logs are both retained; dense scenes may be slower,
so the mode remains opt-in. Physical performance is deferred by user request.
