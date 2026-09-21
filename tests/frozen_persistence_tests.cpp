#include "white/frozen_cloud.hpp"
#include "white/generation.hpp"
#include "white/developed_scene.hpp"
#include "white/top_lobe_scene.hpp"
#include "white/anvil_scene.hpp"
#include "white/centerline_scene.hpp"
#include "white/persistence.hpp"
#include "white/revision_queue.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace white;
using Json=nlohmann::json;
namespace {
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F>void reject(F action,const char* message){bool bad=false;try{action();}catch(const std::exception&){bad=true;}check(bad,message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("white-frozen-persistence-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
std::string bytes(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void reseal_payload(Json& scene) {
    auto source=scene["cloud"]["source"];source.erase("payload_hash");
    std::uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned char byte:source.dump()){hash^=byte;hash*=UINT64_C(1099511628211);}
    scene["cloud"]["source"]["payload_hash"]=std::to_string(hash);
}
void copy_recomputed_hashes(Json& scene,FrozenCloudState state) {
    state.content_hash=frozen_content_hash(state);state.payload_hash=frozen_payload_hash(state);
    scene["cloud"]["source"]["content_hash"]=std::to_string(state.content_hash);
    scene["cloud"]["source"]["payload_hash"]=std::to_string(state.payload_hash);
}
void same_field(const Scene& a,const Scene& b,bool same_packet=true) {
    const SceneDensityEvaluator first(a),second(b);
    check(first.local_support()==second.local_support()&&first.world_support()==second.world_support()&&first.maximum()==second.maximum(),"Frozen roundtrip changed support or rho_max");
    const auto x=first.gpu_params(),y=second.gpu_params();
    if(same_packet)check(std::memcmp(&x,&y,sizeof(x))==0,"Frozen roundtrip changed GPU packet");
    for(int z=-60;z<=60;z+=20)for(int y0=-10;y0<=180;y0+=5)for(int x0=-70;x0<=190;x0+=10){
        const Vec3 p{double(x0),double(y0),double(z)};const auto a0=first.at(p),b0=second.at(p);
        check(same_packet?a0==b0:std::abs(a0-b0)<=2e-14,"Frozen roundtrip changed evaluated density");
    }
}
Scene freeze_static(Scene source) {
    GenerationSettings settings;settings.enabled=false;
    auto result=generate_cloud_state(source,settings);
    check(result.status==GenerationStatus::completed&&result.candidate.has_value(),"Static candidate failed");
    return freeze_candidate(result);
}
Json frozen_v1_json(const Scene& scene) {
    auto encoded=Json::parse(scene_json(scene));auto legacy=*scene.frozen;
    legacy.contract_version=1;
    for(auto& field:legacy.fields)field.clipping_envelope.reset();
    auto& source=encoded["cloud"]["source"];source["contract_version"]=1;
    for(auto& field:source["fields"])field.erase("clipping_envelope");
    copy_recomputed_hashes(encoded,legacy);
    return encoded;
}
Scene detailed_frozen() {
    auto source=new_anvil_scene(new_developed_scene(new_centerline_scene(new_cumulonimbus_scene())));
    auto& cloud=source.anvil->cloud;cloud.settings.mode=TopLobeMode::children;
    cloud.lobe_ids.back()=std::numeric_limits<Id>::max();
    auto& group=cloud.trunk.cells.front().shape.source;
    group.parameters.detail_seed=std::numeric_limits<std::uint64_t>::max();
    group.modifiers.noise.origin={13,2,-7};group.modifiers.noise.medium_strength=.2;group.modifiers.noise.micro_erosion=.6;group.modifiers.noise.warp_amplitude=.5;
    source.anvil->settings.enabled=true;refresh_anvil_scene(source);
    auto settings=default_generation_settings(source);settings.stage=.8;
    settings.wind={{0,{}},{.5,{10,0,5}},{1,{45,0,20}}};
    auto result=generate_cloud_state(source,settings);
    check(result.status==GenerationStatus::completed&&result.candidate.has_value(),"Detailed anvil generation failed");
    auto frozen=freeze_candidate(result);same_field(result.candidate->evaluated,frozen,false);return frozen;
}
void roundtrip_and_compatibility() {
    const auto scene=detailed_frozen();const auto jobs=generation_job_count();
    const auto text=scene_json(scene);const auto json=Json::parse(text);const auto& source=json.at("cloud").at("source");
    check(json.at("schema_version")==11&&json.at("cloud").size()==3&&json.at("cloud").at("kind")=="frozen","Frozen state lacks one schema11 frozen authority plus finishing");
    check(!source.contains("assets")&&!source.contains("path")&&source.at("fields").size()==2&&source.at("hierarchy").size()==3&&!source.at("anvil").is_null(),"Frozen file omitted evaluated structure or added external storage");
    check(source.at("hierarchy").back().at("id")=="18446744073709551615","Frozen uint64 hierarchy identity lost precision");
    const auto loaded=parse_scene_json(text);
    check(loaded==scene&&scene_json(loaded)==text,"Frozen state/provenance did not roundtrip exactly");same_field(scene,loaded);
    check(frozen_can_regenerate(*loaded.frozen),"Known provenance cannot be regenerated explicitly");
    check(generation_job_count()==jobs,"Frozen save/load/evaluation reran a generation job");

    const auto& field=scene.frozen->fields.front();auto noise=field.recipe.noise;noise.medium_strength=.4;noise.warp_amplitude+=.25;
    auto layers=field.layers;layers.micro=false;
    const auto detailed=scene_with_frozen_detail(scene,field.development_id,noise,12345,layers);
    check(detailed.frozen->content_hash==scene.frozen->content_hash&&detailed.frozen->payload_hash!=scene.frozen->payload_hash,"Detail edit changed frozen geometry identity or omitted payload invalidation");
    check(density_input_hash(detailed)!=density_input_hash(scene),"Effective detail edit did not change density cache identity");
    const auto restored_detail=parse_scene_json(scene_json(detailed));check(restored_detail==detailed,"Detail layers/seeds did not roundtrip");same_field(detailed,restored_detail);
    auto optical=scene;optical.frozen->optics.g=.5;refresh_frozen_scene(optical);
    check(optical.frozen->content_hash==scene.frozen->content_hash&&density_input_hash(optical)==density_input_hash(scene)&&optical.frozen->payload_hash!=scene.frozen->payload_hash,"Optics changed frozen structure/density identity or lost payload checksum");
    check(parse_scene_json(scene_json(optical))==optical,"Frozen optics did not roundtrip");
    check(generation_job_count()==jobs,"Detail/optics edit or restoration reran generation");

    auto unknown=scene;unknown.frozen->generation_version=999;
    unknown.frozen->provenance->settings.algorithm_version=999;
    refresh_frozen_scene(unknown);
    const auto recovered=parse_scene_json(scene_json(unknown));
    check(recovered==unknown&&!frozen_can_regenerate(*recovered.frozen),"Unknown generation version did not preserve fixed data and disable regeneration");
    check(recovered.frozen->content_hash==scene.frozen->content_hash&&density_input_hash(recovered)==density_input_hash(scene),"Generation version changed fixed geometry/density identity");
    same_field(scene,recovered);
    auto detached=scene;detached.frozen->provenance.reset();refresh_frozen_scene(detached);
    const auto without_recipe=parse_scene_json(scene_json(detached));
    check(without_recipe==detached&&!frozen_can_regenerate(*without_recipe.frozen)&&without_recipe.frozen->content_hash==scene.frozen->content_hash,"Provenance-free fixed state is not independently usable");
    same_field(scene,without_recipe);
    check(generation_job_count()==jobs,"Compatibility load or field evaluation reran generation");

    auto base=new_cumulonimbus_scene();auto center=new_centerline_scene(base);auto developed=new_developed_scene(center);
    auto second=make_developed_cell(*developed.developed);second.translation={65,0,0};
    auto two=scene_with_developed_command(developed,DevelopedAdd{second});
    auto top=new_top_lobe_scene(developed);auto anvil=new_anvil_scene(top);
    for(const auto& original:std::array{Scene{},base,center,two,top,anvil}) {
        const auto fixed=freeze_static(original);const auto count=generation_job_count();
        const auto restored=parse_scene_json(scene_json(fixed));
        check(restored==fixed&&restored.frozen->provenance->initial.index()==fixed.frozen->provenance->initial.index(),"Typed provenance variant was not preserved");
        same_field(fixed,restored);check(generation_job_count()==count,"Static provenance restoration reran generation");
    }
    auto empty=developed;empty.developed->cells.clear();refresh_developed_scene(empty);
    const auto frozen_empty=freeze_static(empty);check(frozen_empty.frozen->fields.empty()&&frozen_empty.frozen->rho_max==0,"Empty frozen field lost transparent state");
    check(parse_scene_json(scene_json(frozen_empty))==frozen_empty,"Empty frozen field did not roundtrip");
}
void tampering_and_bounds() {
    const auto scene=detailed_frozen();const auto encoded=Json::parse(scene_json(scene));
    auto bad=encoded;bad["cloud"]["source"]["contract_version"]=frozen_cloud_contract_version+1;
    reject([&]{parse_scene_json(bad.dump());},"Unknown frozen contract accepted");
    bad=encoded;bad["schema_version"]=9;reject([&]{parse_scene_json(bad.dump());},"Schema9 accepted a frozen source");
    bad=encoded;bad["cloud"]["source"]["fields"][0]["recipe"]["density"]=.125;
    reject([&]{parse_scene_json(bad.dump());},"Tampered evaluated field passed payload checksum");
    bad=encoded;bad["cloud"]["source"]["content_hash"]="1";
    reject([&]{parse_scene_json(bad.dump());},"Tampered immutable content hash accepted");
    bad=encoded;bad["cloud"]["source"]["fields"][0]["recipe"]["cells"][0]["center"][0]=987.;reseal_payload(bad);
    reject([&]{parse_scene_json(bad.dump());},"Resealed payload bypassed immutable geometry identity");
    bad=encoded;bad["cloud"]["source"].erase("payload_hash");
    reject([&]{parse_scene_json(bad.dump());},"Missing payload checksum accepted");
    for(const auto* key:{"assets","path","binary_file","cached_source"}) {
        bad=encoded;bad["cloud"]["source"][key]="../outside.bin";
        reject([&]{parse_scene_json(bad.dump());},"Unknown frozen storage member accepted");
    }
    bad=encoded;bad["cloud"]["source"]["fields"].push_back(bad["cloud"]["source"]["fields"][0]);
    reject([&]{parse_scene_json(bad.dump());},"More than two frozen fields accepted");
    bad=encoded;auto& cells=bad["cloud"]["source"]["fields"][0]["recipe"]["cells"];while(cells.size()<9)cells.push_back(cells[0]);
    reject([&]{parse_scene_json(bad.dump());},"More than eight primitives accepted");
    bad=encoded;bad["cloud"]["source"]["hierarchy"].push_back(bad["cloud"]["source"]["hierarchy"][0]);
    reject([&]{parse_scene_json(bad.dump());},"More than three hierarchy nodes accepted");
    bad=encoded;auto& points=bad["cloud"]["source"]["curves"][0]["points"];while(points.size()<7)points.push_back(points[0]);
    reject([&]{parse_scene_json(bad.dump());},"More than six curve points accepted");
    bad=encoded;auto& knots=bad["cloud"]["source"]["provenance"]["settings"]["wind"];while(knots.size()<7)knots.push_back(knots[0]);
    reject([&]{parse_scene_json(bad.dump());},"Unbounded provenance wind accepted");
    bad=encoded;bad["cloud"]["source"]["provenance"]["settings"]["algorithm_version"]=UINT64_C(4294967296);
    reject([&]{parse_scene_json(bad.dump());},"Generation version overflow accepted");
    bad=encoded;bad["cloud"]["source"]["provenance"]["settings"]["wind_height"]=0.;reseal_payload(bad);
    reject([&]{parse_scene_json(bad.dump());},"Resealed invalid generation settings accepted");
    bad=encoded;bad["cloud"]["source"]["generation_version"]=999;bad["cloud"]["source"]["provenance"]["settings"]["algorithm_version"]=999;
    bad["cloud"]["source"]["provenance"]["settings"]["wind"][0]["displacement"][1]=10.;reseal_payload(bad);
    reject([&]{parse_scene_json(bad.dump());},"Unavailable generation version bypassed known-format settings bounds");
    bad=encoded;bad["cloud"]["source"]["fields"][0]["layers"]["medium"]=1;
    reject([&]{parse_scene_json(bad.dump());},"Nonboolean frozen detail layer accepted");
    auto invalid=*scene.frozen;invalid.hierarchy[1].parent_id=invalid.hierarchy[1].id;
    bad=encoded;bad["cloud"]["source"]["hierarchy"][1]["parent_id"]=std::to_string(invalid.hierarchy[1].parent_id);copy_recomputed_hashes(bad,invalid);
    reject([&]{parse_scene_json(bad.dump());},"Rehashed frozen hierarchy self-parent accepted");
    invalid=*scene.frozen;invalid.hierarchy[2].parent_id=invalid.hierarchy[1].id;
    bad=encoded;bad["cloud"]["source"]["hierarchy"][2]["parent_id"]=std::to_string(invalid.hierarchy[2].parent_id);copy_recomputed_hashes(bad,invalid);
    reject([&]{parse_scene_json(bad.dump());},"Rehashed frozen hierarchy child-parent depth accepted");
    invalid=*scene.frozen;invalid.curves[0].points.front().t=.01;
    bad=encoded;bad["cloud"]["source"]["curves"][0]["points"][0]["t"]=.01;copy_recomputed_hashes(bad,invalid);
    reject([&]{parse_scene_json(bad.dump());},"Rehashed curve missing its base endpoint accepted");
    invalid=*scene.frozen;invalid.fields[1].development_id=invalid.fields[0].development_id;
    bad=encoded;bad["cloud"]["source"]["fields"][1]["development_id"]=std::to_string(invalid.fields[1].development_id);copy_recomputed_hashes(bad,invalid);
    reject([&]{parse_scene_json(bad.dump());},"Rehashed duplicate/mismatched frozen field identity accepted");
}
void envelope_authority_and_migration() {
    Scene source;source.cloud.cells={{2,{0,0,0},{20,20,20},0}};
    source.cloud.base.enabled=false;source.cloud.envelope={{-5,-5,-5},{5,5,5}};
    const auto scene=freeze_static(source);const auto field=scene.frozen->fields.front();
    check(field.clipping_envelope==source.cloud.envelope,"Custom envelope lost its immutable clipping authority");
    auto roomy_source=source;roomy_source.cloud.envelope={{-25,-25,-25},{25,25,25}};
    const auto roomy=freeze_static(roomy_source);
    check(roomy.frozen->content_hash!=scene.frozen->content_hash&&roomy.frozen->fields.front().clipping_envelope==roomy_source.cloud.envelope,"Oversized authored envelope lost its distinct immutable geometry identity");
    const auto jobs=generation_job_count();auto noise=field.recipe.noise;noise.warp_amplitude=.001;
    const auto edited=scene_with_frozen_detail(scene,field.development_id,noise,field.recipe.detail_seed,field.layers);
    check(edited.frozen->content_hash==scene.frozen->content_hash&&edited.frozen->support==scene.frozen->support&&edited.frozen->fields.front().recipe.envelope==source.cloud.envelope,"Detail edit moved a frozen hard envelope or changed geometry identity");
    const SceneDensityEvaluator evaluator(edited);const auto packet=evaluator.gpu_params();
    check(evaluator.at({4,0,0})>0&&evaluator.at({5,0,0})==0&&evaluator.at({10,0,0})==0,"Tiny macro edit revealed density outside the saved custom clipping mask");
    check(packet.field.cloud.fields.groups[0].envelope_min.x==-5&&packet.field.cloud.fields.groups[0].envelope_max.x==5&&packet.field.envelope_max.x==5,"Frozen clipping mask differs from the existing GPU final envelope");
    check(parse_scene_json(scene_json(edited))==edited,"Frozen clipping authority did not roundtrip");
    const auto restored=scene_with_frozen_detail(edited,field.development_id,field.recipe.noise,field.recipe.detail_seed,field.layers);
    check(restored==scene,"Restoring detail did not preserve the fixed crop exactly");
    noise.warp_amplitude=20;
    const auto roomy_edit=scene_with_frozen_detail(roomy,field.development_id,noise,field.recipe.detail_seed,field.layers);
    check(roomy_edit.frozen->support==roomy.frozen->support&&roomy_edit.frozen->content_hash==roomy.frozen->content_hash,"An initially oversized custom mask became expandable after a stronger warp");
    noise.warp_amplitude=.001;

    const auto generated=freeze_static(new_cumulonimbus_scene());const auto generated_field=generated.frozen->fields.front();
    check(!generated_field.clipping_envelope,"Generated sampling envelope became an authored crop");
    auto stronger=generated_field.recipe.noise;stronger.warp_amplitude+=5;
    const auto expanded=scene_with_frozen_detail(generated,generated_field.development_id,stronger,generated_field.recipe.detail_seed,generated_field.layers);
    check(expanded.frozen->content_hash==generated.frozen->content_hash&&expanded.frozen->support.min.x<generated.frozen->support.min.x&&expanded.frozen->support.max.x>generated.frozen->support.max.x,"Generated sampling bounds did not expand for stronger macro warp");
    // Increasing by a non-exact decimal must use the new amplitude directly;
    // subtraction/addition of the previous amplitude can undershoot by one ULP.
    auto decimal=generated_field.recipe.noise;decimal.warp_amplitude=1.1;
    const auto fractional=scene_with_frozen_detail(generated,generated_field.development_id,decimal,generated_field.recipe.detail_seed,generated_field.layers);
    decimal.warp_amplitude=1.2;
    check(validate_frozen_cloud(*scene_with_frozen_detail(fractional,generated_field.development_id,decimal,generated_field.recipe.detail_seed,generated_field.layers).frozen).empty(),"Decimal warp increase produced nonconservative rounded bounds");

    auto invalid=*scene.frozen;invalid.fields.front().recipe.envelope.max.x=6;
    reject([&]{refresh_frozen_cloud(invalid);},"Mismatched immutable clip and evaluated envelope accepted");
    invalid=*scene.frozen;invalid.fields.front().clipping_envelope.reset();
    reject([&]{refresh_frozen_cloud(invalid);},"Tight crop was accepted as unbounded sampling metadata");
    const auto encoded=Json::parse(scene_json(scene));auto bad=encoded;
    bad["cloud"]["source"]["fields"][0].erase("clipping_envelope");
    reject([&]{parse_scene_json(bad.dump());},"Frozen v2 omitted clipping authority metadata");
    bad=encoded;bad["cloud"]["source"]["fields"][0]["clipping_envelope"]["max"][0]=6.;reseal_payload(bad);
    reject([&]{parse_scene_json(bad.dump());},"Resealed explicit crop bypassed immutable content identity");

    const auto legacy=frozen_v1_json(scene);const auto migrated=parse_scene_json(legacy.dump());
    check(migrated==scene&&migrated.frozen->contract_version==2,"Frozen v1 tight crop did not migrate to immutable clipping data");
    same_field(scene,migrated);
    const auto migrated_edit=scene_with_frozen_detail(migrated,field.development_id,noise,field.recipe.detail_seed,field.layers);
    check(migrated_edit==edited,"Migrated crop changed finishing behavior");
    const auto migrated_generated=parse_scene_json(frozen_v1_json(generated).dump());
    check(migrated_generated==generated&&!migrated_generated.frozen->fields.front().clipping_envelope,"Frozen v1 conservative sampling bounds lost expandable authority");
    same_field(generated,migrated_generated);
    // The v1 delta-based warp update could save a bound one double ULP
    // inside the directly recomputed bound. Preserve those exact saved bytes
    // and keep this generated field expandable rather than inventing a crop.
    auto rounded=frozen_v1_json(generated);auto rounded_state=*generated.frozen;
    rounded_state.contract_version=1;
    const double upper=std::nextafter(rounded_state.fields[0].recipe.envelope.max.x,-INFINITY);
    rounded_state.fields[0].recipe.envelope.max.x=upper;rounded_state.support.max.x=upper;
    rounded["cloud"]["source"]["fields"][0]["recipe"]["envelope"]["max"][0]=upper;
    rounded["cloud"]["source"]["support"]["max"][0]=upper;copy_recomputed_hashes(rounded,rounded_state);
    const auto rounded_migration=parse_scene_json(rounded.dump());
    check(!rounded_migration.frozen->fields[0].clipping_envelope&&rounded_migration.frozen->fields[0].recipe.envelope.max.x==upper&&rounded_migration.frozen->support.max.x==upper,"Legacy double rounding was rejected, moved, or mistaken for an explicit clip");
    const auto rounded_edit=scene_with_frozen_detail(rounded_migration,generated_field.development_id,stronger,generated_field.recipe.detail_seed,generated_field.layers);
    check(rounded_edit.frozen->support==expanded.frozen->support,"Legacy rounded sampling bounds stopped expanding after migration");
    bad=legacy;bad["cloud"]["source"]["payload_hash"]="1";
    reject([&]{parse_scene_json(bad.dump());},"Migration accepted a corrupt v1 payload checksum");
    bad=legacy;bad["cloud"]["source"]["content_hash"]="1";reseal_payload(bad);
    reject([&]{parse_scene_json(bad.dump());},"Migration replaced an invalid v1 content hash before checking it");
    bad=legacy;bad["cloud"]["source"]["fields"][0]["recipe"]["optics"]["g"]=.5;reseal_payload(bad);
    reject([&]{parse_scene_json(bad.dump());},"Migration silently repaired inconsistent legacy field authority");
    for(bool keep_provenance:{false,true}) {
        auto unknown=scene;unknown.frozen->generation_version=999;
        if(keep_provenance)unknown.frozen->provenance->settings.algorithm_version=999;
        else unknown.frozen->provenance.reset();
        refresh_frozen_scene(unknown);
        const auto loaded=parse_scene_json(frozen_v1_json(unknown).dump());
        check(loaded==unknown&&!frozen_can_regenerate(*loaded.frozen),"Frozen v1 migration depends on available history or generation implementation");
        same_field(scene,loaded);
    }
    check(generation_job_count()==jobs+1,"Envelope migration, detail editing or evaluation invoked generation");
}
void saved_v1_fixture() {
    const auto path=std::filesystem::path(__FILE__).parent_path()/"fixtures"/"frozen-v1"/"calm-front.white.json";
    const auto saved=Json::parse(bytes(path));const auto jobs=generation_job_count();
    check(saved.at("schema_version")==10&&saved.at("cloud").at("source").at("contract_version")==1,"Historical fixture no longer contains original schema10/frozen-v1 bytes");
    const auto migrated=read_scene(path);auto old=saved.at("cloud").at("source");
    auto current=Json::parse(scene_json(migrated)).at("cloud").at("source");
    for(const auto* key:{"contract_version","content_hash","payload_hash"}){old.erase(key);current.erase(key);}
    for(auto& field:current["fields"]){check(field.at("clipping_envelope").is_null(),"Historical generated field became a clipping mask");field.erase("clipping_envelope");}
    check(current==old,"Migration changed evaluated structure, support, detail or provenance in the historical frozen payload");
    check(parse_scene_json(scene_json(migrated))==migrated,"Migrated historical fixture did not roundtrip as frozen v2");
    for(bool keep_provenance:{false,true}) {
        auto legacy=saved;auto& source=legacy["cloud"]["source"];source["generation_version"]=999;
        if(keep_provenance)source["provenance"]["settings"]["algorithm_version"]=999;
        else source["provenance"]=nullptr;
        reseal_payload(legacy);const auto loaded=parse_scene_json(legacy.dump());
        check(!frozen_can_regenerate(*loaded.frozen)&&loaded.frozen->contract_version==2,"Historical fixed data required an available generator during migration");
        same_field(migrated,loaded);
    }
    check(generation_job_count()==jobs,"Historical frozen fixture loading/evaluation invoked generation");
}
void atomic_save_and_legacy() {
    Temp temp;const auto scene=detailed_frozen();EditorSession editor(scene);const auto path=temp.path/"fixed.white.json";
    editor.save(path);const auto stable_bytes=bytes(path);const auto jobs=generation_job_count();
    auto next=scene;next.exposure_ev+=1;editor.apply(next);
    for(auto fault:{SaveFault::before_write,SaveFault::before_publish}) {
        reject([&]{editor.save(path,fault);},"Atomic frozen save failure did not fire");
        check(bytes(path)==stable_bytes&&read_scene(path)==scene&&editor.modified(),"Failed frozen save changed previous file or clean state");
    }
    const auto bad_path=temp.path/"truncated.white.json";{std::ofstream out(bad_path);out<<stable_bytes.substr(0,stable_bytes.size()/2);}
    const auto revision=editor.document().revision();
    reject([&]{editor.load(bad_path,true);},"Truncated frozen artifact loaded");
    check(editor.document().scene()==next&&editor.document().revision()==revision,"Failed frozen load changed active scene");
    reject([&]{editor.add_cell();},"Generic primitive command edited a frozen proxy");
    check(editor.document().scene()==next,"Rejected primitive edit changed frozen state");
    editor.save(path);check(read_scene(path)==next&&!editor.modified(),"Atomic frozen overwrite did not publish complete artifact");
    for(const auto& entry:std::filesystem::directory_iterator(temp.path))check(entry.path().string().find(".tmp-")==std::string::npos,"Atomic frozen save leaked a staging file");
    check(generation_job_count()==jobs,"Saving/loading frozen artifacts reran generation");

    for(const auto& original:std::array{Scene{},new_cumulonimbus_scene(),new_centerline_scene(),new_developed_scene(new_cumulonimbus_scene()),new_top_lobe_scene(new_cumulonimbus_scene()),new_anvil_scene(new_cumulonimbus_scene())}) {
        auto old=Json::parse(scene_json(original));old["schema_version"]=9;
        auto migrated=parse_scene_json(old.dump());
        check(migrated==original&&!migrated.frozen&&scene_json(migrated).find("provenance")==std::string::npos,"Schema9 migration inferred generation or changed a static source");
    }
    const auto v1=std::filesystem::path(__FILE__).parent_path()/"fixtures"/"scene-v1.white.json";
    const auto legacy=read_scene(v1);
    check(legacy.schema_version==11&&!legacy.frozen&&!legacy.cumulonimbus&&!legacy.centerline&&!legacy.developed&&!legacy.top_lobes&&!legacy.anvil,"Legacy fixture inferred a source or frozen state");
    check(parse_scene_json(scene_json(legacy))==legacy&&generation_job_count()==jobs,"Legacy fixture migration changed state or started generation");
}
}
int main(){try {
    roundtrip_and_compatibility();tampering_and_bounds();envelope_authority_and_migration();saved_v1_fixture();atomic_save_and_legacy();
    std::cout<<"Frozen schema10: exact evaluated fields/provenance; unknown generation compatibility; payload and structural hashes; bounds; atomic rollback; legacy migration; zero regeneration on load PASS\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
