#pragma once
#include "white/top_lobe_scene.hpp"
#include <functional>
#include <stop_token>

namespace white {
inline constexpr std::uint32_t generation_algorithm_version=1;
inline constexpr std::size_t max_wind_knots=6;
struct WindKnot {
    double altitude=0; // Normalized fixed reference altitude, not current bounds.
    Vec3 displacement{}; // Horizontal object-local metres at stage 1, not m/s.
    bool operator==(const WindKnot&)const=default;
};
struct DevelopmentGrowth {
    Id cell_id=0;
    double start_stage=0,amount=1;
    std::vector<Id> pinned_controls; // Preserve guide X/Z in the initial frame.
    bool operator==(const DevelopmentGrowth&)const=default;
};
struct GenerationSettings {
    std::uint32_t algorithm_version=generation_algorithm_version;
    bool enabled=true;
    double stage=1,initial_height_fraction=.4;
    double wind_base=0,wind_height=120;
    std::vector<WindKnot> wind{{0,{}},{1,{}}};
    Vec3 reference_translation{}; // Bulk motion, separately applied at stage 1.
    std::vector<DevelopmentGrowth> cells;
    bool operator==(const GenerationSettings&)const=default;
};
GenerationSettings default_generation_settings(const Scene&);
std::vector<std::string> validate_generation(const Scene&,const GenerationSettings&);
Vec3 sample_generation_wind(const GenerationSettings&,double object_local_altitude);
double development_stage(const GenerationSettings&,Id);
std::uint64_t generation_input_hash(const Scene&,const GenerationSettings&);
struct GenerationCandidate {
    Scene initial; // Provenance for an explicit new candidate; never live authority.
    GenerationSettings settings;
    Scene evaluated; // Complete selected state; rendering never integrates growth.
    std::uint64_t input_hash=0;
    double elapsed_ms=0;
};
enum class GenerationStatus {completed,cancelled,failed};
struct GenerationOutcome {
    GenerationStatus status=GenerationStatus::failed;
    std::optional<GenerationCandidate> candidate;
    std::string message;
};
// Bounded direct evaluation. Progress callbacks run between structural units,
// not per density sample. Only a completed candidate may be published.
GenerationOutcome generate_cloud_state(const Scene&,const GenerationSettings&,
    std::stop_token stop={},const std::function<void(double)>& progress={});
}
