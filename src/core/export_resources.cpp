#include "white/export_resources.hpp"
#include "white/build_info.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace white {
namespace {
constexpr std::array<std::string_view,6> names{
    "cpu_dense_grid","gpu_tiles","transfer_staging","cpu_readback_queue","retained_snapshot","accounted_total"};
std::array<std::uint64_t,6> amounts(const ExportResourceBytes& b){
    return {b.cpu_dense_grid,b.gpu_tiles,b.transfer_staging,b.cpu_readback_queue,b.retained_snapshot,b.accounted_total};
}
std::uint64_t add(std::uint64_t a,std::uint64_t b){
    if(b>std::numeric_limits<std::uint64_t>::max()-a)throw std::overflow_error("Export resource byte sum exceeds uint64");
    return a+b;
}
std::uint64_t multiply(std::uint64_t a,std::uint64_t b){
    if(b&&a>std::numeric_limits<std::uint64_t>::max()/b)throw std::overflow_error("Export resource byte product exceeds uint64");
    return a*b;
}
std::uint64_t align_up(std::uint64_t bytes,std::uint32_t alignment){
    return multiply(add(bytes,alignment-1)/alignment,alignment);
}
}
bool ExportResourcePlan::reservations_fit()const{
    const auto usage=amounts(required),budget=amounts(limits);
    for(unsigned i=0;i<usage.size();++i)if(usage[i]>budget[i])return false;
    return true;
}
ExportResourcePlan plan_export_resources(const DensityExportSnapshot& snapshot,const ExportResourceOptions& options,const ExportResourceBytes& limits){
    for(const auto n:options.tile_extent)if(!n)throw std::invalid_argument("Export tile extents must be positive");
    if(!options.transfer_row_alignment||(options.transfer_row_alignment&(options.transfer_row_alignment-1)))
        throw std::invalid_argument("Transfer row alignment must be a positive power of two bytes");
    const bool cpu=options.gpu_tile_slots==0&&options.transfer_staging_slots==0&&options.cpu_readback_slots==0;
    const bool staged=options.gpu_tile_slots>0&&options.transfer_staging_slots>0&&options.cpu_readback_slots>0;
    if(!cpu&&!staged)throw std::invalid_argument("Use zero slots for the CPU model or positive GPU/staging/readback slots for the staged model");
    const auto metadata_payload=add(snapshot.metadata_json().size(),1); // Include the string terminator.
    // Credit the entire string object as possible inline character storage.
    // Any remaining bytes must lie outside it, independent of SSO strategy.
    // Other Scene/evaluator dynamic allocations are deliberately not guessed.
    const auto external_metadata_min=metadata_payload>sizeof(std::string)?metadata_payload-sizeof(std::string):0;
    const auto retained_minimum=add(sizeof(DensityExportSnapshot),external_metadata_min);
    if(options.retained_snapshot_bytes<retained_minimum)
        throw std::invalid_argument("Retained snapshot reservation is below the known object/metadata payload lower bound");
    ExportResourcePlan plan;plan.snapshot_hash=snapshot.snapshot_hash();plan.finishing_snapshot_hash=snapshot.finishing_snapshot_hash();
    plan.export_request_hash=snapshot.request_hash();plan.layout=snapshot.layout();plan.options=options;plan.limits=limits;
    plan.snapshot_object_bytes=sizeof(DensityExportSnapshot);plan.snapshot_metadata_bytes=snapshot.metadata_json().size();
    plan.snapshot_known_lower_bound_bytes=retained_minimum;
    for(unsigned axis=0;axis<3;++axis){
        plan.tile_extent[axis]=std::min(options.tile_extent[axis],plan.layout.extent[axis]);
        plan.tile_grid[axis]=1+(plan.layout.extent[axis]-1)/plan.tile_extent[axis];
    }
    plan.tile_count=checked_volume_bytes(plan.tile_grid[0],plan.tile_grid[1],plan.tile_grid[2],1);
    plan.packed_tile_bytes=checked_volume_bytes(plan.tile_extent[0],plan.tile_extent[1],plan.tile_extent[2],sizeof(float));
    if(staged){
        plan.transfer_row_pitch_bytes=align_up(multiply(plan.tile_extent[0],sizeof(float)),options.transfer_row_alignment);
        plan.transfer_tile_bytes=multiply(multiply(plan.transfer_row_pitch_bytes,plan.tile_extent[1]),plan.tile_extent[2]);
    }
    auto& bytes=plan.required;
    bytes.cpu_dense_grid=checked_volume_bytes(plan.layout.extent[0],plan.layout.extent[1],plan.layout.extent[2],sizeof(float));
    bytes.gpu_tiles=multiply(plan.packed_tile_bytes,options.gpu_tile_slots);
    bytes.transfer_staging=multiply(plan.transfer_tile_bytes,options.transfer_staging_slots);
    bytes.cpu_readback_queue=multiply(plan.packed_tile_bytes,options.cpu_readback_slots);
    bytes.retained_snapshot=options.retained_snapshot_bytes;
    for(const auto value:{bytes.cpu_dense_grid,bytes.gpu_tiles,bytes.transfer_staging,bytes.cpu_readback_queue,bytes.retained_snapshot})
        bytes.accounted_total=add(bytes.accounted_total,value);
    return plan;
}
void require_export_resource_reservations(const ExportResourcePlan& plan){
    const auto usage=amounts(plan.required),budget=amounts(plan.limits);
    std::string exceeded;
    for(unsigned i=0;i<usage.size();++i)if(usage[i]>budget[i]){if(!exceeded.empty())exceeded+=", ";exceeded+=names[i];}
    if(!exceeded.empty())throw std::invalid_argument("Export resource reservations exceed caller limits: "+exceeded);
}
std::string export_resource_plan_json(const ExportResourcePlan& plan){
    using Json=nlohmann::json;Json resources=Json::object(),exceeded=Json::array();
    const auto usage=amounts(plan.required),budget=amounts(plan.limits);
    for(unsigned i=0;i<usage.size();++i){const bool fits=usage[i]<=budget[i];
        resources[std::string(names[i])]={{"requested_bytes",std::to_string(usage[i])},{"limit_bytes",std::to_string(budget[i])},{"fits",fits}};
        if(!fits)exceeded.push_back(names[i]);
    }
    Json result={{"contract","white.density_export_resource_plan"},{"contract_version",export_resource_plan_version},
        {"snapshot_hash",std::to_string(plan.snapshot_hash)},{"finishing_snapshot_hash",std::to_string(plan.finishing_snapshot_hash)},
        {"export_request_hash",std::to_string(plan.export_request_hash)},
        {"sampling","center_point"},{"voxel_size_m",plan.layout.voxel_size_m},{"padding_voxels",plan.layout.padding_voxels},
        {"extent",plan.layout.extent},{"voxel_count",std::to_string(plan.layout.sample_count)},
        {"index_zero_world_m",{plan.layout.index_zero_world.x,plan.layout.index_zero_world.y,plan.layout.index_zero_world.z}},
        {"world_bounds_m",{{"min",{plan.layout.world_bounds.min.x,plan.layout.world_bounds.min.y,plan.layout.world_bounds.min.z}},
            {"max",{plan.layout.world_bounds.max.x,plan.layout.world_bounds.max.y,plan.layout.world_bounds.max.z}}}},
        {"tile",{{"requested_extent",plan.options.tile_extent},{"effective_extent",plan.tile_extent},{"grid",plan.tile_grid},
            {"count",std::to_string(plan.tile_count)},{"packed_payload_bytes",std::to_string(plan.packed_tile_bytes)},
            {"transfer_row_alignment_bytes",plan.options.transfer_row_alignment},
            {"transfer_row_pitch_bytes",std::to_string(plan.transfer_row_pitch_bytes)},{"transfer_payload_bytes",std::to_string(plan.transfer_tile_bytes)},
            {"gpu_slots",plan.options.gpu_tile_slots},{"transfer_staging_slots",plan.options.transfer_staging_slots},{"cpu_readback_slots",plan.options.cpu_readback_slots}}},
        {"retained_snapshot",{{"reservation_source","caller_supplied"},{"inline_object_bytes",std::to_string(plan.snapshot_object_bytes)},
            {"metadata_text_bytes",std::to_string(plan.snapshot_metadata_bytes)},
            {"known_payload_lower_bound_bytes",std::to_string(plan.snapshot_known_lower_bound_bytes)},{"dynamic_storage_measured",false}}},
        {"resources",resources},{"modeled_reservations_fit",plan.reservations_fit()},{"exceeded_categories",exceeded},
        {"accounting","requested buffer payloads plus caller snapshot reservation; not measured peak resident memory"},
        {"openvdb_tree_bytes",nullptr},{"allocator_and_driver_overhead_bytes",nullptr},{"vdb_output_file_bytes",nullptr},
        {"actual_memory_admission_established",false},{"writer_ready",false},{"allocates_export_buffers",false}
    };
    return result.dump(2)+"\n";
}
}
