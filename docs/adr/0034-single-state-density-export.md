# ADR 0034: One frozen, finished density state for VDB

Status: executable contract foundation for [Issue #42](https://github.com/MasafumiYamaguchi/Testbed/issues/42).
This adds a CPU snapshot/coordinate/metadata API, not a product VDB writer.
Issue #41's Phase 2 Go decision, physical RTX measurements and natural-cloud
acceptance remain separate open gates. The user authorized implementation work
while those acceptance gates remain open. No Issue is closed by this ADR.

## Authority and supported version

The export job captures one `DensityExportSnapshot` by value from an already
validated `Scene` whose only cloud authority is `FrozenCloudState`. Its Scene,
settings, evaluator, layout, metadata and fingerprints are immutable. There is
no Document reference, generation callback, live candidate pointer, timeline or
per-tile seed update. Capture rejects an unfrozen procedural source. Capture,
voxel sampling and metadata creation must invoke zero generation jobs.

The common `SceneDensityEvaluator` evaluates the captured Frozen fields and
ordered finishing stack. The job transforms each world-space sample into the
captured object's local frame exactly once. The evaluated growth and wind
deformation therefore exist in density values, including the evaluated anvil;
they are not instructions for a recipient's wind solver. After capture, editing
the stage, wind, candidate, current cloud, detail or finishing has no effect on
the job. A new export requires a new capture. Cancelled export must not replace
any completed output; publication and fault handling belong to the writer.

Contract version 1 supports Native Scene schema 11 / algorithm 3, Frozen
contract 2 and finishing-stack contract 1. It includes the saved detail seeds,
Base/Macro/Medium/Micro switches, current isotropic detail controls, source hard
constraints and ordered cut/density/detail-protection masks. It does not support
paint, brush assets or the not-yet-implemented regional anisotropic detail model.
`supported_finishing.paint` is explicitly false. Native parsing rejects
unrecognized schema members and finishing kinds, and snapshot capture also pins
the exact supported data versions independently of the application's current
version constants. A future painted project must not be accepted and silently
exported without its paint. Paint support requires
a supported Native/evaluator contract, complete owned paint data in the snapshot,
and a new export contract version with paint-aware fingerprints and tests.
The complete paint-related acceptance condition of #42 therefore remains open.

A recognized Frozen payload with an unavailable generation version remains
exportable. Its saved evaluated geometry, selection, input hash, version and
optional provenance are preserved exactly. Capture never upgrades or substitutes
a generation algorithm. Missing provenance is represented as absent, not a
fabricated initial recipe. Unknown Frozen/data contracts remain errors.

## Grid and density meaning

The eventual file contains exactly one OpenVDB `FloatGrid`, named `density`,
with grid class `GRID_FOG_VOLUME` and background `0.0f`. Its values are finite,
nonnegative, dimensionless density controls `rho`; they are neither a level set
nor an SDF, opacity, extinction coefficient or density integrated over a voxel.
Values above one are valid. Do not normalize by the maximum, multiply by voxel
volume, clamp to one, bake exposure, or change values to compensate object scale.

For each integer coordinate inside the declared sampling extent, evaluate the
continuous final field at that voxel's world-space center in double precision,
then round once to IEEE binary32. Reject negative/nonfinite values and binary32
overflow. Binary32 rounding, including underflow to zero, is part of the format;
no additional zero threshold, quantization or lossy pruning is permitted by
version 1. Positive binary32 values are active; exact zero values may be absent.
Every inactive/background value is zero. Equal-value tiles or lossless tree
pruning may change topology without changing any value or active-value meaning.

The consumer needs this scalar grid and an ordinary volume shader, without
reapplying a ProjectWhite cut mask or running a growth/wind simulation. This does
not promise exact continuous boundaries after finite-resolution reconstruction.

## Coordinates, bounds and padding

The world is right-handed; +X maps to grid i, +Y to j and up, +Z to k. Numeric
world distances are metres. The export lattice is axis-aligned in world space
with one positive finite isotropic voxel size `h`. Native positive nonuniform
scale, quaternion rotation and translation are already baked into the sample
positions. They must not also be attached as an extra DCC object transform.
This choice keeps voxel size physically isotropic after Native object scaling.

Let `[a,b]` be the conservative world-space AABB of the final evaluator. It is
obtained from the evaluated support and all eight transformed corners, not from
nonzero voxel scanning or the initial recipe. It may contain empty space. The
sampling extent never crops this bound. For each axis, define integer face
coordinates on a world-zero lattice:

```
L = floor(a / h) - p
U = ceil(b / h) + p
N = U - L
outer faces = [L*h, U*h]
center origin o = (L + 1/2)*h
world(i) = o + h*i
index(x) = (x - o)/h
```

`p` is an integer padding count in [1,1024], default 1. Outward rounding is
checked against the original bounds after floating-point multiplication, and
corrected outward by one cell if necessary. This avoids accidentally shrinking
support by one rounding unit. The integer coordinates stored in VDB are rebased
to `[0,N-1]`; negative world positions do not imply negative stored indices.
Fractional indices are valid. The minimum outer face maps to -0.5 and the maximum
to `N-0.5`. Integer indices identify voxel centers. Any index outside `[0,N)` has
zero background, without clamping to the edge sample. Linear buffer fixtures use
X fastest, then Y, then Z; an OpenVDB tree does not imply a buffer memory layout.

Metadata retains both the conservative source AABB and padded outer-face AABB,
the center origin, h, p and all three extents. The active voxel bounding box may
be smaller or absent for an empty cloud; it is not a replacement for either
declared AABB. The zero padding is enough to surround the field for the specified
trilinear reconstruction. It does not make a coarse internal cut exact.

The API rejects nonfinite/inverted/degenerate support, nonpositive h, invalid p,
unrepresentable half-cell centers, per-axis N greater than signed-int32 maximum,
and uint64 overflow in `N.x*N.y*N.z*sizeof(float)`. Global unpadded face-index
magnitudes above 2^50 are rejected before half-cell construction. These are
coordinate/size representability checks, not resource-budget approval or an
allocation. Tiling, memory limits, progress and publication are later writer work.

For OpenVDB, construct a uniform linear transform with scale h, then translate
by the recorded **center origin** o. Issue #13's spike uses 0.75 m and
(11.25,-3.5,5.75) as its index-zero center; that is compatible with this
interpretation. Its old fixed origin is a test fixture, not the new support-based
layout algorithm. OpenVDB documents the distinction between cell and vertex
interpretations and the half-cell translation explicitly in its
[transform documentation](https://www.openvdb.org/documentation/doxygen/transformsAndMaps.html).

## Reconstruction and hard constraints

Version 1 specifies point samples and trilinear interpolation of the eight
neighboring binary32 values, with zero background. An OpenVDB recipient can use
`GridSampler` / `BoxSampler`; the
[OpenVDB interpolation examples](https://www.openvdb.org/documentation/doxygen/codeExamples.html#sInterpolation)
identify this as first-order trilinear interpolation. Metadata cannot force a
DCC to choose that filter. Nearest-neighbor, higher-order interpolation, resampling
or an additional transform may produce different values and require separate
comparison. In particular, a higher-order filter must not be assumed nonnegative.

The continuous evaluator applies full-strength hard-cut interiors, flat bases,
explicit crop bounds and each field's constraints to the final density. A cut
targeting one field removes that field's contribution; overlapping untargeted
fields can still contribute density there. A complete object-wide cut, or a point
where every contributing field is zero, produces a zero stored sample. Trilinear
interpolation near a globally zero boundary can mix a zero sample with a positive
neighbor and introduce nonzero reconstruction inside the analytic cut. A feature smaller than a voxel may be
missed entirely. This is finite sampling error, not permission to omit the cut
from source evaluation. No recipient-specific corrective mask is required or
promised. Issue #43 and acceptance Issues #49/#50 must measure density, boundary,
integrated optical-depth and image errors at selected voxel sizes and separate
them from actual calm/wind or finishing differences. No accuracy threshold or
naturalness acceptance is claimed by these contract tests.

## Optical units and rescaling

The Native optical recommendations are carried unchanged: `extinction_scale`
in inverse metres, albedo in [0,1], and Henyey-Greenstein phase g using the
cosine between incoming and outgoing **photon propagation** directions. Positive
g means forward scattering. With world-metre ray length s:

```
sigma_t(x) = extinction_scale * rho(x)     [1/m]
sigma_s(x) = albedo * sigma_t(x)
tau = integral sigma_t(x(s)) ds           [dimensionless]
T = exp(-tau)
```

These are shader recommendations; OpenVDB custom metadata is not assumed to
configure a recipient's shader, phase sign, distance unit or scene scale.
The file records rho, not sigma_t. The importer must map metre positions and
per-metre extinction into its own distance units. If one DCC unit denotes u
metres, numeric positions become `world_m/u` and extinction per DCC unit becomes
`extinction_per_m*u` to describe the same physical cloud.

Uniformly enlarging the physical cloud by k while preserving rho and extinction
multiplies optical depth through corresponding rays by k. To deliberately keep
the old optical depth, the caller would separately divide extinction by k (or
explicitly modify density). Export does neither automatically. Nonuniform scale
has direction-dependent path-length changes and has no single equivalent
compensation factor. Changing voxel resolution changes sampling error; it is
not an optical scale change and must not trigger extinction retuning.

## Identity and metadata

`metadata_json()` emits the versioned schema below. uint64 IDs and hashes are
decimal strings, preserving values above JSON's common 53-bit exact-integer
range. Fingerprints use the repository's sorted-key canonical JSON / FNV-1a 64
convention and are corruption/staleness checks, not cryptographic authentication.

| Field | Required meaning |
|---|---|
| `contract`, `contract_version` | `white.single_state_density`, version 1 |
| `producer` | Application/version and source build commit |
| `grid` | Name, FloatGrid, fog class, zero background, nonnegative rho, active-value policy, non-SDF |
| `coordinates` | Metres, right-handed +Y-up, axis mapping, baked transform, continuous source support |
| `snapshot.id` | Captured Frozen object ID; content hash is required alongside it |
| `snapshot.frozen_content_hash` | Immutable evaluated geometry and selection identity |
| `snapshot.frozen_payload_hash` | Saved Frozen integrity, including current detail/provenance/optics |
| `snapshot.density_input_hash` | Existing common-evaluator density invalidation fingerprint |
| `snapshot.finishing_snapshot_hash` | Current per-field detail seeds/noise/layer switches and complete ordered finishing stack, including IDs/targets |
| `snapshot.hash` | Complete captured Native cloud authority plus Scene schema/algorithm, excluding camera, sun, exposure and preview approximation |
| `snapshot.selection` | Saved kind/unit/value, currently `development_stage` / `dimensionless` / [0,1] |
| `snapshot.*_version` | Native, Frozen and finishing data contracts used to interpret the capture |
| `generation` | Preserved input hash/version, optional saved settings, provenance presence and regeneration availability; no export-time evaluation |
| `supported_finishing` | Explicit current detail/mask support and `paint:false` |
| `optics_recommendation` | Extinction per metre, albedo, HG g/direction convention; no implicit scale compensation |
| `export_settings` | h, p, zero lattice anchor, padded faces, center origin, N, sample count, center sampling, trilinear filter, zero threshold 0 |
| `export_request_hash` | Export contract version + snapshot hash + complete export settings/layout |
| `authority` | `native_project`; VDB cannot restore Native editing state/history |
| `dcc_metadata_auto_application_assumed` | false |

The finishing fingerprint intentionally includes dormant/disabled saved layer
settings; the effective density fingerprint is separate. The full snapshot hash
also includes provenance and optical recommendations so they cannot silently
change under one export identity. Resampling changes only request settings/hash,
not snapshot or finishing identity. View changes affect neither. The stage is
model state, not seconds and not an external animation frame; no frame rate or
timeline frame is synthesized.

The future writer must embed this complete JSON as grid string metadata
`white:metadata_json`, plus integer `white:contract_version`. It may mirror the
same bytes as `density.metadata.json` for readers without custom-metadata access.
Standard OpenVDB name/class/transform/background properties must also be set on
the grid itself; JSON descriptions are not a substitute. The present API only
creates this metadata string. The writer must validate its embedded copy on
reopen, record actual writer/OpenVDB versions, and publish complete data safely.

The Native project is the editing authority and must be retained separately.
These metadata fields describe provenance and interpretation; they do not
reconstruct cells, masks, paint, nodes, Undo state or the entire generation
history from a sampled VDB. Byte-identical VDB files across library/compiler
versions are not promised. The same captured state and settings under the same
numerical implementation reproduce the same layout, fingerprints and samples;
future implementation changes require versioned compatibility and comparison.

## Evidence and remaining work

`white_density_export_tests` is CPU-only and leaves the optional OpenVDB target
independent. It checks asymmetric nonunit spacing, translated origin, axis
ordering, fractional index/world mapping, outer faces, padding and overflow;
known density after rotated/nonuniformly scaled Native transforms; explicit
rejection of nonfinite world samples; two-field/active-anvil agreement with the
complete evaluator and a field-targeted cut retaining its independent neighbor;
exact Native
roundtrip/re-capture; resolution identity; a baked cut and its explicit coarse
trilinear leakage; detail/stack capture isolation; density differences between
calm and wind; later candidate/stage isolation; zero generation calls during
sampling; unknown generation versions and absent history; and the world-scale
versus optical-depth relationship along each nonuniformly scaled axis.

Issue #13 already has a separate verified small Windows VDB write/reopen spike
and dependency/license evidence at `docs/evidence/vdb`. This change does not
replace that spike or claim a new VDB roundtrip. Production writing, tiling,
reopen comparisons, resolution error studies, imported DCC renders, paint
support and Phase 2/Phase 3 acceptance remain outstanding. The eventual exporter
must carry forward the accepted #41 calm/wind and finished fixtures; these CPU
tests are structural contract evidence, not those accepted visual fixtures.
