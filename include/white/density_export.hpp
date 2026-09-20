#pragma once
#include "white/developed_scene.hpp"
#include <string>

namespace white {
inline constexpr std::uint32_t density_export_contract_version=1;
struct DensityExportSettings {
    double voxel_size_m=1;
    std::uint32_t padding_voxels=1;
    bool operator==(const DensityExportSettings&)const=default;
};
struct DensityExportLayout {
    Bounds source_world_support;
    Bounds world_bounds; // Outer faces, including explicit zero padding.
    Vec3 index_zero_world; // Center of integer index (0,0,0), in metres.
    std::array<std::uint32_t,3> extent{};
    double voxel_size_m=1;
    std::uint32_t padding_voxels=1;
    std::uint64_t sample_count=0;
};
// Axis-aligned isotropic world lattice; native object TRS is baked into samples.
// This is a contract/CPU sampling API. It does not link or write OpenVDB.
DensityExportLayout density_export_layout(Bounds world_support,DensityExportSettings);
Vec3 export_index_to_world(const DensityExportLayout&,Vec3 index);
Vec3 export_world_to_index(const DensityExportLayout&,Vec3 world);

// Owned, immutable job input. Capture requires an already frozen authority;
// generation candidates and subsequent Document edits cannot enter this value.
class DensityExportSnapshot {
public:
    explicit DensityExportSnapshot(Scene,DensityExportSettings={});
    const Scene& scene()const{return scene_;}
    const DensityExportLayout& layout()const{return layout_;}
    const DensityExportSettings& settings()const{return settings_;}
    std::uint64_t snapshot_hash()const{return snapshot_hash_;}
    std::uint64_t finishing_snapshot_hash()const{return finishing_hash_;}
    std::uint64_t request_hash()const{return request_hash_;}
    const std::string& metadata_json()const{return metadata_;}
    double density_at_world(Vec3)const;
    // Exact center point evaluation, rounded once to FloatGrid's float. Any
    // integer outside [0,extent) is zero background. No threshold or volume mean.
    float sample(std::array<std::int32_t,3> index)const;
private:
    const Scene scene_;
    const DensityExportSettings settings_;
    const SceneDensityEvaluator evaluator_;
    const DensityExportLayout layout_;
    const std::uint64_t snapshot_hash_,finishing_hash_,request_hash_;
    const std::string metadata_;
};
}
