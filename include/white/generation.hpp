#pragma once
#include "white/top_lobe_scene.hpp"
#include <functional>
#include <stop_token>

namespace white {
std::uint64_t generation_job_count();
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
