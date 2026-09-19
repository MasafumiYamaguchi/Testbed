#pragma once
#include "white/delta_tracking.hpp"
#include "white/hdr_export.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace white {
enum class ReferenceMode {single_scattering,multiple_scattering};
enum class ReferenceStatus {complete,event_limit,bounce_limit,numerical_failure};
struct ReferenceSettings {
    unsigned width=32,height=18,samples=64;
    std::uint64_t seed=42;
    ReferenceMode mode=ReferenceMode::multiple_scattering;
    unsigned bounce_limit=64,roulette_start=3;
    std::uint64_t event_limit=100000;
    bool pixel_jitter=true;
};
struct ReferenceSample {
    Vec3 radiance{};
    double transmittance=0;
    ReferenceStatus status=ReferenceStatus::complete;
    std::uint64_t collision_events=0,tracking_events=0,roulette_terminations=0;
};
// The background is seen only through the original camera ray. It does not
// illuminate the medium. Directional sunlight is added exclusively by NEE.
inline constexpr Vec3 reference_background{.015,.022,.035};
ReferenceSample reference_sample(const TrackingSnapshot&,const TrackingRay&,const ReferenceSettings&,TrackingRngKey);
struct ReferenceStatistics {
    std::uint64_t attempted_paths=0,complete_paths=0,event_limited_paths=0,bounce_limited_paths=0,numerical_failures=0;
    std::uint64_t collision_events=0,tracking_events=0,roulette_terminations=0;
    bool complete()const{return attempted_paths==complete_paths;}
};
class ReferenceRenderer {
public:
    ReferenceRenderer(TrackingSnapshot,ReferenceSettings);
    // Advances by complete sample planes; reproducible across chunk sizes.
    void advance(unsigned sample_planes);
    HdrImage image()const;
    double mean_radiance()const;
    double mean_estimator_variance()const;
    unsigned samples_completed()const{return completed_;}
    const ReferenceSettings& settings()const{return settings_;}
    const TrackingSnapshot& snapshot()const{return snapshot_;}
    const ReferenceStatistics& statistics()const{return statistics_;}
private:
    TrackingSnapshot snapshot_;
    ReferenceSettings settings_;
    unsigned completed_=0;
    std::vector<double> sum_,sum_square_;
    ReferenceStatistics statistics_;
};
// Independent deterministic midpoint integration of the SAME frozen trilinear
// field; this separates quadrature error from Monte Carlo variance/bake error.
ReferenceSample reference_raymarch(const TrackingSnapshot&,const TrackingRay&,unsigned view_steps,unsigned shadow_steps);
TrackingSnapshot bake_reference_snapshot(const Scene&,unsigned resolution);
std::string reference_report_json(const ReferenceRenderer&);
}
