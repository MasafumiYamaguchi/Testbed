#include "white/persistence.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
using namespace white;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
    auto rejects=[&](auto action,const char* label){try{action();check(false,label);}catch(const std::exception&){check(true,label);}};
    auto root=std::filesystem::temp_directory_path()/ ("white-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        Scene scene;scene.cloud.detail_seed=std::numeric_limits<std::uint64_t>::max();
        scene.cloud.cells[0].structure_seed=9007199254740993ULL;
        scene.cloud.cuts.push_back({3,{2,3,4},{5,6,7},0.5});
        scene.preview_approx.enabled=true;scene.preview_approx.strength=0.625;
        scene.camera.position={44,50,66};scene.sun.direction_to_light={1,0,0};scene.exposure_ev=-1.5;
        auto json=scene_json(scene);check(parse_scene_json(json)==scene,"exact round-trip including uint64 seeds");
        rejects([&]{parse_scene_json("{");},"truncated JSON rejected");
        auto future=json;auto pos=future.find("\"schema_version\": 11");future.replace(pos,20,"\"schema_version\": 12");
        rejects([&]{parse_scene_json(future);},"future version rejected");
        rejects([&]{parse_scene_json(std::string(max_scene_bytes+1,' '));},"input size cap");
        rejects([&]{parse_scene_json(std::string(40,'[')+"0"+std::string(40,']'));},"nesting cap");
        auto invalid=json;pos=invalid.find("\"albedo\": 0.9");invalid.replace(pos,13,"\"albedo\": 1.5");
        rejects([&]{parse_scene_json(invalid);},"invalid decoded optics rejected");
        const auto path=root/"scene.white.json";
        EditorSession editor(scene);check(editor.modified(),"new scene is unsaved");
        editor.save(path);check(!editor.modified()&&read_scene(path)==scene,"save marks exact scene clean");
        const auto id=editor.add_cell();const auto added=editor.document().scene();const auto r=editor.document().revision();
        check(editor.modified()&&added.cloud.cells.back().id==id,"add cell command");
        check(editor.undo()&&editor.document().scene()==scene&&editor.document().revision()>r&&!editor.modified(),"undo uses newer revision and returns to saved state");
        check(editor.redo()&&editor.document().scene()==added,"redo reproduces scene");
        check(editor.remove_cell(id)&&editor.document().scene()==scene,"remove cell command");
        editor.undo();editor.undo();auto branch=editor.document().scene();branch.exposure_ev=2;editor.apply(branch);
        check(!editor.can_redo(),"branch edit discards redo");
        const auto before_drag=editor.document().scene();editor.begin_drag();
        for(int i=0;i<20;++i){auto next=editor.document().scene();next.cloud.cells[0].radii.x=21+i;editor.apply(next);}
        const auto dragged=editor.document().scene();editor.end_drag();editor.undo();
        check(editor.document().scene()==before_drag,"drag collapses twenty edits into one undo");
        editor.redo();check(editor.document().scene()==dragged,"drag redo");
        editor.begin_drag();auto next=dragged;next.cloud.cells[0].center.x++;editor.apply(next);
        const auto cancel_revision=editor.document().revision();editor.cancel_drag();
        check(editor.document().scene()==dragged&&editor.document().revision()>cancel_revision,"cancel restores with newer revision");
        const auto stable=editor.document().scene();const auto revision=editor.document().revision();
        rejects([&]{editor.load(path);},"load refuses implicit discard of unsaved changes");
        check(editor.document().scene()==stable,"unsaved scene preserved");
        const auto bad_path=root/"broken.json";{std::ofstream out(bad_path);out<<"{";}
        rejects([&]{editor.load(bad_path,true);},"broken load rejected");
        check(editor.document().scene()==stable&&editor.document().revision()==revision,"failed load preserves scene and revision");
        rejects([&]{editor.save(path,SaveFault::before_write);},"write failure injected");
        check(read_scene(path)==scene&&editor.modified(),"write failure preserves previous file and dirty state");
        rejects([&]{editor.save(path,SaveFault::before_publish);},"publish failure injected");
        check(read_scene(path)==scene,"publish failure preserves previous file");
        rejects([&]{editor.save(root/"missing"/"scene.json");},"missing parent fails safely");
        auto dir=root/"directory.json";std::filesystem::create_directory(dir);
        rejects([&]{editor.save(dir);},"destination directory cannot be overwritten");
        bool no_temps=true;for(auto& p:std::filesystem::directory_iterator(root))if(p.path().string().find(".tmp-")!=std::string::npos)no_temps=false;
        check(no_temps,"temporary files cleaned on failures");
        editor.save(path);check(read_scene(path)==stable&&!editor.modified(),"atomic overwrite succeeds");
        editor.add_cell();editor.load(path,true);check(editor.document().scene()==stable&&!editor.modified()&&!editor.can_undo()&&!editor.can_redo(),"explicit load resets history and dirty state");
        editor.begin_drag();rejects([&]{editor.save(path);},"save during drag refused");editor.cancel_drag();
        auto invalid_scene=stable;invalid_scene.cloud.cells[0].radii.x=0;
        rejects([&]{editor.apply(invalid_scene);},"invalid command rejected");check(editor.document().scene()==stable,"invalid command preserves scene");
        for(int i=0;i<150;++i){auto n=editor.document().scene();n.exposure_ev=(i%2)?1:2;editor.apply(n);}
        int undos=0;while(editor.undo())++undos;check(undos==128,"history bounded at 128 commands");
    } catch(const std::exception& e) {std::cerr<<"Unexpected exception: "<<e.what()<<'\n';++failures;}
    std::filesystem::remove_all(root);
    return failures?1:0;
}
