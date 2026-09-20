#pragma once
#include "white/dense_cache.hpp"
#include "white/majorant.hpp"
#include <span>
#include <stdexcept>
#include <string>

namespace white {
// A published reference is an owning, immutable copy. Density is R32F at voxel
// centers, linearly interpolated/clamped within grid.local_bounds, followed by
// the preview's hard envelope/base/cut mask. No procedural edits, post-grid noise,
// or approximate preview lighting caches enter this medium.
class TrackingSnapshot {
public:
    TrackingSnapshot(Scene,GridLayout,std::vector<float>);
    TrackingSnapshot(Scene,GridLayout,std::vector<float>,Majorant);
    const Scene& scene() const {return scene_;}
    const GridLayout& grid() const {return grid_;}
    std::span<const float> density() const {return density_;}
    const Majorant& majorant() const {return majorant_;}
    double density_at_world(Vec3) const;
private:
    Scene scene_;
    GridLayout grid_;
    std::vector<float> density_;
    Majorant majorant_;
};

enum class TrackingMode {global,local};
struct TrackingOptions {
    TrackingMode mode=TrackingMode::local;
    std::uint64_t event_budget=1000000;
};
// Direction is unit length in WORLD space. Parameter distance, begin and end
// are world metres, including when the density grid has nonuniform TRS scale.
struct TrackingRay {
    Vec3 origin_world{},direction_world{0,0,1};
    double begin=0,end=10000;
};
struct TrackingRngKey {
    std::uint64_t seed=0;
    std::uint32_t pixel=0,sample=0,bounce=0,stream=0;
};
// Counter-based open (0,1) uniform. Dimensions 0/1 are delta distance/accept,
// 2 is ratio distance; higher dimensions are reserved for reference sampling.
double tracking_uniform(const TrackingRngKey&,std::uint64_t event,std::uint32_t dimension);
struct TrackingInterval {double entry,exit,majorant_extinction;};
std::vector<TrackingInterval> tracking_intervals(const TrackingSnapshot&,const TrackingRay&,TrackingMode);
Vec3 tracking_local_position(const TrackingSnapshot&,const TrackingRay&,double distance);
double tracking_extinction(const TrackingSnapshot&,const TrackingRay&,double distance);

enum class TrackingFailureReason {event_budget,numerical,invalid_majorant};
class TrackingFailure : public std::runtime_error {
public:
    explicit TrackingFailure(const std::string& message):std::runtime_error(message){}
    TrackingFailure(TrackingFailureReason reason,const std::string& message):std::runtime_error(message),reason_(reason){}
    TrackingFailureReason reason() const {return reason_;}
private:
    TrackingFailureReason reason_=TrackingFailureReason::numerical;
};
enum class TrackingEvent {escape,collision};
struct DeltaTrackResult {
    TrackingEvent event=TrackingEvent::escape;
    double distance=0;
    Vec3 position_world{};
    double density=0,extinction=0;
    std::uint64_t candidate_events=0,null_events=0,intervals=0,random_draws=0;
};
// Samples extinction collisions. Scattering/absorption is the caller's albedo
// decision. Budget exhaustion, numerical stalls and invalid majorants throw;
// they are never interpreted as an escaped, unbiased reference sample.
DeltaTrackResult delta_track(const TrackingSnapshot&,const TrackingRay&,const TrackingRngKey&,TrackingOptions={});
}
