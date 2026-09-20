#pragma once
#include "white/centerline.hpp"
#include <cstddef>

namespace white {
std::vector<std::string> validate_developed_cloud(const DevelopedCloud&);
std::size_t developed_primitive_count(const DevelopedCloud&);
Id next_developed_id(const DevelopedCloud&);
// Exact single-development migration, retaining all five roles and any manual
// children, cuts, noise and altitude profile. No automatic budget reduction.
DevelopedCloud develop_centerline(const CenterlineShape&);
DevelopedCloud develop_cumulonimbus(const CumulonimbusGroup&);
// Allocates one development ID plus all five reserved stable role IDs. New
// development uses three active roles and default source settings.
DevelopedCell make_developed_cell(const DevelopedCloud&);
enum class DevelopedDuplicateSeed {retain,regenerate};
struct DevelopedAdd {DevelopedCell cell;};
struct DevelopedRemove {Id id;};
struct DevelopedMove {Id id;Vec3 translation;};
struct DevelopedEdit {Id id;CenterlineCommand command;};
struct DevelopedSetRoles {Id id;std::vector<unsigned> roles;};
// Shared shape fusion (local metres) and density overlap are separate controls.
// A caller editing one passes the current value of the other.
struct DevelopedSetFusion {double width,overlap;};
struct DevelopedDuplicate {
    Id id;
    DevelopedDuplicateSeed policy;
    std::uint64_t structure_seed=0;
    DevelopedDuplicate(Id target,DevelopedDuplicateSeed choice,std::uint64_t seed=0)
        :id(target),policy(choice),structure_seed(seed){}
};
using DevelopedCommand=std::variant<DevelopedAdd,DevelopedRemove,DevelopedMove,
    DevelopedEdit,DevelopedSetRoles,DevelopedSetFusion,DevelopedDuplicate>;
DevelopedCloud command_developed_cloud(const DevelopedCloud&,const DevelopedCommand&);
// Distances/fusion are local metres; coefficients are nonnegative dimensionless
// densities. Geometric bridge is continuously gated by both coefficients.
double developed_density_union(double distance_a,double coefficient_a,
    double distance_b,double coefficient_b,double fusion_width,double overlap);

struct alignas(16) GpuDevelopedParams {
    std::array<GpuDensityParams,max_developed_cells> groups{};
    std::array<Float4,max_developed_cells> translations{};
    Float4 envelope_min{},envelope_max{};
    Float4 settings{}; // count, fusion metres, overlap, conservative density max
};
static_assert(sizeof(GpuDevelopedParams)==1904);
static_assert(offsetof(GpuDevelopedParams,translations)==1824);
static_assert(offsetof(GpuDevelopedParams,envelope_min)==1856);
static_assert(offsetof(GpuDevelopedParams,settings)==1888);
// Fixed two-group CPU/HLSL contract. gpu_params() supplies the full data; it
// does not imply the application has selected/bound the grouped shader yet.
class DevelopedEvaluationPlan {
public:
    explicit DevelopedEvaluationPlan(DevelopedCloud);
    const DevelopedCloud& cloud()const{return cloud_;}
    double at(Vec3 object_local)const;
    double maximum()const{return maximum_;}
    Bounds local_support()const{return support_;}
    Bounds world_support()const;
    GpuDevelopedParams gpu_params()const;
    const std::vector<DensityField>& fields()const{return fields_;}
private:
    DevelopedCloud cloud_;
    std::vector<DensityField> fields_;
    std::vector<AltitudeDensityEvaluator> profiles_;
    Bounds support_{{-1,-1,-1},{1,1,1}};
    double maximum_=0;
};
// Legacy Recipe lowering is exact only for zero/one developed cell. Multiple
// independently profiled cells require the grouped evaluator, never flatten.
CloudRecipe lower_single_developed_recipe(const DevelopedCloud&);
std::vector<float> bake_developed_cloud(const DevelopedEvaluationPlan&,const GridLayout&);
}
