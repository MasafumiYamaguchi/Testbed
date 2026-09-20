#include "white/frozen_cloud.hpp"
#include "white/generation.hpp"
#include "white/anvil_scene.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <stdexcept>
using namespace white;
namespace {
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char* message){try{fn();}catch(const std::exception&){return;}throw std::runtime_error(message);}
Scene source(bool top,bool anvil){auto scene=new_anvil_scene(new_cumulonimbus_scene({}));scene.anvil->cloud.settings.mode=top?TopLobeMode::children:TopLobeMode::off;scene.anvil->settings.enabled=anvil;refresh_anvil_scene(scene);return scene;}
Scene fixed(Scene initial,double wind=0){auto settings=default_generation_settings(initial);settings.stage=.8;settings.wind.back().displacement={wind,0,wind*.2};const auto outcome=generate_cloud_state(initial,settings);check(outcome.status==GenerationStatus::completed,outcome.message.c_str());return freeze_candidate(outcome);}
void equality(){
    for(bool top:{false,true})for(bool anvil:{false,true}){
        const auto initial=source(top,anvil);auto settings=default_generation_settings(initial);settings.stage=.8;settings.wind.back().displacement={50,0,10};
        const auto outcome=generate_cloud_state(initial,settings);check(outcome.status==GenerationStatus::completed,outcome.message.c_str());
        const auto scene=freeze_candidate(outcome);const SceneDensityEvaluator before(outcome.candidate->evaluated),after(scene);const auto b=before.local_support();
        check(scene_density_requires_direct(scene)==(top||anvil),"Frozen cache eligibility differs from its complete packet");
        check(scene.frozen&&after.local_support()==b&&after.maximum()==before.maximum(),"Freeze changed support or density bound");
        double worst=0;for(int z=0;z<17;++z)for(int y=0;y<23;++y)for(int x=0;x<17;++x){const Vec3 p{b.min.x+(b.max.x-b.min.x)*x/16,b.min.y+(b.max.y-b.min.y)*y/22,b.min.z+(b.max.z-b.min.z)*z/16};worst=std::max(worst,std::abs(before.at(p)-after.at(p)));}
        check(worst<=2e-14,"Freeze changed the evaluated field");
        const auto calls=generation_job_count();auto standalone=scene;standalone.frozen->provenance.reset();refresh_frozen_scene(standalone);
        check(standalone.frozen->content_hash==scene.frozen->content_hash,"Removing provenance changed the structural identity");
        const SceneDensityEvaluator independent(standalone);check(independent.at({20,75,0})==after.at({20,75,0})&&generation_job_count()==calls,"Evaluation without history reran growth");
        std::cout<<"freeze_equality top="<<top<<" anvil="<<anvil<<" max_error="<<worst<<" history_free=true PASS\n";
    }
    auto independent=new_developed_scene(new_cumulonimbus_scene({}));auto second=make_developed_cell(*independent.developed);second.translation={60,0,0};independent=scene_with_developed_command(independent,DevelopedAdd{second});
    const auto frozen=fixed(independent,30);check(frozen.frozen->fields.size()==2&&!frozen.frozen->top_enabled,"Independent developments were flattened");
}
void detail_and_lifecycle(){
    auto scene=fixed(source(true,true),45);const auto original=scene;const auto calls=generation_job_count();const auto content=scene.frozen->content_hash,cache=density_input_hash(scene);const auto& top=scene.frozen->fields[1];auto noise=top.recipe.noise;noise.medium_strength=.9;noise.micro_erosion=2;
    scene=scene_with_frozen_detail(scene,top.development_id,noise,top.recipe.detail_seed+91,top.layers);
    check(scene.frozen->content_hash==content&&density_input_hash(scene)!=cache&&scene.frozen->payload_hash!=original.frozen->payload_hash,"Detail changed geometry or failed to invalidate density");
    check(scene.frozen->curves==original.frozen->curves&&scene.frozen->hierarchy==original.frozen->hierarchy&&scene.frozen->anvil==original.frozen->anvil&&scene.frozen->selection_value==original.frozen->selection_value,"Detail regenerated structural metadata");
    for(std::size_t i=0;i<scene.frozen->fields.size();++i)check(scene.frozen->fields[i].recipe.cells==original.frozen->fields[i].recipe.cells,"Detail reseeded structural lobes");
    const SceneDensityEvaluator before(original),after(scene);
    for(int y=-2;y<60;++y)for(int x=-50;x<90;x+=7)check(before.at({double(x),double(y),7})==after.at({double(x),double(y),7}),"Top detail changed the protected lower region");
    auto optics=scene;optics.frozen->optics.albedo=.7;refresh_frozen_scene(optics);check(!has(classify_change(scene,optics),Dirty::density)&&has(classify_change(scene,optics),Dirty::optics),"Optics unnecessarily invalidated frozen density");
    optics.camera.position.x+=1;optics.sun.irradiance.x+=.1;optics.exposure_ev=1;require_valid(optics);
    check(density_input_hash(optics)==density_input_hash(scene),"Camera/light/optics changed frozen density key");
    check(density_job_hash(scene,{128,128,128})!=density_job_hash(scene,{256,256,256})&&scene.frozen->content_hash==content,"Resampling changed the selected state");
    check(generation_job_count()==calls,"Finish or cache changes started growth");
    for(int layer=0;layer<4;++layer){auto layers=top.layers;if(layer==0)layers.base=false;if(layer==1)layers.macro=false;if(layer==2)layers.medium=false;if(layer==3)layers.micro=false;
        const auto changed=scene_with_frozen_detail(original,top.development_id,top.recipe.noise,top.recipe.detail_seed,layers);const auto effective=frozen_effective_recipe(changed.frozen->fields[1]);
        check(scene_density_requires_direct(changed)&&gpu_scene_density_params(changed).field.cloud.fields.settings.x==2,"Top layer toggle broke the direct cache-sample packet contract");
        check(changed.frozen->content_hash==content,"Layer toggle changed frozen content identity");check((layer!=0||effective.density==0)&&(layer!=1||effective.noise.warp_amplitude==0)&&(layer!=2||effective.noise.medium_strength==0)&&(layer!=3||effective.noise.micro_erosion==0),"Layer toggle failed");
    }
    noise.origin.x+=1;rejects([&]{scene_with_frozen_detail(scene,top.development_id,noise,17,{});},"Detail silently changed frozen reference coordinates");
    EditorSession session(original);session.apply(scene);check(session.undo()&&session.document().scene()==original&&session.redo()&&session.document().scene()==scene,"Frozen detail Undo/Redo lost state");
    const auto initial=generation_initial_scene(*scene.frozen);auto settings=scene.frozen->provenance->settings;settings.stage=.4;const auto candidate=generate_cloud_state(initial,settings);const auto next=freeze_candidate(candidate);session.apply(next);
    check(session.undo()&&session.document().scene()==scene&&session.redo()&&session.document().scene()==next,"Explicit new candidate did not preserve the previous success");
    std::stop_source stop;stop.request_stop();const auto cancelled=generate_cloud_state(initial,settings,stop.get_token());rejects([&]{freeze_candidate(cancelled);},"Cancelled job was Freezable");
    auto bad=settings;bad.stage=2;const auto failed=generate_cloud_state(initial,bad);rejects([&]{freeze_candidate(failed);},"Failed job was Freezable");check(session.document().scene()==next,"Failed or cancelled generation replaced frozen state");
    std::cout<<"freeze_lifecycle detail_seed_and_layers_geometry_stable=true no_finish_growth=true explicit_replace_undo=true cancel_preserved=true PASS\n";
}
void provenance_hash(){
    auto initial=source(false,false);const auto settings=default_generation_settings(initial);const auto hash=generation_input_hash(initial,settings);initial.anvil->settings.extension+=3;refresh_anvil_scene(initial);
    check(generation_input_hash(initial,settings)!=hash,"Generation provenance ignored dormant anvil source controls");
}
void fixtures(const std::filesystem::path& dir){
    std::filesystem::create_directories(dir);
    for(bool windy:{false,true}){auto initial=source(true,true);auto settings=default_generation_settings(initial);settings.stage=.8;if(windy)settings.wind.back().displacement={50,0,10};
        const auto outcome=generate_cloud_state(initial,settings);check(outcome.status==GenerationStatus::completed,outcome.message.c_str());
        for(bool side:{false,true}){auto before=outcome.candidate->evaluated,after=freeze_candidate(outcome);Camera camera;camera.target={35,65,0};camera.position=side?Vec3{385,80,0}:Vec3{35,80,350};before.camera=after.camera=camera;
            const auto name=std::string(windy?"wind-":"calm-")+(side?"side":"front");save_scene_atomic(before,dir/(name+"-selected.white.json"));save_scene_atomic(after,dir/(name+"-frozen.white.json"));
        }
    }
}
}
int main(int argc,char** argv){try{equality();detail_and_lifecycle();provenance_hash();if(argc>1)fixtures(argv[1]);std::cout<<"FrozenCloudState contract PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
