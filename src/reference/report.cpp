#include "white/reference.hpp"
#include "white/density.hpp"
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <bit>
#include <iomanip>
#include <sstream>
namespace white {
std::string reference_report_json(const ReferenceRenderer& r){
    const auto& s=r.settings();const auto& t=r.statistics();const auto& grid=r.snapshot().grid();
    const bool finished=r.samples_completed()==s.samples;
    const std::string status=!t.complete()?"PARTIAL / approximate; safety or numerical failure":
        finished?"complete bounded reference":"in progress; requested sample budget incomplete";
    std::uint64_t hash=14695981039346656037ull;
    auto append=[&](std::uint64_t x,unsigned bytes){for(unsigned i=0;i<bytes;++i){hash^=(x>>(i*8))&255;hash*=1099511628211ull;}};
    for(auto n:grid.extent)append(n,4);
    for(double x:{grid.local_bounds.min.x,grid.local_bounds.min.y,grid.local_bounds.min.z,grid.local_bounds.max.x,grid.local_bounds.max.y,grid.local_bounds.max.z})append(std::bit_cast<std::uint64_t>(x),8);
    for(float x:r.snapshot().density())append(std::bit_cast<std::uint32_t>(x),4);
    double bake_max=0,bake_square=0;constexpr unsigned probes=9;const DensityField procedural(r.snapshot().scene().cloud);
    for(unsigned z=0;z<probes;++z)for(unsigned y=0;y<probes;++y)for(unsigned x=0;x<probes;++x){
        const auto lo=grid.local_bounds.min,hi=grid.local_bounds.max;
        const Vec3 local{lo.x+(hi.x-lo.x)*(x+.5)/probes,lo.y+(hi.y-lo.y)*(y+.5)/probes,lo.z+(hi.z-lo.z)*(z+.5)/probes};
        const double error=std::abs(procedural.at(local)-r.snapshot().density_at_world(local_to_world(r.snapshot().scene().cloud.transform,local)));
        bake_max=std::max(bake_max,error);bake_square+=error*error;
    }
    std::ostringstream hash_text;hash_text<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
    nlohmann::json out={{"integrator","CPU delta tracking + directional sun NEE + independent ratio tracking"},
        {"integrator_version",1},{"mode",s.mode==ReferenceMode::single_scattering?"single scattering":"multiple scattering"},
        {"status",status},
        {"complete",t.complete()&&finished},{"seed",std::to_string(s.seed)},{"requested_samples_per_pixel",s.samples},{"completed_sample_planes",r.samples_completed()},
        {"pixel_jitter",s.pixel_jitter},{"maximum_bounces",s.bounce_limit},{"roulette_start",s.roulette_start},
        {"event_limit_per_tracking_call",s.event_limit},{"attempted_paths",t.attempted_paths},{"complete_paths",t.complete_paths},
        {"event_limited_paths",t.event_limited_paths},{"bounce_limited_paths",t.bounce_limited_paths},{"numerical_failures",t.numerical_failures},
        {"collision_events",t.collision_events},{"tracking_events",t.tracking_events},{"roulette_terminations",t.roulette_terminations},
        {"tracking_event_counter_scope","candidate events from successfully completed tracking calls; a throwing call's consumed work is not included"},
        {"grid_extent",grid.extent},{"grid_values_and_layout_fnv1a64",hash_text.str()},
        {"density_bake_probe_comparison",{{"count",probes*probes*probes},{"maximum_absolute",bake_max},{"rms",std::sqrt(bake_square/(probes*probes*probes))},{"meaning","procedural recipe vs frozen trilinear grid at fixed 9x9x9 interior probes; not a global bound or radiance error"}}},
        {"all_attempted_paths_complete",t.complete()},{"requested_sample_budget_complete",r.samples_completed()==s.samples},
        {"medium","immutable R32F voxel centers, trilinear clamp, original hard base/cut/envelope constraints"},
        {"background","primary camera transmittance times fixed RGB only; no background illumination or secondary escape radiance"},
        {"ray_extent","primary camera near/far clipping only; sun visibility and secondary transport extend to the frozen medium boundary"},
        {"sun","one directional delta light, exclusively next-event estimation; phase-sampled sun hit contributes zero"},
        {"throughput","sigma_s/sigma_t=albedo each real extinction event, HG/pdf, roulette survival division"},
        {"roulette","after configured bounce: survival clamp(beta, 0.05, 0.95), survivor beta /= survival"},
        {"random_streams",{{"pixel",0},{"primary_transmittance",10},{"delta",20},{"sun_ratio",30},{"roulette",40},{"phase",50}}},
        {"mean_radiance",r.mean_radiance()},{"mean_pixel_estimator_variance",r.mean_estimator_variance()},
        {"error_categories",{{"bake","finite frozen density grid; not measured by MC standard error"},{"optical_model","grey scalar extinction/albedo, RGB sun, HG, no background medium illumination"},{"numerical","double integration/random walks over R32F density; compare deterministic quadrature independently"},{"monte_carlo","reported variance of each pixel sample mean; no denoising or adaptive sampling"}}}};
    return out.dump(2);
}
}
