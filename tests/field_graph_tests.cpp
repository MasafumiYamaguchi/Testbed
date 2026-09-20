#include "white/field_graph.hpp"
#include "white/persistence.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace white;
namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
FieldNode& node(FieldGraph& graph,Id id) {
    const auto it=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.id==id;});
    if(it==graph.nodes.end())throw std::runtime_error("Test node missing");
    return *it;
}
void rejected(const FieldGraph& graph,const char* diagnosis) {
    const auto errors=validate_field_graph(graph);
    check(std::any_of(errors.begin(),errors.end(),[&](const auto& s){return s.find(diagnosis)!=std::string::npos;}),"Missing specific invalid graph diagnostic");
    bool failed=false;
    try {FieldEvaluationPlan invalid(graph);(void)invalid.gpu_params();}
    catch(const std::invalid_argument&){failed=true;}
    check(failed,"Invalid graph reached GPU uniform preparation");
}
void check_evaluation_order(const FieldGraph& graph) {
    const auto order=FieldEvaluationPlan(graph).evaluation_order();
    check(order.size()==graph.nodes.size(),"Evaluation plan lost a node");
    for(const auto& current:graph.nodes) {
        const auto output=std::find(order.begin(),order.end(),current.id);
        check(output!=order.end(),"Evaluation plan omitted a node");
        for(const auto input:current.inputs)
            check(std::find(order.begin(),output,input)!=output,"Evaluation plan reads a dependency before evaluating it");
    }
}
void compare_recipe(const CloudRecipe& recipe,std::size_t& samples) {
    auto graph=field_graph_from_recipe(recipe);
    check(validate_field_graph(graph).empty(),"Converted recipe failed graph validation");
    check(lower_to_recipe(graph)==recipe,"Graph conversion lost Recipe parameters or stable object IDs");
    const DensityField old(recipe);const FieldEvaluationPlan plan(graph);
    check_evaluation_order(graph);
    const auto old_gpu=gpu_density_params(old),new_gpu=plan.gpu_params();
    check(std::memcmp(&old_gpu,&new_gpu,sizeof(old_gpu))==0,"GPU fixed kernel uniform bytes differ after graph lowering");
    check(plan.maximum()==old.maximum()&&plan.local_support()==old.local_support()&&plan.world_support()==old.world_support(),"Conservative bounds changed after graph lowering");
    check(plan.algorithm_version()==2,"Density algorithm version changed");
    GridLayout grid{old.local_support(),{31,33,35}};
    for(unsigned z=0;z<35;++z)for(unsigned y=0;y<33;++y)for(unsigned x=0;x<31;++x) {
        const auto p=index_to_local(grid,{double(x),double(y),double(z)});
        const auto a=old.at(p),b=plan.at(p);++samples;
        check(a==b,"Old and graph CPU density mismatch");
        check(std::isfinite(b)&&b>=0&&b<=plan.maximum(),"Graph sample exceeds conservative density bound");
    }
    const auto bounds=plan.local_support();
    for(int corner=0;corner<8;++corner) {
        const Vec3 p{corner&1?bounds.max.x:bounds.min.x,corner&2?bounds.max.y:bounds.min.y,corner&4?bounds.max.z:bounds.min.z};
        check(plan.at(p)==0,"Envelope boundary not zero");
        const auto outside=p+Vec3{corner&1?1.0:-1.0,corner&2?1.0:-1.0,corner&4?1.0:-1.0};
        check(plan.at(outside)==0,"Outside finite support not zero");
    }
    std::reverse(graph.nodes.begin(),graph.nodes.end());
    const FieldEvaluationPlan reordered(graph);
    check(reordered.evaluation_order()==plan.evaluation_order(),"Storage order changed evaluation order");
    check(reordered.density_field().recipe()==old.recipe(),"Storage order changed kernel plan");
}
}
int main(int argc,char** argv) {try {
    if(argc!=2)throw std::runtime_error("Expected Phase 0 fixture directory");
    std::size_t samples=0,fixtures=0;
    for(const auto& file:std::filesystem::directory_iterator(argv[1])) {
        if(!file.is_regular_file()||file.path().extension()!=".json")continue;
        compare_recipe(read_scene(file.path()).cloud,samples);++fixtures;
    }
    check(fixtures==17,"Expected all seventeen Phase 0 fixtures");
    auto recipe=density_fixture(2);recipe.cells.clear();compare_recipe(recipe,samples);
    recipe=density_fixture(2);recipe.overlap=1;recipe.density=1000;recipe.noise.warp_amplitude=20;recipe.noise.micro_erosion=20;recipe.noise.medium_strength=1;
    recipe.noise.origin={11.5,-5.25,6.125};recipe.transform.translation={10,-2,30};recipe.transform.scale={2,3,0.25};
    recipe.transform.rotation={0,0,std::sin(0.25),std::cos(0.25)};
    while(recipe.cells.size()<8){auto cell=recipe.cells.back();cell.id=100+recipe.cells.size();cell.center.x+=0.7;recipe.cells.push_back(cell);}
    while(recipe.cuts.size()<8){Cut cut;cut.id=200+recipe.cuts.size();cut.center={40,double(recipe.cuts.size())*5,40};recipe.cuts.push_back(cut);}
    compare_recipe(recipe,samples);
    const auto original=field_graph_from_recipe(density_fixture(2));
    auto bad=original;bad.nodes.clear();rejected(bad,"1..64");
    bad=original;bad.nodes.resize(max_field_nodes+1);rejected(bad,"1..64");
    bad=original;node(bad,2).inputs.resize(max_field_inputs+1,1);rejected(bad,"8 input");
    bad=original;node(bad,1).id=0;rejected(bad,"nonzero and unique");
    bad=original;node(bad,2).id=1;rejected(bad,"nonzero and unique");
    bad=original;node(bad,2).inputs.clear();rejected(bad,"missing or extra inputs");
    bad=original;node(bad,2).inputs={999};rejected(bad,"missing node");
    bad=original;node(bad,2).inputs={5};rejected(bad,"type mismatch");
    bad=original;node(bad,6).inputs={5,4};rejected(bad,"type mismatch");
    bad=original;node(bad,2).inputs={3};rejected(bad,"cycle");
    bad=original;node(bad,3).inputs={3};rejected(bad,"cycle");
    bad=original;bad.output=99;rejected(bad,"output must reference");
    bad=original;bad.output=4;rejected(bad,"output must reference");
    bad=original;bad.nodes.push_back({99,FieldShape{}, {}});rejected(bad,"Disconnected");
    bad=original;bad.nodes.push_back({99,FieldGridOperation{}, {}});rejected(bad,"Iterative Grid Operation");
    bad=original;bad.graph_version=2;rejected(bad,"graph version");
    bad=original;bad.algorithm_version=3;rejected(bad,"algorithm version");
    bad=original;std::get<FieldShape>(node(bad,1).parameters).cells.resize(9);rejected(bad,"8 cell");
    bad=original;std::get<FieldMask>(node(bad,5).parameters).cuts.resize(9);rejected(bad,"8 cut");
    bad=original;std::get<FieldDensity>(node(bad,4).parameters).scale=std::numeric_limits<double>::quiet_NaN();rejected(bad,"Density/blend/overlap");
    bad=original;std::get<FieldMask>(node(bad,5).parameters).envelope.max.x=std::numeric_limits<double>::infinity();rejected(bad,"Envelope");
    bad=original;std::get<FieldWarp>(node(bad,2).parameters).amplitude=21;rejected(bad,"Noise strength");
    bad=original;std::get<FieldWarp>(node(bad,2).parameters).origin.x=1;rejected(bad,"shared saved local");
    bad=original;std::get<FieldShape>(node(bad,1).parameters).cells.front().radii.x=0;rejected(bad,"center/radii");
    bad=original;std::get<FieldShape>(node(bad,1).parameters).cells[1].id=std::get<FieldShape>(node(bad,1).parameters).cells[0].id;rejected(bad,"globally unique");
    bad=original;std::get<FieldOutput>(node(bad,6).parameters).transform.scale.y=-1;rejected(bad,"transform");
    FieldGraphDocument document(original);
    const auto revision=document.revision();
    try {document.replace(bad);check(false,"Invalid graph replacement unexpectedly succeeded");}catch(const std::invalid_argument&){}
    check(document.revision()==revision&&document.graph()==original,"Invalid replacement changed document");
    auto changed=original;std::get<FieldDensity>(node(changed,4).parameters).scale=2;
    check(document.replace(changed)&&document.revision()==revision+1,"Parameter edit did not advance graph revision");
    check(document.last_change().parameters_changed==std::vector<Id>{4},"Wrong parameter dependency tracked");
    check(FieldEvaluationPlan(document.graph()).at({0,35,0})==2*FieldEvaluationPlan(original).at({0,35,0}),"Parameter edit did not reach lowered evaluator");
    std::reverse(changed.nodes.begin(),changed.nodes.end());check(!document.replace(changed),"Storage-only reorder advanced graph revision");
    changed=original;node(changed,1).id=77;node(changed,2).inputs={77};
    const auto changes=field_graph_changes(original,changed);
    check(changes.added==std::vector<Id>{77}&&changes.removed==std::vector<Id>{1}&&changes.references_changed==std::vector<Id>{2},"Stable node reference replacement not tracked");
    check(changes.parameters_changed.empty(),"Reference edit falsely changed parameters");
    check(lower_to_recipe(original)==lower_to_recipe(changed),"Graph ID renaming changed stable Cell noise seeds");
    check_evaluation_order(changed);
    std::reverse(changed.nodes.begin(),changed.nodes.end());check_evaluation_order(changed);
    changed=original;node(changed,6).id=88;changed.output=88;
    check(field_graph_changes(original,changed).output_changed,"Output reference edit not tracked");
    bool nonfinite=false;try {FieldEvaluationPlan(original).at({std::numeric_limits<double>::infinity(),0,0});}catch(const std::invalid_argument&){nonfinite=true;}
    check(nonfinite,"Nonfinite graph sample was accepted");
    changed=original;
    const FieldEvaluationPlan queued_snapshot(changed);
    const auto captured_uniforms=queued_snapshot.gpu_params();
    const auto captured_density=queued_snapshot.at({0,35,0});
    std::get<FieldDensity>(node(changed,4).parameters).scale=3;
    const FieldEvaluationPlan fresh_preview(changed),fresh_bake(changed);
    const auto queued_uniforms=queued_snapshot.gpu_params();
    check(std::memcmp(&captured_uniforms,&queued_uniforms,sizeof(captured_uniforms))==0&&queued_snapshot.at({0,35,0})==captured_density,
        "Editing the source graph mutated a queued immutable evaluation snapshot");
    const auto preview_uniforms=fresh_preview.gpu_params(),bake_uniforms=fresh_bake.gpu_params();
    check(std::memcmp(&preview_uniforms,&bake_uniforms,sizeof(preview_uniforms))==0,
        "Independent preview and bake plans generated different kernel uniforms");
    check(fresh_preview.at({0,35,0})==3*captured_density,"New preview did not observe the accepted graph edit");
    std::cout<<"Field Graph: "<<fixtures<<" Phase 0 fixtures + empty/extreme cases; "<<samples
             <<" exact CPU comparisons; GPU uniforms byte-identical; validation and revision tests passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
