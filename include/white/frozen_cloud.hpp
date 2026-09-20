#pragma once
#include "white/anvil.hpp"
#include "white/modifiers.hpp"


namespace white {
struct GenerationCandidate;
struct GenerationOutcome;
// Immutable geometry identity; independent of detail, optics and provenance.
std::uint64_t frozen_content_hash(const FrozenCloudState&);
// Canonical FNV-1a payload checksum; includes everything except itself.
std::uint64_t frozen_payload_hash(const FrozenCloudState&);
std::uint64_t frozen_density_hash(const FrozenCloudState&);
std::vector<std::string> validate_frozen_cloud(const FrozenCloudState&);
CloudRecipe frozen_proxy_recipe(const FrozenCloudState&);
void refresh_frozen_scene(Scene&);
Scene freeze_candidate(const GenerationOutcome&);
Scene freeze_candidate(const GenerationCandidate&);
Scene generation_initial_scene(const FrozenCloudState&);
bool frozen_can_regenerate(const FrozenCloudState&);
CloudRecipe frozen_effective_recipe(const FrozenField&);
// Evaluated-data precision allowance; never consults generation provenance.
double frozen_anvil_edge_error_bound(const FrozenCloudState&);
// Called only after v1 payload/content hashes have been verified. Uses saved
// evaluated data and never requires a generation implementation or provenance.
void migrate_frozen_v1(FrozenCloudState&);
// Recompute finite support/rho_max and content hash after an authorized edit.
void refresh_frozen_cloud(FrozenCloudState&);
Scene scene_with_frozen_detail(Scene,Id field_id,NoiseSettings,std::uint64_t detail_seed,FrozenDetailLayers);
class FrozenEvaluationPlan {
public:
    explicit FrozenEvaluationPlan(FrozenCloudState);
    double at(Vec3 object_local,const ModifierFieldScales& scales={})const;
    double maximum()const{return state_.rho_max;}
    Bounds local_support()const{return state_.support;}
    Bounds world_support()const;
    GpuAnvilParams gpu_params()const;
private:
    FrozenCloudState state_;
    std::vector<DensityField> fields_;
    std::vector<AltitudeDensityEvaluator> profiles_;
};
}
