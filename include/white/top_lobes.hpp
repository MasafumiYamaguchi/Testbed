#pragma once
#include "white/developed_cells.hpp"

namespace white {
struct TopLobeNode {
    Id id=0,parent_id=0;
    unsigned depth=0;
    Cell primitive{};
    bool operator==(const TopLobeNode&)const=default;
};
TopLobeSource make_top_lobe_source(const DevelopedCloud&,Id target);
struct TopLobeSettingsCommand {TopLobeSettings settings;};
struct TopLobeTrunkCommand {DevelopedCommand command;};
using TopLobeCommand=std::variant<TopLobeSettingsCommand,TopLobeTrunkCommand>;
// Pure validated edit. Trunk edits retain reserved lobe IDs; a deleted target
// is rejected explicitly instead of silently retargeting another development.
TopLobeSource command_top_lobes(TopLobeSource,const TopLobeCommand&);
std::vector<std::string> validate_top_lobes(const TopLobeSource&);
std::vector<TopLobeNode> generate_top_lobes(const TopLobeSource&);
double top_lobe_mask_height(const TopLobeSource&);
struct alignas(16) GpuTopLobeParams {
    GpuDevelopedParams fields{};
    Float4 mask{}; // enabled, object-local top boundary, mode, actual lobe count
};
static_assert(sizeof(GpuTopLobeParams)==1920);
class TopLobeEvaluationPlan {
public:
    explicit TopLobeEvaluationPlan(TopLobeSource);
    const TopLobeSource& source()const{return source_;}
    const std::vector<TopLobeNode>& hierarchy()const{return hierarchy_;}
    const std::optional<DensityField>& top_field()const{return top_;}
    double at(Vec3 object_local)const;
    double maximum()const{return maximum_;}
    Bounds local_support()const{return support_;}
    Bounds world_support()const;
    GpuTopLobeParams gpu_params()const;
private:
    TopLobeSource source_;
    DevelopedEvaluationPlan trunk_;
    std::vector<TopLobeNode> hierarchy_;
    std::optional<DensityField> top_;
    std::vector<AltitudeDensityEvaluator> profiles_;
    Bounds support_{};
    double mask_height_=0,maximum_=0;
};
std::vector<float> bake_top_lobes(const TopLobeEvaluationPlan&,const GridLayout&);
}
