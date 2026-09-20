#include "white/cumulonimbus.hpp"
#include "white/persistence.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace white;
using Json=nlohmann::json;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void reject(F f,const char* message) {
    bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);
}
struct TemporaryDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("white-prefab-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory(){if(!std::filesystem::create_directory(path))throw std::runtime_error("Cannot create test directory");}
    ~TemporaryDirectory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
int main(){try {
    auto scene=new_cumulonimbus_scene();auto& source=*scene.cumulonimbus;
    source.cloud_id=9007199254740993ULL;source.cell_ids={9007199254740994ULL,9007199254740995ULL,9007199254740996ULL,9007199254740997ULL,9007199254740998ULL};
    source.parameters.structure_seed=std::numeric_limits<std::uint64_t>::max();source.parameters.detail_seed=9007199254740993ULL;
    source.parameters.growth_direction={0.6,0.8,0};source.parameters.width=110;source.parameters.height=155;
    source.cell_adjustments={{source.cell_ids[1],{4,3,-2},{1.1,0.9,1.2},std::numeric_limits<std::uint64_t>::max()},
                             {source.cell_ids[4],{-3,1,5},{0.8,1.1,1},{}}};
    source.modifiers.manual_cells.push_back({9007199254740999ULL,{40,55,10},{7,8,9},9007199254740993ULL});
    source.modifiers.cuts.push_back({9007199254741000ULL,{12,40,-3},{4,5,6},1.5});
    source.modifiers.noise.origin={5,-3,8};source.modifiers.optics.g=0.6;source.modifiers.transform.translation={20,30,10};
    scene.preview_approx={true,0.25};scene.cloud=derive_cumulonimbus_recipe(source);require_valid(scene);
    const auto bytes=scene_json(scene);const auto encoded=Json::parse(bytes);
    check(encoded.at("schema_version")==10&&encoded.at("cloud").size()==2&&encoded.at("cloud").at("kind")=="cumulonimbus","Prefab not encoded as source-only schema7 object");
    check(!encoded.at("cloud").contains("cells")&&!encoded.at("cloud").contains("envelope")&&!encoded.at("cloud").contains("recipe"),"Derived Recipe serialized as second authority");
    check(encoded.at("cloud").at("source").at("cloud_id").is_string(),"Stable ID serialized numerically");
    check(parse_scene_json(bytes)==scene,"Prefab source and derived snapshot did not round-trip exactly");
    check(scene_json(parse_scene_json(bytes))==bytes,"Source serialization not deterministic");
    const auto custom=custom_cloud_scene(scene);const auto custom_json=Json::parse(scene_json(custom));
    check(!custom.cumulonimbus&&custom.cloud==scene.cloud&&!custom_json.at("cloud").contains("kind"),"Custom conversion changed recipe or serialized prefab state");
    check(parse_scene_json(custom_json.dump())==custom,"Custom schema7 round-trip failed");
    // Every supported prior schema migrates to a Custom Cloud, never guessed
    // from the shape. Older versions use their historical defaults exactly.
    for(unsigned version=1;version<=4;++version) {
        Scene old;old.cloud.noise={};old.cloud.optics.g=0;old.preview_approx={};
        auto legacy=Json::parse(scene_json(old));legacy["schema_version"]=version;legacy["algorithm_version"]=2u;legacy["cloud"].erase("altitude_density");
        if(version<4)legacy.erase("preview_approx");
        if(version<=2)legacy["cloud"]["optics"].erase("g");
        if(version==1){legacy["algorithm_version"]=1u;legacy["cloud"].erase("noise");}
        const auto migrated=parse_scene_json(legacy.dump());
        check(migrated==old&&migrated.schema_version==10&&!migrated.cumulonimbus,"Legacy Custom Cloud migration changed values or inferred prefab");
    }
    auto stale=scene;stale.cloud.cells[0].center.x+=1;reject([&]{require_valid(stale);},"Stale generated snapshot was accepted");
    reject([&]{scene_json(stale);},"Stale generated snapshot was saved");
    stale=scene;stale.cumulonimbus->parameters.height+=1;reject([&]{require_valid(stale);},"Changed source without derived refresh was accepted");
    auto invalid=encoded;invalid["cloud"]["recipe"]=custom_json["cloud"];reject([&]{parse_scene_json(invalid.dump());},"Duplicate recipe authority accepted");
    invalid=encoded;invalid["cloud"]["kind"]="unknown";reject([&]{parse_scene_json(invalid.dump());},"Unknown object kind accepted");
    invalid=encoded;invalid["cloud"]["source"]["contract_version"]=2;reject([&]{parse_scene_json(invalid.dump());},"Unknown prefab contract accepted");
    invalid=encoded;invalid["cloud"]["source"]["parameters"]["height"]=0;reject([&]{parse_scene_json(invalid.dump());},"Invalid decoded height accepted");
    invalid=encoded;invalid["cloud"]["source"]["parameters"]["detail_seed"]=9007199254740993ULL;reject([&]{parse_scene_json(invalid.dump());},"Numeric seed accepted");
    invalid=encoded;invalid["cloud"]["source"]["parameters"]["detail_seed"]="18446744073709551616";reject([&]{parse_scene_json(invalid.dump());},"Overflowing seed accepted");
    invalid=encoded;invalid["cloud"]["source"]["cell_ids"][0]=invalid["cloud"]["source"]["cell_ids"][1];reject([&]{parse_scene_json(invalid.dump());},"Duplicate stable cell ID accepted");
    invalid=encoded;invalid["cloud"]["source"]["cell_adjustments"][0]["cell_id"]="999";reject([&]{parse_scene_json(invalid.dump());},"Dangling local edit accepted");
    invalid=encoded;invalid["cloud"]["source"]["modifiers"]["base_enabled"]=1;reject([&]{parse_scene_json(invalid.dump());},"Nonboolean modifier accepted");
    invalid=encoded;invalid["cloud"]["source"]["cell_ids"].push_back("99");reject([&]{parse_scene_json(invalid.dump());},"Extra generated Cell ID accepted");
    invalid=encoded;invalid["cloud"]["source"]["modifiers"]["manual_cells"]=Json::array({custom_json["cloud"]["cells"][0],custom_json["cloud"]["cells"][1],custom_json["cloud"]["cells"][2],custom_json["cloud"]["cells"][3]});
    reject([&]{parse_scene_json(invalid.dump());},"Oversized manual cell array accepted");
    EditorSession editor(scene);const auto baseline=editor.document().scene();const auto revision=editor.document().revision();
    reject([&]{editor.apply(stale);},"Editor accepted stale snapshot");
    check(editor.document().scene()==baseline&&editor.document().revision()==revision&&!editor.can_undo(),"Invalid edit changed scene/revision/history");
    editor.begin_drag();
    for(double height:{160.0,180.0,200.0})editor.apply(scene_with_cumulonimbus_command(editor.document().scene(),{CumulonimbusParameter::height,height}));
    editor.end_drag();const auto taller=editor.document().scene();
    check(taller.cumulonimbus->cell_ids==baseline.cumulonimbus->cell_ids&&taller.cumulonimbus->cell_adjustments==baseline.cumulonimbus->cell_adjustments&&taller.cumulonimbus->modifiers==baseline.cumulonimbus->modifiers,"High-level command destroyed local source edits");
    check(editor.undo()&&editor.document().scene()==baseline&&!editor.can_undo(),"EditorSession did not group prefab drag into one Undo");
    check(editor.redo()&&editor.document().scene()==taller,"EditorSession Redo failed to restore both source and render snapshot");
    auto reset=scene_with_cumulonimbus_command(taller,{CumulonimbusParameter::height,cumulonimbus_value(CumulonimbusParameters{},CumulonimbusParameter::height)});
    editor.apply(reset);check(editor.undo()&&editor.document().scene()==taller,"Prefab reset not undoable through EditorSession");
    editor.begin_drag();editor.apply(reset);editor.cancel_drag();check(editor.document().scene()==taller&&editor.can_redo(),"Canceled prefab reset lost source or redo");
    const auto id=editor.add_cell({0,{80,60,0},{8,9,10},77});const auto added=editor.document().scene();
    check(added.cumulonimbus->modifiers.manual_cells.back().id==id&&added.cloud.cells.back().id==id,"Add Cell did not update source and derived recipe");
    check(editor.remove_cell(id)&&editor.document().scene()==taller,"Remove manual Cell did not preserve prefab");
    const auto before_delete=editor.document().scene();const auto before_delete_revision=editor.document().revision();
    reject([&]{editor.remove_cell(before_delete.cumulonimbus->cell_ids[0]);},"Generated Cell deletion implicitly changed prefab contract");
    check(editor.document().scene()==before_delete&&editor.document().revision()==before_delete_revision,"Rejected generated-cell delete changed state");
    editor.apply(custom_cloud_scene(before_delete));check(!editor.document().scene().cumulonimbus,"Explicit conversion did not change object type");
    check(editor.undo()&&editor.document().scene()==before_delete,"Explicit conversion could not restore source via Undo");
    TemporaryDirectory directory;const auto path=directory.path/"prefab.white.json";
    editor.save(path);check(read_scene(path)==before_delete&&!editor.modified(),"Prefab disk save/load failed");
    editor.apply(scene_with_cumulonimbus_command(editor.document().scene(),{CumulonimbusParameter::width,140.0}));
    const auto modified=editor.document().scene();const auto modified_revision=editor.document().revision();
    reject([&]{editor.save(path,SaveFault::before_publish);},"Injected prefab publish failure did not occur");
    check(read_scene(path)==before_delete&&editor.document().scene()==modified&&editor.modified(),"Failed save clobbered old prefab or source");
    const auto bad_path=directory.path/"invalid.white.json";{std::ofstream out(bad_path);out<<invalid.dump();}
    reject([&]{editor.load(bad_path,true);},"Invalid prefab load accepted");
    check(editor.document().scene()==modified&&editor.document().revision()==modified_revision,"Failed load replaced source or revision");
    editor.load(path,true);check(editor.document().scene()==before_delete&&!editor.can_undo()&&!editor.can_redo()&&!editor.modified(),"Successful prefab load did not restore source/reset session state");
    std::cout<<"Cumulonimbus Scene schema7: source-only round-trip; uint64 values; schemas1..4 migration; stale snapshot rejection; EditorSession Undo/add/remove/conversion; atomic failure preservation passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
