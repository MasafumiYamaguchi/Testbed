#pragma once
#include "white/density.hpp"
#include <cstddef>
#include <variant>

namespace white {
inline constexpr std::uint32_t field_graph_version=1;
inline constexpr std::uint32_t field_density_algorithm_version=2;
inline constexpr std::size_t max_field_nodes=64;
inline constexpr std::size_t max_field_inputs=8;

// These are typed stages of the existing pointwise kernel, not signed-distance
// values. Stage types deliberately prevent reordering noncommuting operations.
enum class FieldType {shape,warped_shape,modulated_shape,density,mask,output,grid};
enum class FieldDomain {pointwise,iterative_grid};
struct FieldShape {
    std::vector<Cell> cells;
    double blend_width=2,overlap=0;
    bool operator==(const FieldShape&)const=default;
};
struct FieldWarp {
    Vec3 origin{};
    double frequency=0.035,amplitude=0;
    std::uint64_t structure_seed=42;
    bool operator==(const FieldWarp&)const=default;
};
struct FieldNoise {
    Vec3 origin{};
    double medium_frequency=0.12,medium_strength=0,micro_frequency=0.6,micro_erosion=0;
    std::uint64_t detail_seed=17;
    bool operator==(const FieldNoise&)const=default;
};
struct FieldDensity {
    double scale=1;
    bool operator==(const FieldDensity&)const=default;
};
struct FieldMask {
    Bounds envelope{};
    BasePlane base{};
    std::vector<Cut> cuts;
    bool operator==(const FieldMask&)const=default;
};
struct FieldOutput {
    Id cloud_id=1;
    Transform transform{};
    Optics optics{}; // Preserved Recipe metadata; never part of density uniforms.
    bool operator==(const FieldOutput&)const=default;
};
enum class GridOperationKind {erosion,diffusion};
struct FieldGridOperation {
    GridOperationKind kind=GridOperationKind::erosion;
    bool operator==(const FieldGridOperation&)const=default;
};
using FieldParameters=std::variant<FieldShape,FieldWarp,FieldNoise,FieldDensity,FieldMask,FieldOutput,FieldGridOperation>;
struct FieldNode {
    Id id=0; // Stable graph namespace, separate from stable Cell/Cut IDs.
    FieldParameters parameters=FieldShape{};
    std::vector<Id> inputs; // Ordered typed ports; output has [density, mask].
    bool operator==(const FieldNode&)const=default;
};
struct FieldGraph {
    std::uint32_t graph_version=field_graph_version;
    std::uint32_t algorithm_version=field_density_algorithm_version;
    std::vector<FieldNode> nodes;
    Id output=0;
    bool operator==(const FieldGraph&)const=default;
};
FieldType field_type(const FieldNode&);
FieldDomain field_domain(const FieldNode&);
FieldGraph field_graph_from_recipe(const CloudRecipe&);
std::vector<std::string> validate_field_graph(const FieldGraph&);
// Validation, including supported topology and parameter ranges, precedes any
// lowering or upload. No arbitrary code or unsupported graph reaches the GPU.
CloudRecipe lower_to_recipe(const FieldGraph&);

class FieldEvaluationPlan {
public:
    explicit FieldEvaluationPlan(const FieldGraph&);
    const DensityField& density_field()const{return field_;}
    const std::vector<Id>& evaluation_order()const{return order_;}
    double at(Vec3 local)const{return field_.at(local);}
    double maximum()const{return field_.maximum();}
    Bounds local_support()const{return field_.local_support();}
    Bounds world_support()const{return field_.world_support();}
    GpuDensityParams gpu_params()const{return gpu_density_params(field_);}
    std::uint32_t algorithm_version()const{return field_density_algorithm_version;}
private:
    DensityField field_;
    std::vector<Id> order_;
};

struct FieldGraphChanges {
    bool version_changed=false,output_changed=false;
    std::vector<Id> added,removed,parameters_changed,references_changed;
    bool empty()const;
};
// Both operands must be valid; vector storage order is not a semantic edit.
FieldGraphChanges field_graph_changes(const FieldGraph&,const FieldGraph&);
class FieldGraphDocument {
public:
    explicit FieldGraphDocument(FieldGraph);
    const FieldGraph& graph()const{return graph_;}
    std::uint64_t revision()const{return revision_;}
    const FieldGraphChanges& last_change()const{return changes_;}
    bool replace(FieldGraph); // Validate first; failures preserve graph/revision.
private:
    FieldGraph graph_;
    std::uint64_t revision_=1;
    FieldGraphChanges changes_;
};
}
