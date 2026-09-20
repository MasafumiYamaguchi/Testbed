#pragma once
#include "white/frozen_cloud.hpp"
#include "white/generation.hpp"
#include <span>
#include <string_view>

namespace white {
inline constexpr std::uint32_t cloud_preset_version=1;
enum class CloudPresetKind {cumulonimbus,wide,multiple,anvil};
enum class PresetWind {calm,upper_shear};
struct CloudPresetDefinition {
    CloudPresetKind kind;
    std::string_view key,label;
    std::uint32_t version=cloud_preset_version,generation_version=generation_algorithm_version;
    double width=80,height=120,separation=0;
    unsigned developments=1;
    bool top_lobes=false,anvil=false;
};
std::span<const CloudPresetDefinition> cloud_presets();
struct CloudPresetRequest {
    CloudPresetKind kind=CloudPresetKind::cumulonimbus;
    std::uint32_t preset_version=cloud_preset_version;
    std::uint64_t structure_seed=42,detail_seed=17;
    double stage=1;
    PresetWind wind=PresetWind::calm;
};
struct GenerationDraft {
    Scene initial;
    GenerationSettings settings;
    std::string preset_key;
    std::uint32_t preset_version=0;
    bool operator==(const GenerationDraft&)const=default;
};
// Produces inputs only. Uses the same source constructors and generation API as
// manual editing; the current document is never replaced by preset selection.
GenerationDraft make_cloud_preset(const CloudPresetRequest&);
enum class StructureVariationScope {whole_cloud,selected_development};
GenerationDraft make_structure_variation(const Scene&,StructureVariationScope,std::uint64_t seed,Id selected=0);
// A zero target applies a deterministic seed to every fixed field. Otherwise
// only that stable field ID changes. Geometry/origin/finishing constraints stay.
Scene regenerate_fixed_detail(Scene,std::uint64_t seed,Id target=0);
// Normal stage/seed candidates keep mutable fixed-field detail/layers/optics.
// New-preset reset and complete clone operations intentionally use other paths.
Scene preserve_candidate_finish(const Scene& current,Scene candidate);
// Preparation guard: immutable evaluated structure and source history must
// match; finishing, view and object-instance metadata may change before launch.
bool generation_draft_matches_current(const Scene& prepared_current,const Scene& current);
// Common comparison view, with no geometry or material-copy side effects.
Scene candidate_comparison_scene(const Scene& current,const Scene& candidate);
bool candidate_scene_unchanged(const Scene& current,const Scene& job_guard);
}
