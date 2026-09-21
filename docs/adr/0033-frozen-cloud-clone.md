# ADR 0033: Clone a frozen cloud without changing its current density

Issue #37 distinguishes a copy of the present cloud from a new generated seed
variation. `clone_frozen_cloud` copies the complete Scene by value and never
starts a growth job. It reissues the object, fields, primitives, cuts, curves,
controls, profiles, hierarchy and generation-input identities. All internal
references use the returned remapping. Geometry, transforms, masks, detail
values, optics, selection and view remain unchanged.

The allocator uses a fresh, strictly increasing range above all existing and
reserved IDs by default. An optional explicit range must be nonzero, disjoint
and fit in uint64. It rejects collisions and overflow before changing the copy.
Source collection and reservation limits bound allocation and traversal work.
Control/profile IDs retain their existing separate-namespace alias semantics;
every occurrence of an original numeric identity maps consistently. Finishing
layer IDs are a separate namespace and receive a separate mapping; their target
IDs use the object/field mapping. Mask coordinates, order and values are copied.

Density evaluation sorts primitives by ID and uses
`mix(global_seed ^ mix(cell_id) ^ mix(local_seed))` for its warp key. Monotone ID
allocation preserves merge order. To preserve the key with a fresh ID, the copy
stores
`local_seed' = unmix(mix(local_seed) ^ mix(old_id) ^ mix(new_id))`.
SplitMix64's XOR shifts, odd multiplications and addition are invertible modulo
2^64. Inverse odd multipliers are calculated with six Newton iterations; XOR
shifts use the finite inverse XOR series. Thus all uint64 seeds, including zero
and maximum values, are handled exactly without searching. Only the internal
per-primitive seed compensation changes; detail settings and the current field
remain exact. CPU samples and the complete GPU density packet verify equality.

Typed provenance is retained and remapped, including pinned controls and cell
rules. Its fingerprint is recomputed with the canonical generation-input
encoding. Known source contracts may be reconstructed to validate and hash the
input; the growth algorithm is never invoked. Unavailable generation versions
remain unavailable and copyable, and a history-free fixed field stays history
free. A later explicitly requested generation uses the cloned identities and
therefore an independent random stream. It is not promised to reproduce the
old development from the same visible seed, especially for ID-keyed top lobes.
The authoritative cloned density is immediately identical and independent.

Tests cover all four presets at seed zero, uint64 maximum and maximum minus one;
exact CPU/GPU density; typed provenance and persistence; independent detail
edits; unavailable algorithms; history-free data; monotone disjoint mappings;
allocation ending exactly at uint64 maximum; zero, collision and overflow
rejection. Schema 11 also exercises separate finishing IDs and remapped targets
with unchanged masks and exact finished density.
