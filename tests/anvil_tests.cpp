#include "white/anvil_scene.hpp"
#include "white/centerline_scene.hpp"
#include "white/generation.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;using Json=nlohmann::json;
namespace {
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F>void reject(F f,const char* message){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,message);}
Scene fixture(){
    auto scene=new_anvil_scene(new_developed_scene(new_centerline_scene(new_cumulonimbus_scene())));
    scene.camera.position={50,85,390};scene.camera.target={50,70,0};
    auto& cell=scene.anvil->cloud.trunk.cells.front();cell.shape.source.modifiers.noise={};
    refresh_anvil_scene(scene);return scene;
}
Scene enabled(Scene scene){auto s=scene.anvil->settings;s.enabled=true;return scene_with_anvil_settings(scene,s);}
GenerationCandidate generate(const Scene& scene,const GenerationSettings& settings){auto out=generate_cloud_state(scene,settings);if(!out.candidate)throw std::runtime_error(out.message);return *out.candidate;}
void fields(){
    auto off=fixture();const auto plain=new_developed_scene(off);const SceneDensityEvaluator before(plain),disabled(off);
    const auto a=before.gpu_params(),b=disabled.gpu_params();
    check(off.cloud==plain.cloud&&density_input_hash(off)==density_input_hash(plain)&&std::memcmp(&a,&b,sizeof(a))==0,"Disabled anvil changed legacy field/cache packet");
    for(const auto direction:std::array{Vec3{1,0,0},Vec3{0,0,-1},Vec3{.6,0,.8}})for(double shear:std::array{-1.5,0.,2.}){
        auto scene=off;auto s=scene.anvil->settings;s.enabled=true;s.direction=direction;s.shear=shear;scene=scene_with_anvil_settings(scene,s);
        const AnvilEvaluationPlan field(*scene.anvil);const SceneDensityEvaluator full(scene);const auto bounds=field.local_support();
        check(scene_density_requires_direct(scene)&&scene.cloud.envelope==bounds&&full.maximum()==field.maximum(),"Full evaluator omitted anvil support/maximum");
        std::size_t additions=0;
        for(int y=-1;y<=145;y+=2)for(int x=-100;x<=220;x+=4)for(int z=-100;z<=100;z+=8){
            const Vec3 p{double(x),double(y),double(z)};const double value=full.at(p);
            check(std::isfinite(value)&&value>=0&&value<=field.maximum(),"Anvil exceeded finite density bound");
            if(p.y<=s.start_height)check(value==before.at(p),"Anvil changed protected lower density");
            if(value>before.at(p)+.01)++additions;
            if(value>0)check(p.x>=bounds.min.x&&p.x<=bounds.max.x&&p.y>=bounds.min.y&&p.y<=bounds.max.y&&p.z>=bounds.min.z&&p.z<=bounds.max.z,"Anvil clipped positive density outside support");
        }
        check(additions>10,"Anvil did not create horizontal extension");
        auto point=field.connection_point();check(before.at(point)>0&&field.at(point)>=before.at(point),"Anvil disconnected or removed trunk density");
        for(int i=0;i<=50;++i){const auto p=point+s.direction*(s.extension*.5*i/50.);check(field.at(p)>0,"Anvil neck contains a detached gap");}
    }
    auto scene=enabled(off);auto& cell=scene.anvil->cloud.trunk.cells.front();cell.translation={18,7,-13};
    cell.shape.source.modifiers.cuts.push_back({991,{38,94,0},{7,7,7},1});refresh_anvil_scene(scene);
    const SceneDensityEvaluator cut(scene);check(cut.at(Vec3{38,94,0}+cell.translation)==0,"Anvil refilled a complete cut");
    auto zero=scene.anvil->settings;zero.density_scale=0;scene=scene_with_anvil_settings(scene,zero);check(!scene_has_active_anvil(scene),"Zero-density anvil forced nonzero evaluator");
}
void persistence_and_commands(){
    auto scene=enabled(fixture());const auto original=scene;
    const auto handle=anvil_handle_position(*scene.anvil,AnvilHandle::width);auto wide=scene_with_anvil_handle(scene,AnvilHandle::width,handle+Vec3{0,0,20});
    auto settings=scene.anvil->settings;settings.width+=40;check(wide==scene_with_anvil_settings(scene,settings),"Width handle differs from inspector command");
    const auto origin=anvil_connection_point(*scene.anvil);auto turned=scene_with_anvil_handle(scene,AnvilHandle::direction,origin+Vec3{0,0,100});
    check(turned.anvil->settings.direction==Vec3{0,0,1}&&!turned.anvil->settings.follow_wind&&turned.anvil->cloud==scene.anvil->cloud,"Direction override changed trunk or did not disable following");
    check(density_input_hash(scene)!=density_input_hash(wide)&&has(classify_change(scene,wide),Dirty::density),"Anvil edit omitted density invalidation/hash");
    auto optics=scene;optics.anvil->cloud.trunk.optics.g=.7;refresh_anvil_scene(optics);check(classify_change(scene,optics)==Dirty::optics&&density_input_hash(scene)==density_input_hash(optics),"Optics changed anvil density identity");
    const auto bytes=scene_json(scene);const auto json=Json::parse(bytes);check(json.at("schema_version")==11&&json.at("cloud").at("kind")=="anvil"&&json.at("cloud").size()==2&&parse_scene_json(bytes)==scene,"Anvil source roundtrip lost authority");
    auto bad=json;bad["schema_version"]=8;reject([&]{parse_scene_json(bad.dump());},"Old schema accepted new anvil type");
    bad=json;bad["cloud"]["source"]["contract_version"]=99;reject([&]{parse_scene_json(bad.dump());},"Future anvil version accepted");
    bad=json;bad["cloud"]["source"]["settings"]["enabled"]=1;reject([&]{parse_scene_json(bad.dump());},"Malformed enabled value accepted");
    for(auto legacy:std::array{Scene{},new_cumulonimbus_scene(),new_centerline_scene(),new_developed_scene(fixture())}){auto j=Json::parse(scene_json(legacy));j["schema_version"]=8;check(parse_scene_json(j.dump())==legacy,"Schema8 migration changed static scene");}
    for(int n=0;n<7;++n){auto s=scene.anvil->settings;
        if(n==0)s.thickness=0;if(n==1)s.thickness=1000;if(n==2)s.start_height=-1;if(n==3)s.width=1;if(n==4)s.direction={};if(n==5)s.extension=100000;if(n==6)s.edge_fade=std::numeric_limits<double>::quiet_NaN();
        reject([&]{scene_with_anvil_settings(scene,s);},"Invalid anvil input accepted");check(scene==original,"Invalid edit mutated original");
    }
    reject([&]{scene_with_anvil_handle(scene,AnvilHandle::direction,origin);},"Zero direction handle accepted");
    reject([&]{new_developed_scene(scene);},"Active anvil silently discarded");
    EditorSession editor(scene);editor.begin_drag();editor.apply(wide);editor.apply(turned);editor.end_drag();check(editor.undo()&&editor.document().scene()==scene&&!editor.can_undo(),"Anvil gesture Undo split or lost source");check(editor.redo()&&editor.document().scene()==turned,"Anvil redo failed");
    editor.begin_drag();editor.apply(wide);editor.cancel_drag();check(editor.document().scene()==turned,"Canceled handle replaced last state");
}
void growth(){
    auto scene=enabled(fixture());auto settings=default_generation_settings(scene);const auto calm=generate(scene,settings);
    settings.wind={{0,{}},{.5,{0,0,20}},{1,{0,0,80}}};const auto wind=generate(scene,settings);
    check(wind.evaluated.anvil->settings.direction==Vec3{0,0,1}&&wind.evaluated.anvil->settings.extension>calm.evaluated.anvil->settings.extension,"Wind did not orient/stretch anvil");
    const auto& evaluated=wind.evaluated.anvil->cloud.trunk.cells.front();const auto& s=wind.evaluated.anvil->settings;const auto& p=evaluated.shape.source.parameters;
    const auto anchor=sample_centerline(evaluated.shape,(s.start_height+s.thickness*.5-p.cloud_base)/p.height).position+evaluated.translation;
    check(anvil_connection_point(*wind.evaluated.anvil)==anchor,"Wind displaced attachment a second time");
    auto manual=scene;manual.anvil->settings.follow_wind=false;refresh_anvil_scene(manual);const auto override=generate(manual,settings);
    check(override.evaluated.anvil->settings.direction==manual.anvil->settings.direction&&override.evaluated.anvil->settings.extension==calm.evaluated.anvil->settings.extension,"Manual direction did not override anvil wind rules");
    check(override.evaluated.anvil->cloud==wind.evaluated.anvil->cloud,"Anvil override changed wind-bent trunk/top");
    settings.stage=.25;const auto early=generate(scene,settings);check(early.evaluated.anvil->settings.density_scale==0&&!scene_has_active_anvil(early.evaluated),"Anvil appeared before growth onset");
    settings.stage=.65;const auto middle=generate(scene,settings);check(middle.evaluated.anvil->settings.width<wind.evaluated.anvil->settings.width&&middle.evaluated.anvil->settings.thickness<wind.evaluated.anvil->settings.thickness,"Selected stage did not change independent horizontal/vertical reach");
    check(parse_scene_json(scene_json(wind.evaluated))==wind.evaluated,"Wind-bent anvil requires generation to restore");
    auto top=scene;top.anvil->cloud.settings.mode=TopLobeMode::children;refresh_anvil_scene(top);const auto combined=generate(top,settings);check(scene_has_active_top_lobes(combined.evaluated)&&scene_has_active_anvil(combined.evaluated),"Top/anvil generation did not coexist");
}
void precision_domain(){
    // A 100 km object-local translation has float spacing much wider than the
    // sharp anvil edge can tolerate. All signs/axes need the same policy.
    const auto original=fixture();
    for(const auto offset:std::array{Vec3{100000,0,0},Vec3{-100000,0,0},Vec3{0,0,100000},Vec3{0,0,-100000}}){
        auto distant=original;distant.anvil->cloud.trunk.cells.front().translation=offset;
        refresh_anvil_scene(distant); // Disabled metadata retains legacy domain.
        const auto before=distant;
        reject([&]{enabled(distant);},"Distant horizontal anvil bypassed GPU edge precision validation");
        check(distant==before,"Rejected precision edit changed source");
    }
    // Object placement is applied outside the object-local density kernel and
    // must not be confused with a large source coordinate.
    auto placed=enabled(original);placed.anvil->cloud.trunk.transform.translation={100000,0,-100000};
    refresh_anvil_scene(placed);const AnvilEvaluationPlan field(*placed.anvil),baseline(*enabled(original).anvil);
    check(field.local_support()==baseline.local_support()&&field.at(field.connection_point())==baseline.at(baseline.connection_point()),"World placement changed the local anvil precision domain");
    check(anvil_edge_error_bound(*original.anvil)==0&&anvil_edge_error_bound(*placed.anvil)==anvil_edge_error_bound(*enabled(original).anvil),"Edge allowance leaked into disabled/world-placement state");

    auto thin=original;auto& source=*thin.anvil;auto& settings=source.settings;
    source.cloud.trunk.cells.front().shape.source.modifiers.overlap=0;
    settings.enabled=true;settings.thickness=2;settings.width=100;settings.extension=360;settings.edge_fade=.2;
    refresh_anvil_scene(thin);const AnvilEvaluationPlan precise(source);const TopLobeEvaluationPlan trunk(source.cloud);
    const auto gpu=precise.gpu_params();const double allowance=anvil_edge_error_bound(source)*settings.density_scale;
    const auto center=precise.connection_point()+settings.direction*(settings.extension*.5);
    const auto& source_cell=source.cloud.trunk.cells.front();
    check(anvil_edge_error_bound(settings,center,precise.local_support(),source_cell.shape.source.modifiers.noise,source_cell.translation)==anvil_edge_error_bound(source),"Evaluated edge allowance differs from source allowance");
    double maximum_error=0;
    // Differential precision regression: points through the sheet's steepest
    // edge compare the double evaluator with the packed float shader formula.
    // This isolates new sheet arithmetic while retaining the same trunk value.
    for(int i=1;i<10000;++i){
        const double theta=i*.001;const Vec3 p{gpu.center.x+230*.9*std::cos(theta),gpu.center.y+.9*std::sin(theta),0};
        const float u=(float(p.x)-gpu.center.x)/gpu.dimensions.x,w=(float(p.y)-gpu.center.y)/gpu.dimensions.z;
        const float distance=(std::sqrt(u*u+w*w)-1)*gpu.dimensions.z;
        const float t=std::clamp((distance+gpu.dimensions.w)/gpu.dimensions.w,0.f,1.f);
        const double addition=gpu.cloud.fields.groups[0].settings.z*gpu.settings.y*(1-t*t*(3-2*t));
        maximum_error=std::max(maximum_error,std::abs(precise.at(p)-std::max(trunk.at(p),addition)));
    }
    check(maximum_error>1e-5&&maximum_error<=allowance,"Float sheet edge error escaped its declared coverage bound");
    auto wider=source;wider.settings.edge_fade=.25;
    check(anvil_edge_error_bound(wider)<anvil_edge_error_bound(source),"Wider fade did not reduce edge amplification");
    auto erosion=source;erosion.cloud.trunk.cells.front().shape.source.modifiers.noise.micro_erosion=20;
    erosion.cloud.trunk.cells.front().shape.source.modifiers.noise.micro_frequency=2;
    check(anvil_edge_error_bound(erosion)>max_anvil_edge_error&&!validate_anvil(erosion).empty(),"Narrow fade accepted unbounded inherited erosion precision");
}
void fixtures(const std::filesystem::path& dir){
    std::filesystem::create_directories(dir);const auto off=fixture();auto narrow=enabled(off);narrow.anvil->settings.width=70;narrow.anvil->settings.extension=30;refresh_anvil_scene(narrow);
    auto wide=narrow;wide.anvil->settings.width=130;wide.anvil->settings.extension=100;refresh_anvil_scene(wide);
    auto tilted=wide;tilted.anvil->settings.direction={.6,0,.8};tilted.anvil->settings.shear=1.5;refresh_anvil_scene(tilted);
    auto settings=default_generation_settings(wide);settings.wind={{0,{}},{.5,{10,0,15}},{1,{65,0,45}}};auto wind=generate(wide,settings).evaluated;
    auto manual=wide;manual.anvil->settings.follow_wind=false;refresh_anvil_scene(manual);manual=generate(manual,settings).evaluated;
    settings.stage=.6;auto young=generate(wide,settings).evaluated;
    auto top=wide;top.anvil->cloud.settings.mode=TopLobeMode::children;refresh_anvil_scene(top);settings.stage=1;top=generate(top,settings).evaluated;
    Json manifest=Json::array();
    for(const auto& [name,value]:std::array<std::pair<const char*,Scene>,8>{{{"off",off},{"narrow",narrow},{"wide",wide},{"tilted",tilted},{"wind",wind},{"manual",manual},{"young",young},{"top-wind",top}}}){
        for(const auto view:{"front","side"}){auto scene=value;if(std::string(view)=="side")scene.camera.position={390,85,0};const auto filename=std::string(name)+"-"+view+".white.json";save_scene_atomic(scene,dir/filename);manifest.push_back({{"name",std::string(name)+"-"+view},{"recipe",filename},{"density_hash",std::to_string(density_input_hash(scene))}});}
    }
    std::ofstream(dir/"manifest.json")<<manifest.dump(2)<<'\n';
}
}
int main(int argc,char** argv){try{fields();persistence_and_commands();growth();precision_domain();if(argc==2)fixtures(argv[1]);std::cout<<"Anvil lower density exact, neck continuity, finite support/maximum, commands, source roundtrip, stage/wind/override and top integration PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
