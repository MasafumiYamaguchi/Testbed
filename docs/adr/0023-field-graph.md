# ADR 0023: minimal typed pointwise Field Graph

Issue: #27. This is Phase 2 implementation preparation authorized while physical
GPU review is deferred. It does not change the Phase 1 gate to a physical Go.

## Decision and evaluator route

Graph version 1 is a typed, bounded DAG lowered to the existing density algorithm
version 2. Its six known node types are shape, warp, noise, density, mask and
output. Shape owns stable Cell IDs, smooth union width and overlap. Warp owns the
per-Cell structure seed and bounded local displacement. Noise owns the detail
seed, shared saved local origin, medium modulation and micro erosion. Density
owns amplitude. Mask owns the finite envelope, base plane and stable Cuts. Output
connects ordered density and mask ports, and preserves Cloud ID, TRS and optical
metadata. Graph node IDs occupy their own namespace and do not replace Cell IDs
in noise hashing.

Shape → warp → noise → density and independent mask → output is the sole supported
topology. Stage types retain the operation ordering of the existing kernel; they
are not exact SDF values. Erosion of merged shape and modulation of density remain
in their original positions. Warp and noise must use the same saved local origin,
as the current shared uniform layout has one origin. Other connections, repeated
stages and disconnected nodes fail explicitly. Empty clouds are represented by an
empty shape, preserving all other parameters.

`FieldEvaluationPlan(graph)` validates and lowers an immutable snapshot. Its
`at()`, `density_field()`, support and density maximum expose the CPU reference.
`gpu_params()` returns the existing 768-byte `GpuDensityParams` for both direct
preview and bake. Consumers create a plan once per accepted input snapshot and
upload those uniforms to the common `density.hlsli` evaluator. There is no per-voxel
graph traversal or allocation. Invalid graphs cannot produce an evaluation plan
or GPU uniforms.

The initial integration can derive a graph with `field_graph_from_recipe()` at the
existing Recipe boundary. Recipe remains the only editable/saved source during
this migration. `FieldGraphDocument` is an independent headless owner for future
graph-based editing; the app must adopt it in place of Recipe, not keep two live
editable models synchronized. This change does not introduce a node editor,
graph file schema, plug-in nodes, arbitrary code, or simulation.

## Alternatives

The fixed-kernel lowering keeps existing CPU/HLSL semantics, floating-point order,
noise seeds, resource layouts and validation. It deliberately restricts topology
instead of pretending to execute arbitrary DAGs. A small shader instruction IR
would permit more combinations but would need register/storage limits and a new
CPU/GPU operator contract. A JIT adds a compiler, platform/security surface and
runtime pipeline compilation without a requirement here. Both are deferred.

## Validation and edit tracking

Validation caps graphs at 64 nodes, eight inputs per node, eight Cells and eight
Cuts. It rejects zero/duplicate IDs, missing inputs, wrong port types, cycles,
invalid output, unsupported versions and unsupported iterative Grid Operations.
Cycles and missing references are checked in every component before reachability
and lowering. The existing Recipe validator then checks all parameter ranges,
finite positive extents, transforms and seeds/IDs. The explicit envelope is a
conservative finite support even with warp. Density remains bounded by
`density * (1 + overlap * (cell_count - 1))`, or zero for an empty shape.

Node parameter changes, input reference changes, output reference changes and
added/removed IDs have separate deltas. Vector storage order is not semantic.
`FieldGraphDocument::replace` validates before publishing one monotonic revision;
invalid input preserves the previous graph and revision. The graph and density
algorithm versions are explicit; unsupported versions fail rather than silently
changing old data. Dependency hashes for texture reuse can continue using the
lowered Recipe's existing density hash, which excludes optical-only changes.

## Evidence and remaining boundary

The headless contract test compares all 17 fixed Phase 0 scenes on every point of
a noncubic 31 × 33 × 35 grid, plus an empty field and an eight-Cell/eight-Cut case
with maximum noise settings, nonuniform scale and rotation. It checks exact CPU
density, finite conservative limits, envelope boundary/outside zero, exact Recipe
round trip and byte-identical GPU uniforms. Node order and graph-ID renaming
preserve evaluation and stable Cell noise keys. Invalid graph and revision tests
exercise the production validation path. Renamed nodes also retain dependency-first
evaluation order. A queued immutable plan retains its density and uniforms after
the source graph changes; newly created preview and bake plans agree exactly.

The standalone GCC 13/C++20 run passes 680,295 exact density comparisons across
those 19 inputs. The test is `tests/field_graph_tests.cpp`, with the Phase 0
fixture directory supplied as its single argument.

Actual GPU output comparison still uses the existing Actions density and capture
fixtures after preview and bake call sites adopt `FieldEvaluationPlan::gpu_params`.
Uniform identity establishes the same shader inputs; it does not replace recording
the Actions runtime result or a future physical GPU review.
