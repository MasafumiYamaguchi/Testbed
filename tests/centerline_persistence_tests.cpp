#include "white/centerline_scene.hpp"
#include "white/persistence.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace white;
using Json=nlohmann::json;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void reject(F f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
struct TemporaryDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("white-centerline-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory(){if(!std::filesystem::create_directory(path))throw std::runtime_error("Cannot create temporary directory");}
    ~TemporaryDirectory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
int main(){try {
    auto scene=new_centerline_scene();auto& shape=*scene.centerline;
    shape.points={{9007199254740993ULL,0,{}},{9007199254740994ULL,.5,{6,0,-4}},{std::numeric_limits<Id>::max(),1,{12,0,2}}};
    shape.profile={{9007199254740993ULL,0,1,1},{9007199254740994ULL,.35,.75,.4},{std::numeric_limits<Id>::max(),1,1.1,.9}};
    shape.source.parameters.detail_seed=std::numeric_limits<std::uint64_t>::max();
    shape.source.cell_adjustments={{shape.source.cell_ids[1],{3,-2,5},{1.1,.9,1.2},9007199254740993ULL}};
    shape.source.modifiers.noise.origin={2,3,4};shape.source.modifiers.manual_cells.push_back({100,{45,50,0},{5,6,7},9007199254740993ULL});
    shape.source.modifiers.cuts.push_back({101,{10,40,0},{4,5,6},1});
    scene.cloud=lower_centerline_to_recipe(shape);require_valid(scene);
    check(scene.schema_version==9&&scene.algorithm_version==3&&scene.cloud.altitude_density.enabled,"New centerline lacks schema7 algorithm3 full density profile");
    const auto bytes=scene_json(scene);const auto encoded=Json::parse(bytes);const auto& cloud=encoded.at("cloud");
    check(cloud.size()==2&&cloud.at("kind")=="centerline"&&!cloud.contains("recipe")&&!cloud.contains("altitude_density"),"Centerline serialized competing generated authority");
    check(cloud.at("source").at("points")[0].at("id").is_string()&&cloud.at("source").at("profile")[2].at("id")=="18446744073709551615","Curve/profile IDs lost uint64 precision");
    check(parse_scene_json(bytes)==scene&&scene_json(parse_scene_json(bytes))==bytes,"Centerline source/derived profile round-trip failed");
    auto custom=custom_cloud_scene(scene);check(!custom.centerline&&!custom.cumulonimbus&&custom.cloud==scene.cloud,"Custom conversion dropped density profile");
    const auto custom_encoded=Json::parse(scene_json(custom));check(custom_encoded.at("cloud").contains("altitude_density"),"Custom cloud did not persist full profile");
    check(parse_scene_json(custom_encoded.dump())==custom,"Custom altitude profile round-trip failed");
    // Every old Custom version maps to disabled modulation and exact density.
    std::size_t migrated_samples=0;
    for(unsigned version=1;version<=5;++version) {
        Scene expected;auto legacy=Json::parse(scene_json(expected));legacy["schema_version"]=version;
        legacy["algorithm_version"]=version==1?1u:2u;legacy["cloud"].erase("altitude_density");
        if(version<4)legacy.erase("preview_approx");
        if(version<=2)legacy["cloud"]["optics"].erase("g");
        if(version==1)legacy["cloud"].erase("noise");
        const auto migrated=parse_scene_json(legacy.dump());
        check(migrated==expected&&!migrated.centerline&&!migrated.cumulonimbus&&!migrated.cloud.altitude_density.enabled,"Legacy migration changed appearance defaults or inferred a source");
        const DensityField old_field(expected.cloud),new_field(migrated.cloud);
        for(int y=-10;y<90;y+=2)for(int x=-30;x<=30;x+=3){check(old_field.at({double(x),double(y),0})==new_field.at({double(x),double(y),0}),"Legacy density changed under algorithm3 migration");++migrated_samples;}
    }
    auto prefab=new_cumulonimbus_scene();prefab.cumulonimbus->cell_adjustments={{2,{2,3,4},{1,1.1,.9},{}}};
    prefab.cloud=derive_cumulonimbus_recipe(*prefab.cumulonimbus);
    auto old_prefab=Json::parse(scene_json(prefab));old_prefab["schema_version"]=5;old_prefab["algorithm_version"]=2;
    const auto migrated_prefab=parse_scene_json(old_prefab.dump());
    check(migrated_prefab==prefab&&migrated_prefab.cumulonimbus&&!migrated_prefab.centerline,"Schema5 prefab source/edits were not retained");
    auto stale=scene;stale.cloud.altitude_density.knots[1].scale=.7;reject([&]{require_valid(stale);},"Changed derived profile accepted as second source");
    stale=scene;stale.centerline->points[1].offset.x+=1;reject([&]{scene_json(stale);},"Stale curve snapshot was saved");
    auto both=scene;both.cumulonimbus=scene.centerline->source;reject([&]{require_valid(both);},"Two authoritative sources accepted");
    auto invalid=encoded;invalid["cloud"]["recipe"]=custom_encoded["cloud"];reject([&]{parse_scene_json(invalid.dump());},"Duplicate generated Recipe accepted");
    invalid=encoded;invalid["cloud"]["source"]["contract_version"]=2;reject([&]{parse_scene_json(invalid.dump());},"Future curve contract accepted");
    invalid=encoded;invalid["cloud"]["source"]["source"]["contract_version"]=2;reject([&]{parse_scene_json(invalid.dump());},"Future nested prefab contract accepted");
    invalid=encoded;invalid["cloud"]["source"]["points"][1]["id"]=invalid["cloud"]["source"]["points"][0]["id"];reject([&]{parse_scene_json(invalid.dump());},"Duplicate curve ID accepted");
    invalid=encoded;invalid["cloud"]["source"]["points"][1]["t"]=0;reject([&]{parse_scene_json(invalid.dump());},"Degenerate curve interval accepted");
    invalid=encoded;invalid["cloud"]["source"]["points"][1]["offset"][1]=2;reject([&]{parse_scene_json(invalid.dump());},"Curve control changed independent base/height coordinate");
    invalid=encoded;invalid["cloud"]["source"]["profile"][1]["density_scale"]=1.1;reject([&]{parse_scene_json(invalid.dump());},"Density profile bound violation accepted");
    invalid=encoded;invalid["cloud"]["source"]["points"][0]["id"]=9007199254740993ULL;reject([&]{parse_scene_json(invalid.dump());},"Numeric curve ID accepted");
    invalid=encoded;invalid["schema_version"]=5;invalid["algorithm_version"]=2;reject([&]{parse_scene_json(invalid.dump());},"Legacy schema accepted new curve evaluation contract");
    invalid=encoded;invalid["algorithm_version"]=2;reject([&]{parse_scene_json(invalid.dump());},"New schema interpreted old evaluation algorithm");
    invalid=encoded;invalid["algorithm_version"]=4;reject([&]{parse_scene_json(invalid.dump());},"Unknown evaluation algorithm accepted");
    invalid=custom_encoded;invalid["cloud"]["altitude_density"]["height"]=0;reject([&]{parse_scene_json(invalid.dump());},"Invalid custom profile height accepted");
    invalid=custom_encoded;invalid["cloud"]["altitude_density"]["enabled"]=false;invalid["cloud"]["altitude_density"]["knots"][1]["t"]=0;reject([&]{parse_scene_json(invalid.dump());},"Invalid disabled profile escaped validation");
    invalid=custom_encoded;invalid["schema_version"]=5;invalid["algorithm_version"]=2;reject([&]{parse_scene_json(invalid.dump());},"Legacy migration silently overwrote unexpected profile data");
    EditorSession editor(scene);const auto start_revision=editor.document().revision();reject([&]{editor.apply(stale);},"Editor accepted unsynchronized source");
    check(editor.document().scene()==scene&&editor.document().revision()==start_revision&&!editor.can_undo(),"Invalid source edit changed history/state");
    editor.begin_drag();
    for(double x:{7.0,8.0,9.0}) {
        const auto local=sample_centerline(*editor.document().scene().centerline,.5).position;
        editor.apply(scene_with_centerline_command(editor.document().scene(),CenterlineMovePoint{shape.points[1].id,{x,local.y,-4}}));
    }
    editor.end_drag();const auto curved=editor.document().scene();
    check(curved.centerline->source==shape.source&&curved.centerline->profile==shape.profile,"Curve edit changed unrelated source/profile");
    check(editor.undo()&&editor.document().scene()==scene&&!editor.can_undo(),"Curve gesture did not produce single Undo");
    check(editor.redo()&&editor.document().scene()==curved,"Curve Redo failed");
    const auto point_id=curved.centerline->profile[1].id;
    editor.apply(scene_with_centerline_command(curved,CenterlineSetProfile{point_id,.9,.25}));const auto profiled=editor.document().scene();
    check(profiled.cloud.base==curved.cloud.base&&profiled.centerline->points==curved.centerline->points&&profiled.centerline->source.modifiers.noise.origin==shape.source.modifiers.noise.origin,"Profile edit moved cloud base, curve or noise origin");
    check(editor.undo()&&editor.document().scene()==curved,"Profile edit not undoable");
    editor.begin_drag();editor.apply(profiled);editor.cancel_drag();check(editor.document().scene()==curved&&editor.can_redo(),"Canceled profile edit lost redo/source");
    const auto id=editor.add_cell({0,{50,70,0},{5,6,7},9});check(editor.document().scene().centerline->source.modifiers.manual_cells.back().id==id,"Centerline manual add did not update source");
    check(editor.remove_cell(id)&&editor.document().scene()==curved,"Centerline manual remove did not preserve source");
    reject([&]{editor.remove_cell(curved.centerline->source.cell_ids[0]);},"Generated centerline cell deleted without explicit conversion");
    TemporaryDirectory directory;const auto path=directory.path/"centerline.white.json";
    editor.save(path);check(read_scene(path)==curved&&!editor.modified(),"Centerline disk persistence failed");
    editor.apply(profiled);const auto before_failure=editor.document().scene();const auto before_failure_revision=editor.document().revision();
    reject([&]{editor.save(path,SaveFault::before_publish);},"Injected save fault ignored");
    check(read_scene(path)==curved&&editor.document().scene()==before_failure&&editor.modified(),"Failed save clobbered prior source");
    const auto invalid_path=directory.path/"invalid.white.json";{std::ofstream out(invalid_path);out<<invalid.dump();}
    reject([&]{editor.load(invalid_path,true);},"Invalid curve load succeeded");
    check(editor.document().scene()==before_failure&&editor.document().revision()==before_failure_revision,"Invalid load clobbered source/revision");
    editor.load(path,true);check(editor.document().scene()==curved&&!editor.modified()&&!editor.can_undo(),"Saved curve load failed to restore exact source and clear session history");
    std::cout<<"Centerline schema7 / density algorithm3: source-only curves and custom profiles round-trip; "<<migrated_samples<<" legacy density samples unchanged; schemas1..5 migration; uint64 IDs; profile/curve Undo and atomic rollback passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
