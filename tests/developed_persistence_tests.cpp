#include "white/developed_scene.hpp"
#include "white/centerline_scene.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace white;using Json=nlohmann::json;
namespace {
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F>void reject(F f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
struct TemporaryDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("white-developed-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory(){if(!std::filesystem::create_directory(path))throw std::runtime_error("Cannot create temporary directory");}
    ~TemporaryDirectory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
int main(){try {
    auto original=new_centerline_scene();original.centerline->profile[1].density_scale=.6;
    original.centerline->source.parameters.detail_seed=std::numeric_limits<std::uint64_t>::max();
    original.cloud=lower_centerline_to_recipe(*original.centerline);
    auto single=new_developed_scene(original);
    check(single.developed&&single.developed->cells.size()==1&&!single.centerline&&!single.cumulonimbus,"Conversion retained competing source");
    check(single.cloud==original.cloud&&density_input_hash(single)==density_input_hash(original),"Single development changed exact legacy Recipe/cache identity");
    check(!scene_density_requires_direct(single),"Single development unnecessarily disables legacy cache");
    const auto legacy_gpu=gpu_scene_density_params(original),single_gpu=gpu_scene_density_params(single);
    check(std::memcmp(&legacy_gpu,&single_gpu,sizeof(single_gpu))==0,"Single development changed GPU packet");
    const auto old_field=SceneDensityEvaluator(original),new_field=SceneDensityEvaluator(single);
    for(int y=5;y<115;y+=3)for(int x=-30;x<=30;x+=3)check(old_field.at({double(x),double(y),0})==new_field.at({double(x),double(y),0}),"Single development changed legacy density");
    auto added=make_developed_cell(*single.developed);added.translation={75,2,0};added.shape.source.parameters.height=95;added.shape.source.parameters.width=65;
    added.shape.source.parameters.structure_seed=9007199254740993ULL;added.shape.source.parameters.detail_seed=77;
    added.shape.points[1].offset={5,0,-3};added.shape.profile={{1,0,1,.8},{2,.5,1.2,.5},{3,1,.9,.7}};
    auto scene=scene_with_developed_command(single,DevelopedAdd{added});
    check(scene.schema_version==8&&scene.algorithm_version==3&&scene_density_requires_direct(scene),"Two developments lack schema7/direct evaluation contract");
    check(scene.developed->cells[0]==single.developed->cells[0],"Add replaced first development");
    const SceneDensityEvaluator full(scene);const DevelopedEvaluationPlan reference(*scene.developed);
    check(full.local_support()==reference.local_support()&&scene.cloud.envelope==full.local_support(),"Proxy does not carry full union support");
    check(full.maximum()==reference.maximum()&&full.world_support()==reference.world_support(),"Scene wrapper lost grouped bounds/maximum");
    double second_peak=0;
    for(int y=10;y<100;y+=5)for(int x=55;x<=100;x+=5){const Vec3 p{double(x),double(y),0};check(full.at(p)==reference.at(p),"Scene wrapper lost independent density group");second_peak=std::max(second_peak,full.at(p));}
    check(second_peak>0,"Second development missing from density evaluation");
    const auto packet=gpu_scene_density_params(scene);const auto expected_packet=reference.gpu_params();
    check(packet.fields.settings.x==2&&std::memcmp(&packet.fields,&expected_packet,sizeof(expected_packet))==0,"GPU packet did not contain both developments");
    const auto bytes=scene_json(scene);const auto encoded=Json::parse(bytes);
    check(encoded.at("cloud").size()==2&&encoded.at("cloud").at("kind")=="developed"&&!encoded.at("cloud").contains("recipe"),"Developed persistence stored a second recipe authority");
    check(parse_scene_json(bytes)==scene&&scene_json(parse_scene_json(bytes))==bytes,"Developed source round-trip is not exact/deterministic");
    auto reordered=scene;std::reverse(reordered.developed->cells.begin(),reordered.developed->cells.end());refresh_developed_scene(reordered);
    check(density_input_hash(reordered)==density_input_hash(scene),"Development storage order changed density cache key");
    const auto second_id=added.id;
    const auto changed=scene_with_developed_command(scene,DevelopedEdit{second_id,CumulonimbusCommand{CumulonimbusParameter::height,115.0}});
    check(changed.developed->cells[0]==scene.developed->cells[0]&&has(classify_change(scene,changed),Dirty::density)&&density_input_hash(changed)!=density_input_hash(scene),"Second-group edit was omitted from dirty/hash or changed first group");
    auto optical=scene;optical.developed->optics.g=.7;refresh_developed_scene(optical);
    check(classify_change(scene,optical)==Dirty::optics&&density_input_hash(scene)==density_input_hash(optical),"Optics incorrectly dirtied grouped density/hash");
    auto display=scene;display.exposure_ev+=1;display.camera.position.x+=1;
    check(density_input_hash(display)==density_input_hash(scene),"Camera/exposure affected grouped density hash");
    auto profile_edit=scene_with_developed_command(scene,DevelopedEdit{second_id,CenterlineSetProfile{2,1.2,.2}});
    check(density_input_hash(profile_edit)!=density_input_hash(scene),"Secondary profile omitted from density key");
    auto moved=scene_with_developed_command(scene,DevelopedMove{second_id,{80,2,0}});check(density_input_hash(moved)!=density_input_hash(scene),"Secondary translation omitted from density key");
    auto stale=scene;stale.cloud.envelope.max.x+=1;reject([&]{require_valid(stale);},"Stale metadata proxy accepted");
    auto both=scene;both.cumulonimbus=CumulonimbusGroup{};reject([&]{scene_json(both);},"Competing source accepted");
    auto invalid=encoded;invalid["cloud"]["source"]["contract_version"]=2;reject([&]{parse_scene_json(invalid.dump());},"Future developed contract accepted");
    invalid=encoded;invalid["cloud"]["source"]["cells"].push_back(invalid["cloud"]["source"]["cells"][1]);reject([&]{parse_scene_json(invalid.dump());},"Third development accepted");
    invalid=encoded;invalid["cloud"]["source"]["cells"][1]["roles"]={0,1,2,3};reject([&]{parse_scene_json(invalid.dump());},"Nine-primitive budget accepted");
    invalid=encoded;invalid["cloud"]["source"]["cells"][1]["id"]=invalid["cloud"]["source"]["cells"][0]["id"];reject([&]{parse_scene_json(invalid.dump());},"Duplicate development ID accepted");
    invalid=encoded;invalid["cloud"]["source"]["cells"][1]["roles"][0]=-1;reject([&]{parse_scene_json(invalid.dump());},"Negative role accepted");
    invalid=encoded;invalid["cloud"]["source"]["cells"][1]["shape"]["source"]["modifiers"]["optics"]["g"]=.8;reject([&]{parse_scene_json(invalid.dump());},"Conflicting per-development optics accepted");
    invalid=encoded;invalid["schema_version"]=6;reject([&]{parse_scene_json(invalid.dump());},"Legacy schema accepted new source kind");
    // Prior schema6 supports all old source kinds without inferring grouping.
    for(const auto& old:std::array{Scene{},new_cumulonimbus_scene(),original}) {
        auto j=Json::parse(scene_json(old));j["schema_version"]=6;const auto migrated=parse_scene_json(j.dump());
        check(migrated==old&&!migrated.developed,"Schema6 migration changed existing source/recipe");
    }
    EditorSession editor(scene);editor.begin_drag();editor.apply(moved);editor.apply(changed);editor.end_drag();
    check(editor.undo()&&editor.document().scene()==scene&&!editor.can_undo(),"Developed gesture was not one Undo");
    check(editor.redo()&&editor.document().scene()==changed,"Developed Redo lost independent source");
    auto removed=scene_with_developed_command(changed,DevelopedRemove{second_id});editor.apply(removed);
    check(removed.developed->cells.size()==1&&!scene_density_requires_direct(removed)&&removed.developed->cells[0]==single.developed->cells[0],"Delete changed remaining development or failed to restore cache path");
    check(editor.undo()&&editor.document().scene()==changed,"Delete could not be undone");
    auto empty=scene_with_developed_command(removed,DevelopedRemove{removed.developed->cells[0].id});
    check(SceneDensityEvaluator(empty).maximum()==0&&SceneDensityEvaluator(empty).at({0,20,0})==0&&parse_scene_json(scene_json(empty))==empty,"Empty developed cloud did not persist/render transparent");
    const auto before_bad=editor.document().scene();const auto revision=editor.document().revision();
    reject([&]{editor.apply(stale);},"Editor accepted stale proxy");check(editor.document().scene()==before_bad&&editor.document().revision()==revision,"Invalid source changed document");
    TemporaryDirectory directory;const auto path=directory.path/"developed.white.json";editor.save(path);
    editor.apply(profile_edit);reject([&]{editor.save(path,SaveFault::before_publish);},"Save fault not injected");
    check(read_scene(path)==before_bad&&editor.modified(),"Failed save clobbered prior independent source");
    const auto bad_path=directory.path/"invalid.json";{std::ofstream out(bad_path);out<<invalid.dump();}
    const auto before_load=editor.document().scene();reject([&]{editor.load(bad_path,true);},"Malformed source load succeeded");
    check(editor.document().scene()==before_load,"Failed load replaced independent source");
    std::cout<<"Developed schema7: source-only independent groups; exact single legacy/cache/GPU path; full two-group evaluator/hash; shared optics; schemas1..6 compatibility via suites; source Undo/delete/empty and atomic rollback passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
