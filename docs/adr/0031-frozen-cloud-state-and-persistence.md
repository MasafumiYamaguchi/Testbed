# ADR 0031: evaluated frozen field as the saved authority

Status: foundation for revised Issues #33 and #38. Regional direction-dependent
detail, paint layers and all finishing tools are not claimed complete here.

## One authority

The editor generates a candidate from an initial source and dimensionless stage,
then explicitly freezes and adopts a successful result. A `FrozenCloudState`
replaces the editable source alternative in the Scene. It stores double-precision
evaluated primitive Recipes, development translations, evaluated curve controls
and profiles, stable hierarchy IDs, and the evaluated anvil frame and dimensions.
There is no density asset and no seed-driven structural lowering in its field
evaluator. The initial typed Recipe and generation settings are optional,
immutable provenance for a separate explicitly requested job.

Each top hierarchy node refers to the only copy of its geometry in the top
field. Curves preserve the selected control frame without becoming a second
generator authority. The fixed noise origin stays in development-local metres;
detail commands cannot alter it. Object and development transforms remain
explicit. The shared `SceneDensityEvaluator` routes preview and density baking
through the same frozen field. GPU parameters use the existing bounded two-group
packet. Multi-field or anvil states use the complete direct GPU evaluator; a single
field keeps the existing cache path. Disabling a top detail layer retains the
two-field packet and direct-mode contract. The existing grouped-cache limitation
has not been removed.

`generation_job_count()` counts actual generation function invocations, including
cancelled and failed attempts. Evaluation, detail edits, optics, exposure, light,
camera, persistence and density cache keys never invoke generation. A saved
frozen result is usable with no initial Recipe and with an unavailable generation
algorithm version. Explicit regeneration rejects an unavailable version instead
of substituting a new implementation.

## Identity, detail and bounds

Three hashes serve separate purposes:

| Hash | Meaning | Changes after detail editing |
|---|---|---|
| `content_hash` | Immutable evaluated geometry, explicit clipping envelopes, IDs, curves, hierarchy, selected stage, transforms and saved noise origins | No |
| `payload_hash` | Integrity checksum of the entire serialized frozen payload, including provenance and finishing parameters | Yes |
| `frozen_density_hash` | Effective density inputs used for cache invalidation; excludes optics and generation provenance | Yes, when density inputs change |

These are canonical JSON FNV-1a 64-bit hashes for deterministic corruption and
staleness detection, not authentication. Field ordering and the IDs that choose
warp keys remain observable density inputs. Top hierarchy and curve metadata do
not spuriously invalidate density when their evaluated field is unchanged.

Each field independently enables Base, Macro warp, Medium modulation and Micro
erosion. Detail seed edits retain all evaluated primitives, hierarchy and stage.
Top-field edits preserve the protected lower region exactly. The current anvil
shares the trunk's detail parameters; separate anvil, side and interior masks,
anisotropic noise and painting are follow-up work. These controls do not claim
to recover bands absent from a coarse sampled representation.

Support and `rho_max` are recomputed from the evaluated fields. A generated
field's envelope is a conservative sampling bound: enlarging macro warp expands
it, and decreasing warp need not shrink it. Frozen contract 2 separately stores
an optional immutable `clipping_envelope`. Freezing a Custom Recipe retains its
authored envelope as this hard mask, including a currently oversized envelope.
Detail edits preserve it exactly. It participates in `content_hash`, while an
expandable sampling bound does not. A clipping envelope must equal the evaluated
Recipe envelope, so the existing CPU/GPU final clip enforces it without another
packet or shader branch. This mask is supported for a single field without top
lobes or an anvil, matching the Custom Recipe path. A field without this mask
must contain its conservative primitive support, allowing only double-arithmetic
roundoff in the containment comparison. The two-metre support padding makes
this tolerance conservative; it never changes saved bounds. Full cuts and flat
bases are also applied as hard final constraints. Any invalid numeric state is rejected
before Document publication. The field remains an implicit density function,
not an SDF.

## Lifecycle and persistence

Completed, failed and cancelled generation outcomes are distinct. Only a
completed candidate can be frozen. A newer candidate cannot overwrite edits
made after its job started. Freeze and explicit replacement each use one existing
EditorSession Undo transaction. A failed/cancelled retry keeps the previous
successful candidate and fixed cloud. Discarding a draft candidate has a separate
Undo action; it never mutates the fixed document.

Schema 10 stores one `cloud.kind = frozen` source in the existing atomic JSON
artifact. All fields, curves, hierarchy, anvil, selected kind/unit/value, hashes,
settings, IDs and transforms are self-contained. No external asset paths or
references exist. File size, nesting, collection, enum, object shape and numeric
limits are checked; unexpected storage members are rejected. Existing atomic
temporary-write, flush and replace behavior keeps the previous complete artifact
on write or publication failure. Moving the file needs no companion assets.

Schemas 1 through 9 migrate explicitly to schema 10 without adding growth, wind
or a frozen state. Frozen contract 1 is migrated explicitly after its payload
and content hashes have been verified using the original canonical format.
Version 1 did not record envelope intent. Migration uses only saved evaluated
data: a single field whose envelope does not contain conservative primitive
support retains that envelope as an immutable clip; conservative old bounds
retain their previous expandable sampling behavior. An oversized authored crop
cannot be distinguished from a sampling bound in version 1. Geometry, detail,
support and provenance stay unchanged, and both hashes are updated for contract
2. This process needs neither provenance nor an available generation version.
Other unknown frozen contracts are rejected. A recognized frozen contract with
an unknown generation implementation remains loadable, keeps its provenance
intact, and disables regeneration. Saved-data validation and GPU cache
reconstruction do not regenerate the fixed geometry.

## Evidence and scope

CPU tests compare selected and frozen density for calm/wind, top hierarchy and
anvil combinations; verify independent developments; check exact protected
lower regions; and prove geometry identity, stable origins and job counts across
detail, view, optics and cache-key changes. Persistence tests cover exact
round-trip, all initial-source variants, unsigned 64-bit IDs, unavailable
generation versions, missing provenance, invalid contracts and sizes, corrupted
checksums, atomic failure, fixed custom clipping boundaries, expandable generated
bounds and an unchanged schema10/frozen-v1 fixture from the previous serializer.

The native application lifecycle test shares the same asynchronous launch and
Freeze adoption functions as the buttons. It generates and adopts, edits top
detail and camera/light, cancels and retries, explicitly adopts another stage,
checks Undo/Redo, then saves and reloads without another generation job. Actual
Windows captures and GPU readback evidence must be attached to the integration
PR. WARP success does not establish RTX performance or natural-cloud acceptance.
