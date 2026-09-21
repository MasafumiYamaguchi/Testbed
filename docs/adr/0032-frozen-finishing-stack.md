# 0032: Bounded finishing layers on frozen geometry

Issue #34 adds up to four ordered layers outside `FrozenCloudState`. The evaluated
primitives, content hash, provenance and generation counter are unchanged by layer
edits. Stable modifier IDs use a separate namespace. Each layer targets the frozen
object ID or an explicit frozen field ID; missing references reject publication
with a diagnostic. Generation candidate adoption retains all layers with their
original IDs and mask coordinates. A missing target leaves the previous document
and Undo history intact.

All masks use object-local metres. Object translation, rotation and scale carry
the cloud and masks together. Field translation does not silently move a mask.
World-fixed masks are not offered. Ellipsoid interiors have weight one and a
bounded smooth outward feather. Masks are evaluated before noise displacement,
so a new detail seed cannot shift a mask.

For each field, the ordered stack starts with a density coefficient of one and
detail amplitude of one. A cut subtracts `strength * mask` from the density
coefficient, clamped at zero. A density layer multiplies it by
`1 + strength * mask * (multiplier - 1)`. Thus partial cuts and multipliers have a
reproducible order: half cut then x2 gives one, while x2 then half cut gives 1.5.
A full-strength hard cut instead applies its complete smooth removal feather
after the ordered stack. This is a final zero interior constraint independent of
layer order. Applying the feather after amplification also keeps the boundary
continuous; amplification before the cut cannot leave a nonzero jump just
outside its interior. Original flat bases and source cuts remain final field
constraints.

Detail protection multiplies the amplitudes of macro displacement, medium
density modulation and micro erosion by `1 - strength * mask`. It does not store
regional density snapshots and does not change source geometry or seeds. The
noise input is attenuated before one density-kernel evaluation. The anvil inherits
its column's protected detail and density coefficient. Field-specific controls
preserve the other field's independent base/cut constraints before fusion.

Preview and baking use the same `SceneDensityEvaluator` and packed GPU entry.
The shader modifies its local parameter packet and calls the existing anvil
kernel exactly once. Support stays conservative because suppression and density
multiplication cannot enlarge geometric support; the maximum density multiplies
the source bound by the product of all enabled positive amplification bounds.
An active finishing stack uses Direct preview until an aggregate cache can
reconstruct these local constraints exactly.

Validation covers layer count, IDs, target existence, finite bounded ellipsoids,
strength, multiplier and a float-coordinate precision budget. The editor provides
enable, strength, mask, target, order, duplicate, delete and solo controls through
document commands and Undo. Native scene persistence keeps the source payload and
finishing stack separate and migrates older scenes to an empty stack.

The float comparison budget propagates the mask's coordinate/feather roundoff
through the ordered density coefficient and detail amplitude separately. Detail
protection includes the resulting warp-distance, erosion-distance and medium
density sensitivity; the two-field fusion bridge and anvil feather use their
own derivative bounds. Profile and anvil coefficient tolerances are multiplied
by the finishing density gain. Bake sample-position rounding is bounded using
the complete frozen support frame, including cancellation when a small mask is
inside a much larger sampling envelope. A finishing configuration whose derived density
uncertainty exceeds 0.5% of its conservative density maximum is rejected. The
GPU readback fixture samples every modifier's center, all three exact boundaries,
both sides of those boundaries, the middle feather and the outer feather.

Physical RTX performance and visual naturalness remain separate acceptance work;
CPU invariants and Windows GPU readback/capture evidence establish implementation
behavior only.
