#include "white/top_lobe_scene.hpp"
#include "white/centerline_scene.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void reject(F f,const char* message){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,message);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("white-top-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
}
int main(){try {
    const auto trunk=new_developed_scene(new_centerline_scene(new_cumulonimbus_scene()));
    const auto off=new_top_lobe_scene(trunk);check(off.top_lobes&&!off.developed&&!off.centerline&&!off.cumulonimbus,"Competing top source authority");
    check(off.cloud==trunk.cloud&&density_input_hash(off)==density_input_hash(trunk)&&!scene_density_requires_direct(off),"OFF changed legacy Recipe/cache/direct mode");
    const auto before_gpu=gpu_scene_density_params(trunk),off_gpu=gpu_scene_density_params(off);
    check(std::memcmp(&before_gpu,&off_gpu,sizeof(off_gpu))==0,"OFF changed legacy GPU packet");
    auto parent_settings=off.top_lobes->settings;parent_settings.mode=TopLobeMode::parent;
    auto parent=scene_with_top_lobe_settings(off,parent_settings);check(scene_has_active_top_lobes(parent)&&scene_density_requires_direct(parent),"Active hierarchy did not require complete direct evaluation");
    auto children_settings=parent_settings;children_settings.mode=TopLobeMode::children;
    auto scene=scene_with_top_lobe_settings(parent,children_settings);scene.top_lobes->lobe_ids[2]=std::numeric_limits<Id>::max();refresh_top_lobe_scene(scene);
    check(scene.schema_version==10&&scene.algorithm_version==3,"Wrong source/evaluation version");
    const SceneDensityEvaluator full(scene);const TopLobeEvaluationPlan expected(*scene.top_lobes);const auto gpu=full.gpu_params();const auto reference_gpu=expected.gpu_params();
    check(full.maximum()==expected.maximum()&&full.local_support()==expected.local_support()&&scene.cloud.envelope==full.local_support(),"Scene wrapper/proxy omitted hierarchy bounds");
    check(std::memcmp(&gpu.cloud,&reference_gpu,sizeof(reference_gpu))==0,"Scene wrapper omitted hierarchy mask/GPU data");
    std::size_t samples=0;for(int y=0;y<190;y+=3)for(int x=-60;x<65;x+=5){const Vec3 p{double(x),double(y),0};check(full.at(p)==expected.at(p),"Scene wrapper ignored hierarchy density");++samples;}
    const auto bytes=scene_json(scene);const auto encoded=Json::parse(bytes);check(encoded.at("cloud").size()==2&&encoded.at("cloud").at("kind")=="top_lobes","Top source not saved as single authority");
    check(encoded.at("cloud").at("source").at("lobe_ids")[2]=="18446744073709551615"&&parse_scene_json(bytes)==scene&&scene_json(parse_scene_json(bytes))==bytes,"Reserved uint64 IDs/source roundtrip changed");
    for(const auto& mode_scene:std::array{off,parent,scene})check(parse_scene_json(scene_json(mode_scene))==mode_scene,"Comparison mode source failed roundtrip");
    auto changed_settings=children_settings;changed_settings.parent_radius+=1;
    auto changed=scene_with_top_lobe_settings(scene,changed_settings);check(has(classify_change(scene,changed),Dirty::density)&&density_input_hash(scene)!=density_input_hash(changed),"Hierarchy edit omitted from dirty/hash");
    auto optical=scene;optical.top_lobes->trunk.optics.g=.7;refresh_top_lobe_scene(optical);check(classify_change(scene,optical)==Dirty::optics&&density_input_hash(scene)==density_input_hash(optical),"Shared optics dirtied hierarchy density key");
    auto display=scene;display.exposure_ev+=1;display.camera.position.x+=1;check(density_input_hash(display)==density_input_hash(scene),"Camera/exposure changed density key");
    const auto target=scene.top_lobes->target_cell;auto taller=scene_with_developed_command(scene,DevelopedEdit{target,CumulonimbusCommand{CumulonimbusParameter::height,140.0}});
    check(taller.top_lobes->lobe_ids==scene.top_lobes->lobe_ids&&taller.top_lobes->target_cell==target&&taller.top_lobes->settings==scene.top_lobes->settings,"Trunk edit replaced generator source/IDs");
    reject([&]{scene_with_developed_command(scene,DevelopedRemove{target});},"Target deletion silently retargeted hierarchy");
    reject([&]{new_developed_scene(scene);},"Active hierarchy silently flattened");check(new_developed_scene(off)==trunk,"Explicit OFF conversion changed trunk");
    auto two=trunk;auto second=make_developed_cell(*two.developed);second.translation.x=80;two=scene_with_developed_command(two,DevelopedAdd{second});
    auto off_two=new_top_lobe_scene(two);check(scene_density_requires_direct(off_two)&&density_input_hash(off_two)==density_input_hash(two),"OFF two-group source lost evaluator/cache identity");
    reject([&]{scene_with_top_lobe_settings(off_two,parent_settings);},"Two-trunk active hierarchy exceeded kernel capacity");
    for(const auto& old:std::array{Scene{},new_cumulonimbus_scene(),new_centerline_scene(),trunk,two}){auto j=Json::parse(scene_json(old));j["schema_version"]=7;check(parse_scene_json(j.dump())==old,"Schema7 migration changed old source");}
    auto stale=scene;stale.cloud.envelope.max.y+=1;reject([&]{require_valid(stale);},"Stale proxy accepted");
    auto both=scene;both.developed=scene.top_lobes->trunk;reject([&]{scene_json(both);},"Duplicate authority accepted");
    auto invalid=encoded;invalid["cloud"]["source"]["contract_version"]=2;reject([&]{parse_scene_json(invalid.dump());},"Future top contract accepted");
    invalid=encoded;invalid["cloud"]["source"]["settings"]["mode"]="future";reject([&]{parse_scene_json(invalid.dump());},"Unknown comparison mode accepted");
    invalid=encoded;invalid["cloud"]["source"]["settings"]["child_limit"]=3;reject([&]{parse_scene_json(invalid.dump());},"Unbounded child generation accepted");
    invalid=encoded;invalid["cloud"]["source"]["lobe_ids"][0]=invalid["cloud"]["source"]["lobe_ids"][1];reject([&]{parse_scene_json(invalid.dump());},"Reserved ID collision accepted");
    invalid=encoded;invalid["schema_version"]=7;reject([&]{parse_scene_json(invalid.dump());},"Legacy schema accepted new source kind");
    EditorSession editor(off);editor.begin_drag();editor.apply(parent);editor.apply(scene);editor.end_drag();check(editor.undo()&&editor.document().scene()==off&&!editor.can_undo(),"Hierarchy gesture did not group Undo");
    check(editor.redo()&&editor.document().scene()==scene,"Hierarchy Redo lost source");editor.begin_drag();editor.apply(taller);editor.cancel_drag();check(editor.document().scene()==scene,"Canceled trunk edit lost hierarchy source");
    Temp temp;const auto path=temp.path/"top.white.json";editor.save(path);editor.apply(changed);reject([&]{editor.save(path,SaveFault::before_publish);},"Save failure was not injected");check(read_scene(path)==scene&&editor.modified(),"Failed save clobbered source");
    const auto bad=temp.path/"bad.json";{std::ofstream out(bad);out<<invalid.dump();}reject([&]{editor.load(bad,true);},"Malformed source load accepted");check(editor.document().scene()==changed,"Failed load replaced source");
    std::cout<<"Top-lobe schema8: "<<samples<<" full evaluator samples; OFF exact Recipe/hash/GPU; active source-only roundtrip/IDs; schemas1..7 via suites; dirty/hash/optics; bounded commands, Undo and rollback passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
