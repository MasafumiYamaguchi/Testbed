# ADR 0035: Export payload reservations before buffer allocation

Status: bounded partial implementation of [Issue #43](https://github.com/MasafumiYamaguchi/Testbed/issues/43),
building on the single-state contract in ADR 0034. This is a CPU planning API
and command-line report. It does not allocate or bake an output grid, implement
a GPU export queue, write OpenVDB, or establish Phase 2/3 acceptance.

`plan_export_resources` reads an existing `const DensityExportSnapshot`. It owns
no additional Scene, evaluator or candidate. It copies only layout/identity and
resource-policy numbers into a plan. The snapshot is already captured and its
continued retention is reserved separately. Native file parsing, snapshot capture
and small report strings can allocate; the claim of pre-allocation checking
applies to the output grid and execution buffers, not to those bounded inputs.

No generation, sampling, texture creation, transfer creation or file writing
occurs in the planner. Resolution and padding continue to use ADR 0034's world
metres, outward support bounds, voxel centers and zero padding. Snapshot and
finishing hashes stay fixed across resolution changes; the export request hash
changes with sampling layout. Tile sizes, queue capacities and resource limits
do not change sample values or export-request identity.

## The accounting model

All quantities are requested **payload bytes or caller reservations**. They are
not measured heap, GPU residency, VRAM, process working set or peak memory. The
planner does not claim that a sparse OpenVDB tree costs the same as dense floats.

The modeled execution path retains a CPU dense float grid. This is an explicit
planning model for a future implementation; it is not a requirement that every
future sparse writer allocate a dense grid. An alternative implementation must
provide another declared resource model rather than omit this reservation.

| Resource category | Requested amount |
|---|---|
| `cpu_dense_grid` | `N.x * N.y * N.z * 4` bytes, including padding |
| `gpu_tiles` | Packed maximum effective tile bytes times GPU tile slots |
| `transfer_staging` | Row-aligned tile bytes times transfer/staging slots |
| `cpu_readback_queue` | Packed tile bytes times completed CPU readback slots |
| `retained_snapshot` | Caller-supplied reservation for retained snapshot/evaluator/assets |
| `accounted_total` | Checked sum of the five categories above |

The caller supplies independent limits for all six rows, including total. Limits
are compared with exact integer `requested <= limit`; exact fits are allowed,
and a one-byte overrun is reported and rejected. Zero is a real zero limit, not
an unlimited sentinel. The report lists every exceeded category.
`require_export_resource_reservations(plan)` throws if any limit is exceeded.
The CLI returns exit 2 for the same condition before creating execution buffers.

The effective tile extent is the component-wise minimum of the positive requested
tile extent and N. Per-axis tile counts use `1 + (N-1)/tile`, so the final partial
tile is included without overflow-prone `N+tile-1`. All slots reserve one full
maximum-size tile; smaller edge tiles do not reduce the pool reservation. The
explicit caller slot counts are not reduced even if there are fewer tiles.

For the staged model, a row of `tile.x * 4` bytes is rounded up to the supplied
positive power-of-two alignment. The default is 256 bytes, matching the current
preview's transfer accounting, but the caller must choose the actual backend's
requirement. That pitch is multiplied by tile.y and tile.z. Completed readback
slots are separate packed CPU buffers after row-padding removal; they are not
aliases of staging buffers in this model. No fence ordering, queue scheduling or
driver allocation behavior is implemented or inferred from these counts.

All three slot counts zero select a direct CPU-to-dense-grid model, with zero GPU,
staging and completed-readback reservations. All three positive select the staged
reservation model. A partially specified staged pipeline is rejected. Unused
CPU-model transfer pitches/sizes are zero rather than speculative allocations.

The retained-snapshot reservation must at least cover the known lower bound
`sizeof(DensityExportSnapshot) + max(0, metadata.size()+1-sizeof(std::string))`.
This includes the metadata terminator while conservatively crediting the entire
string object as possible inline character storage, independent of the library's
small-string strategy. The report separately shows inline size, metadata text
length and `known_payload_lower_bound_bytes`. This is a necessary lower bound,
not an estimate of full retained memory. The caller's reservation must
also cover dynamic vectors, strings, provenance, any retained fixed assets and
the owned evaluator's copies of evaluated data. JSON file size is not substituted
for resident bytes. The current planner does not measure these allocations, enforce
an allocator cap, or prove that an arbitrary caller reservation is sufficient.
This is a concrete remaining boundary of #43's retained-asset memory accounting.

Checked uint64 multiplication/addition covers full-grid bytes, tile counts,
row alignment, slots and the combined reservation. Invalid dimensions, slot
combinations, alignments and overflow fail before execution buffers exist.
ABI-specific inline size can vary between builds; output-density identity does
not depend on it. Byte counts and hashes are JSON decimal strings so consumers
cannot lose values beyond their 53-bit numeric precision.

`openvdb_tree_bytes`, `allocator_and_driver_overhead_bytes` and
`vdb_output_file_bytes` are explicitly null, meaning unestimated. The report keeps
`actual_memory_admission_established:false` and `writer_ready:false`, including
when `modeled_reservations_fit:true`. Existing editor/preview resources, other
applications and uncaptured candidates are outside the reported export pools;
callers must account for them when setting available limits. The summed payloads
do not imply that all resources occupy the same physical memory domain.

## Usable CPU entrypoint

`white_export_plan` builds with the CPU-only configuration and does not depend
on OpenVDB or a GPU. For example:

```sh
white_export_plan --recipe cloud.white.json --voxel-size 1 \
  --padding 1 --tile 32 24 16 --slots 2 2 3 --row-alignment 256 \
  --snapshot-reservation 4194304 \
  --limits 1073741824 67108864 33554432 33554432 8388608 1207959552
```

The six limits are GRID, GPU, STAGING, QUEUE, SNAPSHOT and TOTAL, in bytes.
The numbers above are explicit example policy inputs, not measured requirements
or a product recommendation. `--slots 0 0 0` models the CPU path. The recipe,
positive metre voxel size, snapshot reservation and all six limits are required.
Tile, slot, row-alignment and padding options are otherwise optional with defaults
shown by `--help`. Repeated or unknown options, negative/overflowing integers,
nonfinite voxel sizes and missing values are rejected.

Standard output is machine-readable JSON. Exit 0 means the modeled reservations
fit, exit 2 means a caller limit was exceeded, and exit 1 means invalid input or
another error. A successful report includes the unchanged snapshot/finishing hash,
export request hash, voxel centers/bounds, counts, aligned payloads, limits,
unknown-memory fields and `generation_jobs:0`. File parsing and planning accept
recognized Frozen data with unavailable generation implementations, without
regenerating or changing the source. No Native project is written.

## Verification and scope

Tests use a known 7 x 9 x 11 padded output and 4 x 6 x 8 tiles. Independent
expected values distinguish packed tile bytes from 256-byte-aligned transfer
rows and account for different GPU/staging/readback slot counts. Tests cover
exact fits and one-byte failures for all six limits, CPU zero-transfer behavior,
oversized requested tiles, exact and one-byte-below known snapshot payload bounds,
slot and alignment validation, uint64 product/sum
overflow, decimal counts above 2^53, output identity across quality settings and
unchanged generation-job counts. The CLI test opens an existing Native Frozen
v1 fixture through normal migration, checks JSON and exact 0/2 exit codes for
fitting/over-budget plans, and checks exit 1 for nonfinite/zero voxel sizes.

Manual crop is deferred: cutting only the sample range without a declared crop
mask would change the zero-padding/boundary meaning of ADR 0034. Supersampling,
filter quality modes, frequency/crop diagnostics, dense baking, GPU queue
execution, measured retained memory, real VDB overhead, file publication and
DCC validation are also outside this checkpoint. It makes no sampling-accuracy,
paint, naturalness or physical-RTX acceptance claim. Software reconstruction
evidence can be reviewed independently without treating this planner as a writer.
