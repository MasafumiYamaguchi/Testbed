#include "white/generation.hpp"
#include "white/centerline_scene.hpp"
#include "white/persistence.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;
namespace {
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
Scene fixture(){auto scene=new_top_lobe_scene(new_developed_scene(new_centerline_scene(new_cumulonimbus_scene({}))));auto settings=scene.top_lobes->settings;settings.mode=TopLobeMode::children;scene=scene_with_top_lobe_settings(scene,settings);scene.camera.position={90,80,380};scene.camera.target={40,70,0};return scene;}
GenerationCandidate generate(const Scene& scene,const GenerationSettings& s){const auto result=generate_cloud_state(scene,s);if(!result.candidate)throw std::runtime_error(result.message);check(result.status==GenerationStatus::completed,"Non-completed generation published a candidate");return *result.candidate;}
const DevelopedCell& first(const Scene& scene){return editable_developed_source(scene)->cells.front();}
void comparisons(){
    auto initial=fixture();auto settings=default_generation_settings(initial);const auto mature=generate(initial,settings);
    settings.stage=.25;const auto young=generate(initial,settings);
    const auto& a=first(young.evaluated);const auto& b=first(mature.evaluated);
    check(a.shape.source.parameters.height<b.shape.source.parameters.height,"Development does not grow vertically");
    check(a.shape.profile.back().radius_scale/a.shape.profile.front().radius_scale<b.shape.profile.back().radius_scale/b.shape.profile.front().radius_scale,"Growth is only uniform scaling");
    const auto young_nodes=generate_top_lobes(*young.evaluated.top_lobes),mature_nodes=generate_top_lobes(*mature.evaluated.top_lobes);
    check(young_nodes.size()==mature_nodes.size()&&young_nodes[0].id==mature_nodes[0].id&&young_nodes[0].primitive.radii.x<mature_nodes[0].primitive.radii.x,"Lobe growth lost stable identity or failed to change radius");
    settings.stage=1;settings.wind.back().displacement={70,0,25};const auto wind=generate(initial,settings);
    const auto& curve=first(wind.evaluated).shape;
    check(sample_centerline(curve,1).position.x>sample_centerline(first(mature.evaluated).shape,1).position.x+60,"Upper wind displacement missing");
    check(sample_centerline(curve,0).position==sample_centerline(first(mature.evaluated).shape,0).position,"Wind moved fixed cloud base");
    check(wind.evaluated.cloud.transform==mature.evaluated.cloud.transform,"Relative wind translated whole object");
    check(first(wind.evaluated).shape.source.cell_ids==first(initial).shape.source.cell_ids,"Generation replaced stable primitive IDs");
    check(first(wind.evaluated).shape.source.modifiers.noise.origin==first(initial).shape.source.modifiers.noise.origin,"Generation changed noise reference coordinates");
    check(generate(initial,settings).evaluated==wind.evaluated,"Generation depends on previous calls or mutable RNG");
    auto returned=settings;returned.stage=.2;(void)generate(initial,returned);check(generate(initial,settings).evaluated==wind.evaluated,"Returning to a stage changed the candidate");
    const SceneDensityEvaluator field(wind.evaluated);const auto bounds=field.local_support();
    for(int x=-4;x<=4;++x)for(int z=-4;z<=4;++z)check(field.at({x*20.,-.1,z*20.})==0,"Growth/wind violated flat-base forbidden region");
    for(unsigned axis=0;axis<3;++axis){auto p=(bounds.min+bounds.max)*.5;double& coordinate=axis==0?p.x:axis==1?p.y:p.z;coordinate=(axis==0?bounds.max.x:axis==1?bounds.max.y:bounds.max.z)+10;check(field.at(p)==0,"Growth support is not finite");}
    auto drift=settings;drift.reference_translation={25,0,-10};const auto moved=generate(initial,drift);
    check(moved.evaluated.cloud.transform.translation==Vec3{25,0,-10},"Reference motion was mixed into relative deformation");
    check(generate_top_lobes(*moved.evaluated.top_lobes)==generate_top_lobes(*wind.evaluated.top_lobes),"Bulk motion regenerated local lobes");
    check(wind.input_hash!=mature.input_hash&&generate(initial,settings).input_hash==wind.input_hash,"Generation input/version hash is not stable and discriminating");
    check(parse_scene_json(scene_json(wind.evaluated))==wind.evaluated,"Evaluated state cannot be saved without re-running growth");
}
void guides_independent_cells_and_failures(){
    auto initial=new_developed_scene(new_centerline_scene(new_cumulonimbus_scene({})));auto source=*initial.developed;
    auto cell=make_developed_cell(source);cell.translation={110,0,0};initial=scene_with_developed_command(initial,DevelopedAdd{cell});
    auto settings=default_generation_settings(initial);settings.wind.back().displacement={60,0,0};settings.stage=.5;settings.cells.back().start_stage=.6;
    const auto candidate=generate(initial,settings);const auto& generated=*candidate.evaluated.developed;
    check(generated.cells.front().shape.source.parameters.height>generated.cells.back().shape.source.parameters.height,"Cell onset did not delay independent growth");
    auto reference=sample_centerline(initial.developed->cells.front().shape,.5).position;
    initial=scene_with_developed_command(initial,DevelopedEdit{source.cells.front().id,CenterlineMovePoint{2,reference+Vec3{12,0,0}}});
    settings=default_generation_settings(initial);settings.wind.back().displacement={60,0,0};settings.stage=.7;
    const auto pinned=generate(initial,settings);const auto original=sample_centerline(first(initial).shape,.5).position,after=sample_centerline(first(pinned.evaluated).shape,.5).position;
    check(original.x==after.x&&original.z==after.z,"Explicit manual guide horizontal constraints were not preserved");
    auto bad=settings;bad.stage=2;check(generate_cloud_state(initial,bad).status==GenerationStatus::failed,"Out-of-range stage was silently clamped");
    bad=settings;bad.wind.back().displacement={501,0,0};check(generate_cloud_state(initial,bad).status==GenerationStatus::failed,"Wind limit was not enforced");
    bad=settings;bad.wind.back().altitude=0;check(generate_cloud_state(initial,bad).status==GenerationStatus::failed,"Degenerate wind profile was accepted");
    bad=settings;bad.cells.front().cell_id=99999;check(generate_cloud_state(initial,bad).status==GenerationStatus::failed,"Unknown growth target was accepted");
    bad=settings;bad.cells.front().amount=std::numeric_limits<double>::quiet_NaN();check(generate_cloud_state(initial,bad).status==GenerationStatus::failed,"Nonfinite growth was accepted");
    std::stop_source stop;unsigned checkpoints=0;const auto cancelled=generate_cloud_state(initial,settings,stop.get_token(),[&](double p){++checkpoints;if(p>.1)stop.request_stop();});
    check(checkpoints>=2&&cancelled.status==GenerationStatus::cancelled&&!cancelled.candidate,"Mid-generation cancellation published partial state");
    check(generate(initial,settings).evaluated==pinned.evaluated,"Cancellation destroyed the next reproducible result");
    EditorSession session(initial);session.apply(pinned.evaluated);check(session.undo()&&session.document().scene()==initial&&session.redo()&&session.document().scene()==pinned.evaluated,"Explicit candidate adoption is not one Undo step");
    settings.enabled=false;Scene custom;check(generate(custom,settings).evaluated==custom,"Disabled growth changed the legacy static Recipe");
}
void write_fixtures(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);auto initial=fixture();
    nlohmann::json manifest={{"algorithm_version",generation_algorithm_version},{"stage_unit","dimensionless"},{"wind_unit","object-local metres at stage 1; not metres/second"},{"fixtures",nlohmann::json::array()}};
    save_scene_atomic(initial,directory/"initial.white.json");
    for(bool windy:{false,true})for(double stage:{.35,1.}){
        auto settings=default_generation_settings(initial);settings.stage=stage;if(windy)settings.wind={{0,{0,0,0}},{.5,{20,0,5}},{1,{70,0,25}}};
        auto c=generate(initial,settings);const std::string name=std::string(windy?"shear":"calm")+(stage==1?"-mature":"-young");
        save_scene_atomic(c.evaluated,directory/(name+"-front.white.json"));auto side=c.evaluated;side.camera.position={380,80,40};side.camera.target={40,70,0};save_scene_atomic(side,directory/(name+"-side.white.json"));
        const SceneDensityEvaluator field(c.evaluated);const auto bounds=field.local_support();
        manifest["fixtures"].push_back({{"name",name},{"stage",stage},{"input_hash",std::to_string(c.input_hash)},{"generation_cpu_ms",c.elapsed_ms},{"selected_height_m",first(c.evaluated).shape.source.parameters.height},{"density_bound",field.maximum()},{"support_min",{bounds.min.x,bounds.min.y,bounds.min.z}},{"support_max",{bounds.max.x,bounds.max.y,bounds.max.z}}});
    }
    auto steady=default_generation_settings(initial);steady.wind={{0,{40,0,0}},{1,{40,0,0}}};save_scene_atomic(generate(initial,steady).evaluated,directory/"constant-wind.white.json");
    std::ofstream out(directory/"generation-manifest.json");out<<manifest.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot write generation fixture manifest");
}
}
int main(int argc,char** argv){try{comparisons();guides_independent_cells_and_failures();if(argc>1)write_fixtures(argv[1]);std::cout<<"Growth/wind: stages, shear, independent onset, pinned guides, base, stable IDs, support, cancellation, Undo and standalone selected-state save passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
