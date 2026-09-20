#include "white/export_resources.hpp"
#include "white/generation.hpp"
#include "white/persistence.hpp"
#include <nlohmann/json.hpp>
#include <charconv>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
std::uint64_t unsigned_value(std::string_view text){
    std::uint64_t value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size())throw std::invalid_argument("Expected an unsigned integer byte/count value");
    return value;
}
std::uint32_t small_value(std::string_view text){
    const auto value=unsigned_value(text);if(value>std::numeric_limits<std::uint32_t>::max())throw std::invalid_argument("Count exceeds uint32");
    return static_cast<std::uint32_t>(value);
}
double positive_value(std::string_view text){
    double value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||!std::isfinite(value)||value<=0)
        throw std::invalid_argument("Voxel size must be a positive finite number in metres");
    return value;
}
constexpr const char* usage=R"(Usage: white_export_plan --recipe FILE --voxel-size METRES
  --snapshot-reservation BYTES --limits GRID GPU STAGING QUEUE SNAPSHOT TOTAL
  [--tile X Y Z] [--slots GPU STAGING QUEUE] [--row-alignment BYTES] [--padding N]

Prints a JSON resource preflight for an already frozen Native project.
Tile defaults: 32 32 32. Slots default: 0 0 0 (CPU model); use three
positive counts for a staged GPU reservation model. Row alignment defaults
to 256 bytes, padding to 1. All limits are explicit unsigned byte counts.
Exit 0: modeled reservations fit; 2: exceeded limit; 1: invalid input/error.
No grid, GPU transfer or VDB allocation/write is performed. OpenVDB tree,
allocator/driver overhead and actual peak memory remain unestimated.
)";
}
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string_view(argv[1])=="--help"){std::cout<<usage;return 0;}
        std::string recipe;white::DensityExportSettings settings;white::ExportResourceOptions options;white::ExportResourceBytes limits;
        std::set<std::string_view> seen;
        for(int i=1;i<argc;++i){
            const std::string_view key=argv[i];if(!seen.insert(key).second)throw std::invalid_argument("Repeated command-line option");
            auto next=[&]()->std::string_view{if(i+1>=argc)throw std::invalid_argument("Missing command-line option value");return argv[++i];};
            if(key=="--recipe")recipe=next();
            else if(key=="--voxel-size")settings.voxel_size_m=positive_value(next());
            else if(key=="--padding")settings.padding_voxels=small_value(next());
            else if(key=="--tile")for(auto& n:options.tile_extent)n=small_value(next());
            else if(key=="--slots"){options.gpu_tile_slots=small_value(next());options.transfer_staging_slots=small_value(next());options.cpu_readback_slots=small_value(next());}
            else if(key=="--row-alignment")options.transfer_row_alignment=small_value(next());
            else if(key=="--snapshot-reservation")options.retained_snapshot_bytes=unsigned_value(next());
            else if(key=="--limits"){
                limits.cpu_dense_grid=unsigned_value(next());limits.gpu_tiles=unsigned_value(next());limits.transfer_staging=unsigned_value(next());
                limits.cpu_readback_queue=unsigned_value(next());limits.retained_snapshot=unsigned_value(next());limits.accounted_total=unsigned_value(next());
            }else throw std::invalid_argument("Unknown command-line option");
        }
        for(const std::string_view required:{"--recipe","--voxel-size","--snapshot-reservation","--limits"})
            if(!seen.contains(required))throw std::invalid_argument("Missing required option: "+std::string(required));
        const auto before=white::generation_job_count();
        const auto recipe_path=std::filesystem::path(std::u8string(recipe.begin(),recipe.end()));
        const white::DensityExportSnapshot snapshot(white::read_scene(recipe_path),settings);
        const auto plan=white::plan_export_resources(snapshot,options,limits);
        const auto jobs=white::generation_job_count()-before;if(jobs)throw std::logic_error("Resource planning unexpectedly invoked generation");
        auto json=nlohmann::json::parse(white::export_resource_plan_json(plan));json["generation_jobs"]=jobs;
        std::cout<<json.dump(2)<<'\n';return plan.reservations_fit()?0:2;
    }catch(const std::exception& error){
        std::cout<<nlohmann::json({{"error",error.what()},{"allocates_export_buffers",false},{"writer_ready",false}}).dump(2)<<'\n';return 1;
    }
}
