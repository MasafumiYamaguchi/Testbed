#include "white/density_export.hpp"
#include "white/build_info.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace white {
namespace {
using Json=nlohmann::json;
static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Json vec(Vec3 p){return Json::array({p.x,p.y,p.z});}
Json bounds(Bounds b){return {{"min",vec(b.min)},{"max",vec(b.max)}};}
std::uint64_t hash(const Json& value){
    std::uint64_t result=14695981039346656037ull;
    for(unsigned char byte:value.dump())result=(result^byte)*1099511628211ull;
    return result;
}
Scene checked_scene(Scene scene){
    require_valid(scene);
    if(!scene.frozen)throw std::invalid_argument("Density export requires an already frozen state");
    // A future Native model may validate successfully while adding paint or
    // other density inputs this export contract has not adopted. Do not silently
    // widen version 1 when the application's current version constants change.
    if(scene.schema_version!=11||scene.algorithm_version!=3||scene.frozen->contract_version!=2||
       scene.finish_stack.contract_version!=1)
        throw std::invalid_argument("Native density contracts are unsupported by export contract 1");
    return scene;
}
DensityExportSettings checked_settings(DensityExportSettings settings){
    if(!std::isfinite(settings.voxel_size_m)||settings.voxel_size_m<=0||
       settings.padding_voxels<1||settings.padding_voxels>1024)
        throw std::invalid_argument("Density export requires positive finite voxel size and 1..1024 padding voxels");
    return settings;
}
void check_transform(const DensityExportLayout& layout,Vec3 point){
    if(!finite(point)||!finite(layout.index_zero_world)||
       !std::isfinite(layout.voxel_size_m)||layout.voxel_size_m<=0)
        throw std::invalid_argument("Invalid density export coordinate transform");
}
Json native_authority(const Scene& scene){
    const auto native=Json::parse(scene_json(scene));
    return {{"scene_schema_version",scene.schema_version},{"scene_algorithm_version",scene.algorithm_version},{"cloud",native.at("cloud")}};
}
std::uint64_t finishing_hash(const Scene& scene){
    const auto cloud=Json::parse(scene_json(scene)).at("cloud");
    Json detail=Json::array();
    for(const auto& field:cloud.at("source").at("fields"))
        detail.push_back({{"development_id",field.at("development_id")},{"layers",field.at("layers")},
            {"detail_seed",field.at("recipe").at("detail_seed")},{"noise",field.at("recipe").at("noise")}});
    return hash({{"contract_version",density_export_contract_version},{"detail",detail},{"finishing",cloud.at("finishing")}});
}
Json settings_json(DensityExportSettings settings,const DensityExportLayout& layout){
    return {{"voxel_size_m",settings.voxel_size_m},{"padding_voxels",settings.padding_voxels},
        {"lattice_anchor_m",{0,0,0}},{"world_bounds_m",bounds(layout.world_bounds)},
        {"index_zero_world_m",vec(layout.index_zero_world)},{"extent",layout.extent},
        {"sample_count",std::to_string(layout.sample_count)},{"sampling","center_point"},
        {"interpolation","trilinear"},{"zero_threshold",0.0}};
}
std::uint64_t request_fingerprint(std::uint64_t snapshot,DensityExportSettings settings,const DensityExportLayout& layout){
    return hash({{"contract_version",density_export_contract_version},{"snapshot_hash",std::to_string(snapshot)},
        {"export_settings",settings_json(settings,layout)}});
}
std::string metadata(const Scene& scene,DensityExportSettings settings,const DensityExportLayout& layout,
                     std::uint64_t snapshot,std::uint64_t finish,std::uint64_t request){
    const auto& fixed=*scene.frozen;const auto cloud=Json::parse(scene_json(scene)).at("cloud");
    Json generation_settings=nullptr;
    if(fixed.provenance)generation_settings=cloud.at("source").at("provenance").at("settings");
    Json result={
        {"contract","white.single_state_density"},{"contract_version",density_export_contract_version},
        {"producer",{{"application",app_name},{"version",version},{"commit",WHITE_COMMIT}}},
        {"grid",{{"name","density"},{"type","FloatGrid"},{"class","fog_volume"},{"background",0.0},
            {"value","nonnegative_dimensionless_rho"},{"is_sdf",false},{"active_policy","strictly_positive_float_values"}}},
        {"coordinates",{{"unit","metre"},{"handedness","right"},{"up_axis","+Y"},
            {"index_axes",{"+X","+Y","+Z"}},{"object_transform_baked",true},
            {"index_to_world","index_zero_world_m + voxel_size_m * index"},
            {"source_world_support_m",bounds(layout.source_world_support)}}},
        {"snapshot",{{"id",std::to_string(fixed.id)},{"hash_algorithm","canonical_json_fnv1a64"},
            {"hash",std::to_string(snapshot)},{"frozen_content_hash",std::to_string(fixed.content_hash)},
            {"frozen_payload_hash",std::to_string(fixed.payload_hash)},
            {"density_input_hash",std::to_string(density_input_hash(scene))},
            {"finishing_snapshot_hash",std::to_string(finish)},
            {"frozen_contract_version",fixed.contract_version},
            {"finish_stack_contract_version",scene.finish_stack.contract_version},
            {"scene_schema_version",scene.schema_version},{"scene_algorithm_version",scene.algorithm_version},
            {"selection",{{"kind",fixed.selection_kind},{"unit",fixed.selection_unit},{"value",fixed.selection_value}}}}},
        {"generation",{{"version",fixed.generation_version},{"input_hash",std::to_string(fixed.generation_input_hash)},
            {"provenance_available",bool(fixed.provenance)},{"regeneration_available",frozen_can_regenerate(fixed)},
            {"settings",generation_settings},{"evaluation_during_export",false}}},
        {"supported_finishing",{{"frozen_detail",true},{"ordered_cut_density_protection",true},{"paint",false}}},
        {"optics_recommendation",{{"extinction_scale_per_m",fixed.optics.extinction_scale},{"albedo",fixed.optics.albedo},
            {"phase","Henyey-Greenstein"},{"g",fixed.optics.g},
            {"phase_cosine","dot(incoming_photon_direction,outgoing_photon_direction)"},
            {"sigma_t","extinction_scale_per_m * rho"},{"scale_compensation_applied",false}}},
        {"export_settings",settings_json(settings,layout)},{"export_request_hash",std::to_string(request)},
        {"authority","native_project"},{"restores_native_editing_history",false},
        {"dcc_metadata_auto_application_assumed",false}
    };
    return result.dump(2)+"\n";
}
}

DensityExportLayout density_export_layout(Bounds support,DensityExportSettings settings){
    settings=checked_settings(settings);
    if(!finite(support.min)||!finite(support.max)||support.min.x>=support.max.x||
       support.min.y>=support.max.y||support.min.z>=support.max.z)
        throw std::invalid_argument("Invalid density export world support");
    DensityExportLayout result;result.source_world_support=support;result.voxel_size_m=settings.voxel_size_m;
    result.padding_voxels=settings.padding_voxels;
    const double h=settings.voxel_size_m,padding=settings.padding_voxels;
    const std::array<double,3> minimum{support.min.x,support.min.y,support.min.z},maximum{support.max.x,support.max.y,support.max.z};
    std::array<double,3> lower{},upper{},origin{};
    for(unsigned axis=0;axis<3;++axis){
        double lo=std::floor(minimum[axis]/h),hi=std::ceil(maximum[axis]/h);
        // Bound integer arithmetic before conversion and retain half-cell
        // centers. Fine grids far from the origin must fail, not silently drift.
        if(!std::isfinite(lo)||!std::isfinite(hi)||std::abs(lo)>0x1p50||std::abs(hi)>0x1p50)
            throw std::invalid_argument("Density export lattice origin exceeds double coordinate precision");
        if(lo*h>minimum[axis])--lo;
        if(hi*h<maximum[axis])++hi;
        lo-=padding;hi+=padding;
        const double count=hi-lo;
        if(count<1||count>std::numeric_limits<std::int32_t>::max())
            throw std::invalid_argument("Density export extent exceeds signed 32-bit index capacity");
        result.extent[axis]=static_cast<std::uint32_t>(count);
        lower[axis]=lo*h;upper[axis]=hi*h;origin[axis]=std::fma(lo,h,h*.5);
        const double second=std::fma(1,h,origin[axis]);
        const double last=std::fma(count-1,h,origin[axis]),previous=std::fma(count-2,h,origin[axis]);
        if(!std::isfinite(lower[axis])||!std::isfinite(upper[axis])||!std::isfinite(origin[axis])||
           !(lower[axis]<origin[axis]&&origin[axis]<std::fma(lo+1,h,0.0)&&
             origin[axis]<second&&previous<last&&last<upper[axis]))
            throw std::invalid_argument("Density export voxel centers are not representable");
    }
    result.world_bounds={{lower[0],lower[1],lower[2]},{upper[0],upper[1],upper[2]}};
    result.index_zero_world={origin[0],origin[1],origin[2]};
    result.sample_count=checked_volume_bytes(result.extent[0],result.extent[1],result.extent[2],sizeof(float))/sizeof(float);
    return result;
}
Vec3 export_index_to_world(const DensityExportLayout& layout,Vec3 index){
    check_transform(layout,index);const auto h=layout.voxel_size_m;const auto o=layout.index_zero_world;
    const Vec3 result{std::fma(index.x,h,o.x),std::fma(index.y,h,o.y),std::fma(index.z,h,o.z)};
    if(!finite(result))throw std::invalid_argument("Density export world coordinate overflow");
    return result;
}
Vec3 export_world_to_index(const DensityExportLayout& layout,Vec3 world){
    check_transform(layout,world);const auto h=layout.voxel_size_m;const auto o=layout.index_zero_world;
    const Vec3 result{(world.x-o.x)/h,(world.y-o.y)/h,(world.z-o.z)/h};
    if(!finite(result))throw std::invalid_argument("Density export index coordinate overflow");
    return result;
}
DensityExportSnapshot::DensityExportSnapshot(Scene scene,DensityExportSettings settings)
    :scene_(checked_scene(std::move(scene))),settings_(checked_settings(settings)),evaluator_(scene_),
     layout_(density_export_layout(evaluator_.world_support(),settings_)),snapshot_hash_(hash(native_authority(scene_))),
     finishing_hash_(finishing_hash(scene_)),request_hash_(request_fingerprint(snapshot_hash_,settings_,layout_)),
     metadata_(metadata(scene_,settings_,layout_,snapshot_hash_,finishing_hash_,request_hash_)){}
double DensityExportSnapshot::density_at_world(Vec3 point)const{
    if(!finite(point))throw std::invalid_argument("Nonfinite density export world position");
    const double value=evaluator_.at(world_to_local(scene_.frozen->transform,point));
    if(!std::isfinite(value)||value<0)throw std::runtime_error("Invalid final export density");
    return value==0?0:value;
}
float DensityExportSnapshot::sample(std::array<std::int32_t,3> index)const{
    for(unsigned axis=0;axis<3;++axis)if(index[axis]<0||std::uint32_t(index[axis])>=layout_.extent[axis])return 0;
    const auto world=export_index_to_world(layout_,{double(index[0]),double(index[1]),double(index[2])});
    const float value=static_cast<float>(density_at_world(world));
    if(!std::isfinite(value))throw std::runtime_error("Final export density exceeds FloatGrid range");
    return value==0?0:value;
}
}
