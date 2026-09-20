#pragma once
#include "white/density_export.hpp"

namespace white {
inline constexpr std::uint32_t export_resource_plan_version=1;
struct ExportResourceOptions {
    std::array<std::uint32_t,3> tile_extent{32,32,32};
    // All zero means a direct CPU/dense-grid model. All positive describes a
    // staged GPU reservation model, without implementing a GPU export worker.
    std::uint32_t gpu_tile_slots=0,transfer_staging_slots=0,cpu_readback_slots=0;
    std::uint32_t transfer_row_alignment=256;
    // Explicit caller reservation for all retained snapshot/evaluator/asset
    // storage. This is not inferred from the on-disk JSON size or voxel count.
    std::uint64_t retained_snapshot_bytes=0;
};
struct ExportResourceBytes {
    std::uint64_t cpu_dense_grid=0,gpu_tiles=0,transfer_staging=0;
    std::uint64_t cpu_readback_queue=0,retained_snapshot=0,accounted_total=0;
};
struct ExportResourcePlan {
    std::uint64_t snapshot_hash=0,finishing_snapshot_hash=0,export_request_hash=0;
    DensityExportLayout layout;
    ExportResourceOptions options;
    std::array<std::uint32_t,3> tile_extent{},tile_grid{};
    std::uint64_t tile_count=0,packed_tile_bytes=0,transfer_row_pitch_bytes=0,transfer_tile_bytes=0;
    std::uint64_t snapshot_object_bytes=0,snapshot_metadata_bytes=0,snapshot_known_lower_bound_bytes=0;
    ExportResourceBytes required,limits;
    bool reservations_fit()const;
};
// No grid, tile, transfer, GPU or VDB allocation occurs here. The captured
// snapshot already exists; this reserves its continued retention separately.
// Values are requested payload bytes, not measured peak resident memory.
ExportResourcePlan plan_export_resources(const DensityExportSnapshot&,const ExportResourceOptions&,const ExportResourceBytes& limits);
void require_export_resource_reservations(const ExportResourcePlan&);
std::string export_resource_plan_json(const ExportResourcePlan&);
}
