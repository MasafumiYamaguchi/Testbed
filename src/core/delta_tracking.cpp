#include "white/delta_tracking.hpp"
#include "white/developed_scene.hpp"
#include "white/optics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace white {
namespace {
bool finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
void validate_input(const Scene& scene,const GridLayout& grid) {
    require_valid(scene);
    if(scene_density_requires_direct(scene))
        throw std::invalid_argument("Tracking snapshot does not support independent developments or active top lobes: independent hard-mask frozen-grid contract required");
    (void)index_to_local(grid,{}); // Validates bounds and all three extents.
}
void validate_majorant(const GridLayout& grid,std::span<const float> density,const Majorant& supplied) {
    const auto required=build_majorant(grid,density);
    if(supplied.density_grid.extent!=grid.extent||supplied.density_grid.local_bounds!=grid.local_bounds||supplied.levels.size()!=required.levels.size())
        throw TrackingFailure(TrackingFailureReason::invalid_majorant,"Tracking majorant grid/hierarchy mismatch");
    for(std::size_t l=0;l<required.levels.size();++l) {
        const auto& a=supplied.levels[l];const auto& b=required.levels[l];
        if(a.extent!=b.extent||a.maxima.size()!=b.maxima.size())throw TrackingFailure(TrackingFailureReason::invalid_majorant,"Tracking majorant level shape mismatch");
        for(std::size_t i=0;i<a.maxima.size();++i)
            if(!std::isfinite(a.maxima[i])||a.maxima[i]<b.maxima[i])
                throw TrackingFailure(TrackingFailureReason::invalid_majorant,"Tracking majorant violation at level "+std::to_string(l)+", brick "+std::to_string(i));
    }
}
void validate_ray(const TrackingRay& ray) {
    if(!finite(ray.origin_world)||!finite(ray.direction_world)||std::abs(dot(ray.direction_world,ray.direction_world)-1)>1e-9||
       !std::isfinite(ray.begin)||!std::isfinite(ray.end)||ray.begin<0||ray.end<ray.begin)
        throw std::invalid_argument("Tracking ray needs finite bounds and a unit world direction");
}
struct LocalRay {Vec3 origin,direction;};
LocalRay local_ray(const TrackingSnapshot& snapshot,const TrackingRay& ray) {
    auto linear=snapshot.scene().cloud.transform;linear.translation={};
    return {world_to_local(snapshot.scene().cloud.transform,ray.origin_world),world_to_local(linear,ray.direction_world)};
}
double conservative_extinction(float density,double scale) {
    const double exact=double(density)*scale;
    if(exact==0)return 0;
    // Eight nonnegative trilinear terms and their summation are rounded. This
    // expands the proposal bound, not acceptance, and preserves strict checks.
    const double bound=std::nextafter(exact*(1+32*std::numeric_limits<double>::epsilon()),std::numeric_limits<double>::infinity());
    if(!std::isfinite(bound))throw TrackingFailure("Tracking majorant extinction overflow");
    return bound;
}
std::uint64_t mix(std::uint64_t value) {
    value+=0x9e3779b97f4a7c15ULL;
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
}

TrackingSnapshot::TrackingSnapshot(Scene scene,GridLayout grid,std::vector<float> density)
    :scene_(std::move(scene)),grid_(grid),density_(std::move(density)) {
    validate_input(scene_,grid_);majorant_=build_majorant(grid_,density_);
}
TrackingSnapshot::TrackingSnapshot(Scene scene,GridLayout grid,std::vector<float> density,Majorant majorant)
    :scene_(std::move(scene)),grid_(grid),density_(std::move(density)),majorant_(std::move(majorant)) {
    validate_input(scene_,grid_);validate_majorant(grid_,density_,majorant_);
}
double TrackingSnapshot::density_at_world(Vec3 p) const {
    const auto local=world_to_local(scene_.cloud.transform,p);
    return hard_density_region(scene_.cloud,local)?sample_dense(grid_,density_,local):0;
}
double tracking_uniform(const TrackingRngKey& key,std::uint64_t event,std::uint32_t dimension) {
    // Ordered mixing keeps the semantic dimensions distinct, including the
    // complete 64-bit seed/event values; no mutable RNG state is shared.
    auto bits=mix(key.seed);
    bits=mix(bits^key.pixel);bits=mix(bits^key.sample);bits=mix(bits^key.bounce);
    bits=mix(bits^key.stream);bits=mix(bits^event);bits=mix(bits^dimension);
    // 52 bits plus half a bin remain strictly between zero and one in binary64.
    return (double(bits>>12)+0.5)*0x1p-52;
}
std::vector<TrackingInterval> tracking_intervals(const TrackingSnapshot& snapshot,const TrackingRay& ray,TrackingMode mode) {
    validate_ray(ray);
    if(mode!=TrackingMode::global&&mode!=TrackingMode::local)throw std::invalid_argument("Unknown tracking majorant mode");
    const auto local=local_ray(snapshot,ray);
    std::vector<TrackingInterval> result;
    const double scale=snapshot.scene().cloud.optics.extinction_scale;
    if(mode==TrackingMode::global) {
        const auto hit=intersect_bounds(local.origin,local.direction,snapshot.grid().local_bounds,ray.begin,ray.end);
        if(hit&&hit->exit>hit->entry)result.push_back({hit->entry,hit->exit,conservative_extinction(snapshot.majorant().levels.back().maxima[0],scale)});
    } else {
        const auto bricks=traverse_bricks(snapshot.grid(),local.origin,local.direction,ray.begin,ray.end);
        const auto& level=snapshot.majorant().levels.front();
        result.reserve(bricks.size());
        for(const auto& b:bricks) {
            const auto i=(std::size_t(b.brick[2])*level.extent[1]+b.brick[1])*level.extent[0]+b.brick[0];
            result.push_back({b.entry,b.exit,conservative_extinction(level.maxima[i],scale)});
        }
    }
    return result;
}
Vec3 tracking_local_position(const TrackingSnapshot& snapshot,const TrackingRay& ray,double distance) {
    if(!std::isfinite(distance))throw std::invalid_argument("Tracking sample distance is nonfinite");
    const auto local=local_ray(snapshot,ray);return local.origin+local.direction*distance;
}
double tracking_extinction(const TrackingSnapshot& snapshot,const TrackingRay& ray,double distance) {
    const auto local=tracking_local_position(snapshot,ray,distance);
    return hard_density_region(snapshot.scene().cloud,local)?sample_dense(snapshot.grid(),snapshot.density(),local)*snapshot.scene().cloud.optics.extinction_scale:0;
}
DeltaTrackResult delta_track(const TrackingSnapshot& snapshot,const TrackingRay& ray,const TrackingRngKey& key,TrackingOptions options) {
    const auto intervals=tracking_intervals(snapshot,ray,options.mode);
    DeltaTrackResult result;
    result.distance=intervals.empty()?ray.end:intervals.back().exit;
    result.position_world=ray.origin_world+ray.direction_world*result.distance;
    std::uint64_t proposal=0;
    for(const auto& segment:intervals) {
        ++result.intervals;
        if(segment.majorant_extinction==0)continue;
        double t=segment.entry;
        while(t<segment.exit) {
            if(proposal==std::numeric_limits<std::uint64_t>::max())throw TrackingFailure("Tracking RNG event counter exhausted");
            const auto event=proposal++;
            const double u=tracking_uniform(key,event,0);++result.random_draws;
            const double distance=-std::log1p(-u)/segment.majorant_extinction;
            // Overflow means that the free flight exits this finite interval.
            // Test before addition, so an overflowing sum is never published.
            if(distance>=segment.exit-t)break;
            const double next=t+distance;
            if(!std::isfinite(next)||!(next>t))throw TrackingFailure("Tracking free flight cannot advance numerically");
            t=next;
            if(result.candidate_events==options.event_budget)throw TrackingFailure(TrackingFailureReason::event_budget,"Tracking event budget exhausted; sample invalid");
            ++result.candidate_events;
            const double sigma=tracking_extinction(snapshot,ray,t);
            if(!std::isfinite(sigma)||sigma<0||sigma>segment.majorant_extinction)
                throw TrackingFailure(TrackingFailureReason::invalid_majorant,"Tracking sampled extinction violates majorant; sample invalid");
            const double accept=tracking_uniform(key,event,1);++result.random_draws;
            if(accept<sigma/segment.majorant_extinction) {
                result.event=TrackingEvent::collision;result.distance=t;
                result.position_world=ray.origin_world+ray.direction_world*t;
                result.extinction=sigma;result.density=sigma/snapshot.scene().cloud.optics.extinction_scale;
                return result;
            }
            ++result.null_events;
        }
    }
    return result;
}
}
