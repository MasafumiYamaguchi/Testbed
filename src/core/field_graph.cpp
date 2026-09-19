#include "white/field_graph.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace white {
namespace {
using NodeMap=std::map<Id,const FieldNode*>;
struct Analysis {
    std::vector<std::string> errors;
    std::vector<Id> order;
    CloudRecipe recipe;
};
std::vector<FieldType> input_types(FieldType type) {
    switch(type) {
    case FieldType::shape:case FieldType::mask:return {};
    case FieldType::warped_shape:return {FieldType::shape};
    case FieldType::modulated_shape:return {FieldType::warped_shape};
    case FieldType::density:return {FieldType::modulated_shape};
    case FieldType::output:return {FieldType::density,FieldType::mask};
    case FieldType::grid:return {};
    }
    throw std::invalid_argument("Unknown field node type");
}
Analysis analyze(const FieldGraph& graph) {
    Analysis result;
    auto fail=[&](const std::string& message){result.errors.push_back(message);};
    if(graph.graph_version!=field_graph_version)fail("Unsupported field graph version");
    if(graph.algorithm_version!=field_density_algorithm_version)fail("Unsupported density algorithm version");
    if(graph.nodes.empty()||graph.nodes.size()>max_field_nodes) {
        fail("Field graph requires 1..64 nodes");return result;
    }
    NodeMap nodes;
    for(const auto& node:graph.nodes) {
        if(node.id==0||!nodes.emplace(node.id,&node).second)fail("Node IDs must be nonzero and unique");
        if(node.inputs.size()>max_field_inputs)fail("Field node exceeds 8 input limit");
        if(field_domain(node)==FieldDomain::iterative_grid)fail("Iterative Grid Operation is unsupported by pointwise evaluation");
        if(const auto* shape=std::get_if<FieldShape>(&node.parameters);shape&&shape->cells.size()>8)fail("Shape exceeds fixed kernel 8 cell limit");
        if(const auto* mask=std::get_if<FieldMask>(&node.parameters);mask&&mask->cuts.size()>8)fail("Mask exceeds fixed kernel 8 cut limit");
    }
    if(!result.errors.empty())return result;
    const auto output=nodes.find(graph.output);
    if(output==nodes.end()||field_type(*output->second)!=FieldType::output)fail("Graph output must reference an output node");
    for(const auto& node:graph.nodes) {
        const auto expected=input_types(field_type(node));
        if(node.inputs.size()!=expected.size())fail("Node "+std::to_string(node.id)+" has missing or extra inputs");
        for(std::size_t i=0;i<node.inputs.size();++i) {
            const auto input=nodes.find(node.inputs[i]);
            if(input==nodes.end())fail("Node "+std::to_string(node.id)+" references a missing node");
            else if(i<expected.size()&&field_type(*input->second)!=expected[i])fail("Node "+std::to_string(node.id)+" input type mismatch");
        }
    }
    // All nodes are visited, including disconnected components. The size limit
    // bounds recursion and malformed input work before any parameter copying.
    std::map<Id,int> state;
    std::function<void(Id)> visit=[&](Id id) {
        if(state[id]==2)return;
        if(state[id]==1){fail("Field graph contains a cycle");return;}
        state[id]=1;
        for(Id input:nodes.at(id)->inputs)if(nodes.contains(input))visit(input);
        state[id]=2;result.order.push_back(id);
    };
    for(const auto& [id,node]:nodes){(void)node;visit(id);}
    if(!result.errors.empty())return result;
    std::set<Id> reachable;
    std::function<void(Id)> mark=[&](Id id) {
        if(!reachable.insert(id).second)return;
        for(Id input:nodes.at(id)->inputs)mark(input);
    };
    mark(graph.output);
    if(reachable.size()!=nodes.size())fail("Disconnected field nodes are unsupported; remove unused nodes explicitly");
    // This first IR uses precisely one of each fixed-kernel stage. Rejecting
    // all other topologies is preferable to silently changing their meaning.
    std::array<unsigned,6> count{};
    for(const auto& [id,node]:nodes){(void)id;++count.at(static_cast<std::size_t>(field_type(*node)));}
    if(std::any_of(count.begin(),count.end(),[](auto n){return n!=1;}))fail("Fixed kernel requires one shape, warp, noise, density, mask and output node");
    if(!result.errors.empty())return result;
    const auto& out=*output->second;
    const auto& density=*nodes.at(out.inputs[0]);
    const auto& mask=std::get<FieldMask>(nodes.at(out.inputs[1])->parameters);
    const auto& noise_node=*nodes.at(density.inputs[0]);
    const auto& noise=std::get<FieldNoise>(noise_node.parameters);
    const auto& warp_node=*nodes.at(noise_node.inputs[0]);
    const auto& warp=std::get<FieldWarp>(warp_node.parameters);
    const auto& shape=std::get<FieldShape>(nodes.at(warp_node.inputs[0])->parameters);
    const auto& metadata=std::get<FieldOutput>(out.parameters);
    if(noise.origin!=warp.origin)fail("Fixed kernel requires shared saved local noise and warp origin");
    auto& r=result.recipe;
    r.id=metadata.cloud_id;r.transform=metadata.transform;r.optics=metadata.optics;
    r.cells=shape.cells;r.blend_width=shape.blend_width;r.overlap=shape.overlap;
    r.density=std::get<FieldDensity>(density.parameters).scale;
    r.envelope=mask.envelope;r.base=mask.base;r.cuts=mask.cuts;
    r.structure_seed=warp.structure_seed;r.detail_seed=noise.detail_seed;
    r.noise.origin=noise.origin;r.noise.medium_frequency=noise.medium_frequency;r.noise.medium_strength=noise.medium_strength;
    r.noise.micro_frequency=noise.micro_frequency;r.noise.micro_erosion=noise.micro_erosion;
    r.noise.warp_frequency=warp.frequency;r.noise.warp_amplitude=warp.amplitude;
    Scene scene;scene.cloud=r;
    for(const auto& error:validate(scene))fail("Field parameter: "+error);
    return result;
}
void require(const Analysis& result) {
    if(result.errors.empty())return;
    std::ostringstream message;message<<"Field graph validation failed:";
    for(const auto& error:result.errors)message<<"\n- "<<error;
    throw std::invalid_argument(message.str());
}
FieldGraph canonical(FieldGraph graph) {
    std::sort(graph.nodes.begin(),graph.nodes.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return graph;
}
}
FieldType field_type(const FieldNode& node) {
    // Variant order and FieldType order are deliberately the same and fixed by
    // graph version 1. valueless variants are rejected, never sent to a shader.
    if(node.parameters.valueless_by_exception())throw std::invalid_argument("Field node has no parameters");
    return static_cast<FieldType>(node.parameters.index());
}
FieldDomain field_domain(const FieldNode& node) {
    return field_type(node)==FieldType::grid?FieldDomain::iterative_grid:FieldDomain::pointwise;
}
FieldGraph field_graph_from_recipe(const CloudRecipe& r) {
    Scene scene;scene.cloud=r;require_valid(scene);
    FieldGraph graph;
    graph.nodes={
        {1,FieldShape{r.cells,r.blend_width,r.overlap},{}},
        {2,FieldWarp{r.noise.origin,r.noise.warp_frequency,r.noise.warp_amplitude,r.structure_seed},{1}},
        {3,FieldNoise{r.noise.origin,r.noise.medium_frequency,r.noise.medium_strength,r.noise.micro_frequency,r.noise.micro_erosion,r.detail_seed},{2}},
        {4,FieldDensity{r.density},{3}},
        {5,FieldMask{r.envelope,r.base,r.cuts},{}},
        {6,FieldOutput{r.id,r.transform,r.optics},{4,5}}
    };
    graph.output=6;return graph;
}
std::vector<std::string> validate_field_graph(const FieldGraph& graph) {return analyze(graph).errors;}
CloudRecipe lower_to_recipe(const FieldGraph& graph) {auto result=analyze(graph);require(result);return std::move(result.recipe);}
FieldEvaluationPlan::FieldEvaluationPlan(const FieldGraph& graph):field_(lower_to_recipe(graph)) {
    // analyze is bounded to 64 nodes; keep the immutable evaluation order with
    // the validated recipe. Both preview and bake consume these same uniforms.
    order_=analyze(graph).order;
}
bool FieldGraphChanges::empty()const {
    return !version_changed&&!output_changed&&added.empty()&&removed.empty()&&parameters_changed.empty()&&references_changed.empty();
}
FieldGraphChanges field_graph_changes(const FieldGraph& before,const FieldGraph& after) {
    require(analyze(before));require(analyze(after));
    FieldGraphChanges changes;
    changes.version_changed=before.graph_version!=after.graph_version||before.algorithm_version!=after.algorithm_version;
    changes.output_changed=before.output!=after.output;
    NodeMap old_nodes,new_nodes;
    for(const auto& node:before.nodes)old_nodes.emplace(node.id,&node);
    for(const auto& node:after.nodes)new_nodes.emplace(node.id,&node);
    for(const auto& [id,node]:old_nodes) {
        const auto match=new_nodes.find(id);
        if(match==new_nodes.end())changes.removed.push_back(id);
        else {
            if(node->parameters!=match->second->parameters)changes.parameters_changed.push_back(id);
            if(node->inputs!=match->second->inputs)changes.references_changed.push_back(id);
        }
    }
    for(const auto& [id,node]:new_nodes){(void)node;if(!old_nodes.contains(id))changes.added.push_back(id);}
    return changes;
}
FieldGraphDocument::FieldGraphDocument(FieldGraph graph):graph_(canonical(std::move(graph))) {require(analyze(graph_));}
bool FieldGraphDocument::replace(FieldGraph graph) {
    graph=canonical(std::move(graph));auto changes=field_graph_changes(graph_,graph);
    if(changes.empty())return false;
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Field graph revision exhausted");
    graph_=std::move(graph);changes_=std::move(changes);++revision_;return true;
}
}
