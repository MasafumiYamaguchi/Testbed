#include "white/persistence.hpp"
#include "white/cumulonimbus.hpp"
#include "white/centerline.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace white {
namespace {
using Json=nlohmann::json;
Json vec(Vec3 p) {return Json::array({p.x,p.y,p.z});}
Vec3 vec(const Json& j) {
    if(!j.is_array()||j.size()!=3)throw std::invalid_argument("Expected a 3-element numeric vector");
    for(const auto& x:j)if(!x.is_number())throw std::invalid_argument("Vector components must be numbers");
    return {j[0].get<double>(),j[1].get<double>(),j[2].get<double>()};
}
std::uint64_t id(const Json& j) {
    if(!j.is_string())throw std::invalid_argument("IDs and seeds must be decimal strings");
    const auto& str=j.get_ref<const std::string&>();std::uint64_t result=0;
    auto parsed=std::from_chars(str.data(),str.data()+str.size(),result);
    if(str.empty()||parsed.ec!=std::errc{}||parsed.ptr!=str.data()+str.size())throw std::invalid_argument("Invalid uint64 ID/seed");
    return result;
}
Json noise_json(const NoiseSettings& n) {return {{"origin",vec(n.origin)},{"medium_frequency",n.medium_frequency},{"medium_strength",n.medium_strength},{"micro_frequency",n.micro_frequency},{"micro_erosion",n.micro_erosion},{"warp_frequency",n.warp_frequency},{"warp_amplitude",n.warp_amplitude}};}
double number(const Json& j) {if(!j.is_number())throw std::invalid_argument("Expected numeric parameter");return j.get<double>();}
void shape(const Json& j,std::initializer_list<const char*> keys) {
    if(!j.is_object()||j.size()!=keys.size())throw std::invalid_argument("Unexpected object members");
    for(auto key:keys)if(!j.contains(key))throw std::invalid_argument(std::string("Missing required member: ")+key);
}
Json transform_json(const Transform& t) {
    const auto q=t.rotation;
    return {{"translation",vec(t.translation)},{"rotation",Json::array({q.x,q.y,q.z,q.w})},{"scale",vec(t.scale)}};
}
Transform transform_from_json(const Json& j) {
    shape(j,{"translation","rotation","scale"});const auto& q=j.at("rotation");
    if(!q.is_array()||q.size()!=4)throw std::invalid_argument("Rotation requires four components");
    return {vec(j.at("translation")),{number(q[0]),number(q[1]),number(q[2]),number(q[3])},vec(j.at("scale"))};
}
Json optics_json(const Optics& o){return {{"extinction_scale",o.extinction_scale},{"albedo",o.albedo},{"g",o.g}};}
Optics optics_from_json(const Json& j) {
    shape(j,{"extinction_scale","albedo","g"});
    return {number(j.at("extinction_scale")),number(j.at("albedo")),number(j.at("g"))};
}
NoiseSettings noise_from_json(const Json& j) {
    shape(j,{"origin","medium_frequency","medium_strength","micro_frequency","micro_erosion","warp_frequency","warp_amplitude"});
    return {vec(j.at("origin")),number(j.at("medium_frequency")),number(j.at("medium_strength")),number(j.at("micro_frequency")),number(j.at("micro_erosion")),number(j.at("warp_frequency")),number(j.at("warp_amplitude"))};
}
Json cell_json(const Cell& c){return {{"id",std::to_string(c.id)},{"center",vec(c.center)},{"radii",vec(c.radii)},{"structure_seed",std::to_string(c.structure_seed)}};}
Cell cell_from_json(const Json& j) {
    shape(j,{"id","center","radii","structure_seed"});
    return {id(j.at("id")),vec(j.at("center")),vec(j.at("radii")),id(j.at("structure_seed"))};
}
Json cut_json(const Cut& c){return {{"id",std::to_string(c.id)},{"center",vec(c.center)},{"radii",vec(c.radii)},{"transition",c.transition}};}
Cut cut_from_json(const Json& j) {
    shape(j,{"id","center","radii","transition"});
    return {id(j.at("id")),vec(j.at("center")),vec(j.at("radii")),number(j.at("transition"))};
}
Json cumulonimbus_json(const CumulonimbusGroup& group) {
    const auto& p=group.parameters;const auto& m=group.modifiers;
    Json ids=Json::array(),adjustments=Json::array(),cells=Json::array(),cuts=Json::array();
    for(auto value:group.cell_ids)ids.push_back(std::to_string(value));
    for(const auto& a:group.cell_adjustments)adjustments.push_back({{"cell_id",std::to_string(a.cell_id)},
        {"center_offset",vec(a.center_offset)},{"radius_scale",vec(a.radius_scale)},
        {"structure_seed",a.structure_seed?Json(std::to_string(*a.structure_seed)):Json(nullptr)}});
    for(const auto& c:m.manual_cells)cells.push_back(cell_json(c));
    for(const auto& c:m.cuts)cuts.push_back(cut_json(c));
    return {{"contract_version",group.contract_version},{"cloud_id",std::to_string(group.cloud_id)},{"cell_ids",ids},
        {"parameters",{{"width",p.width},{"height",p.height},{"cloud_base",p.cloud_base},{"growth_direction",vec(p.growth_direction)},
            {"density",p.density},{"structure_seed",std::to_string(p.structure_seed)},{"detail_seed",std::to_string(p.detail_seed)}}},
        {"cell_adjustments",adjustments},
        {"modifiers",{{"transform",transform_json(m.transform)},{"optics",optics_json(m.optics)},{"noise",noise_json(m.noise)},
            {"blend_width",m.blend_width},{"overlap",m.overlap},{"base_transition",m.base_transition},{"base_enabled",m.base_enabled},
            {"manual_cells",cells},{"cuts",cuts}}}};
}
CumulonimbusGroup cumulonimbus_from_json(const Json& j) {
    shape(j,{"contract_version","cloud_id","cell_ids","parameters","cell_adjustments","modifiers"});
    if(!j.at("contract_version").is_number_unsigned()||j.at("contract_version")!=cumulonimbus_contract_version)
        throw std::invalid_argument("Unsupported Cumulonimbus contract version");
    CumulonimbusGroup group;group.cloud_id=id(j.at("cloud_id"));
    const auto& ids=j.at("cell_ids");
    if(!ids.is_array()||ids.size()!=cumulonimbus_generated_cells)throw std::invalid_argument("Cumulonimbus requires exactly five stable Cell IDs");
    for(std::size_t i=0;i<group.cell_ids.size();++i)group.cell_ids[i]=id(ids[i]);
    const auto& p=j.at("parameters");shape(p,{"width","height","cloud_base","growth_direction","density","structure_seed","detail_seed"});
    group.parameters={number(p.at("width")),number(p.at("height")),number(p.at("cloud_base")),vec(p.at("growth_direction")),
        number(p.at("density")),id(p.at("structure_seed")),id(p.at("detail_seed"))};
    const auto& adjustments=j.at("cell_adjustments");
    if(!adjustments.is_array()||adjustments.size()>cumulonimbus_generated_cells)throw std::invalid_argument("Too many Cumulonimbus cell adjustments");
    for(const auto& a:adjustments) {
        shape(a,{"cell_id","center_offset","radius_scale","structure_seed"});
        CumulonimbusCellAdjustment adjustment{id(a.at("cell_id")),vec(a.at("center_offset")),vec(a.at("radius_scale")),{}};
        if(!a.at("structure_seed").is_null())adjustment.structure_seed=id(a.at("structure_seed"));
        group.cell_adjustments.push_back(adjustment);
    }
    const auto& m=j.at("modifiers");shape(m,{"transform","optics","noise","blend_width","overlap","base_transition","base_enabled","manual_cells","cuts"});
    auto& modifiers=group.modifiers;modifiers.transform=transform_from_json(m.at("transform"));modifiers.optics=optics_from_json(m.at("optics"));modifiers.noise=noise_from_json(m.at("noise"));
    modifiers.blend_width=number(m.at("blend_width"));modifiers.overlap=number(m.at("overlap"));modifiers.base_transition=number(m.at("base_transition"));
    if(!m.at("base_enabled").is_boolean())throw std::invalid_argument("Prefab base_enabled must be boolean");
    modifiers.base_enabled=m.at("base_enabled").get<bool>();
    const auto& cells=m.at("manual_cells");const auto& cuts=m.at("cuts");
    if(!cells.is_array()||cells.size()>8-cumulonimbus_generated_cells||!cuts.is_array()||cuts.size()>8)
        throw std::invalid_argument("Cumulonimbus exceeds manual cell/cut limit");
    for(const auto& c:cells)modifiers.manual_cells.push_back(cell_from_json(c));
    for(const auto& c:cuts)modifiers.cuts.push_back(cut_from_json(c));
    return group;
}
Json altitude_density_json(const AltitudeDensityProfile& profile) {
    Json knots=Json::array();for(const auto& knot:profile.knots)knots.push_back({{"t",knot.t},{"scale",knot.scale}});
    return {{"enabled",profile.enabled},{"base",profile.base},{"height",profile.height},{"knots",knots}};
}
AltitudeDensityProfile altitude_density_from_json(const Json& j) {
    shape(j,{"enabled","base","height","knots"});
    if(!j.at("enabled").is_boolean())throw std::invalid_argument("Altitude density enabled must be boolean");
    const auto& knots=j.at("knots");
    if(!knots.is_array()||knots.size()<2||knots.size()>max_altitude_density_knots)throw std::invalid_argument("Altitude density requires 2..8 knots");
    AltitudeDensityProfile profile{j.at("enabled").get<bool>(),number(j.at("base")),number(j.at("height")),{}};
    for(const auto& knot:knots){shape(knot,{"t","scale"});profile.knots.push_back({number(knot.at("t")),number(knot.at("scale"))});}
    return profile;
}
Json centerline_json(const CenterlineShape& source) {
    Json points=Json::array(),profile=Json::array();
    for(const auto& p:source.points)points.push_back({{"id",std::to_string(p.id)},{"t",p.t},{"offset",vec(p.offset)}});
    for(const auto& p:source.profile)profile.push_back({{"id",std::to_string(p.id)},{"t",p.t},{"radius_scale",p.radius_scale},{"density_scale",p.density_scale}});
    return {{"contract_version",source.contract_version},{"source",cumulonimbus_json(source.source)},{"points",points},{"profile",profile}};
}
CenterlineShape centerline_from_json(const Json& j) {
    shape(j,{"contract_version","source","points","profile"});
    if(!j.at("contract_version").is_number_unsigned()||j.at("contract_version")!=centerline_contract_version)
        throw std::invalid_argument("Unsupported centerline contract version");
    CenterlineShape source;source.source=cumulonimbus_from_json(j.at("source"));
    const auto& points=j.at("points");const auto& profile=j.at("profile");
    if(!points.is_array()||points.size()<2||points.size()>max_centerline_points||!profile.is_array()||profile.size()<2||profile.size()>max_centerline_profile_points)
        throw std::invalid_argument("Centerline requires 2..6 points and 2..8 profile knots");
    source.points.clear();source.profile.clear();
    for(const auto& p:points){shape(p,{"id","t","offset"});source.points.push_back({id(p.at("id")),number(p.at("t")),vec(p.at("offset"))});}
    for(const auto& p:profile){shape(p,{"id","t","radius_scale","density_scale"});source.profile.push_back({id(p.at("id")),number(p.at("t")),number(p.at("radius_scale")),number(p.at("density_scale"))});}
    return source;
}
void push_bounded(std::vector<Scene>& history,Scene scene) {
    if(history.size()==128)history.erase(history.begin());
    history.push_back(std::move(scene));
}
std::runtime_error file_error(const char* operation) {return std::runtime_error(std::string(operation)+": "+std::generic_category().message(errno));}
}
std::string scene_json(const Scene& s) {
    require_valid(s);const auto& c=s.cloud;
    Json cells=Json::array(),cuts=Json::array();
    for(const auto& cell:c.cells)cells.push_back({{"id",std::to_string(cell.id)},{"center",vec(cell.center)},{"radii",vec(cell.radii)},{"structure_seed",std::to_string(cell.structure_seed)}});
    for(const auto& cut:c.cuts)cuts.push_back({{"id",std::to_string(cut.id)},{"center",vec(cut.center)},{"radii",vec(cut.radii)},{"transition",cut.transition}});
    const auto q=c.transform.rotation;
    Json j={
        {"schema_version",s.schema_version},{"algorithm_version",s.algorithm_version},
        {"cloud",{{"id",std::to_string(c.id)},{"cells",cells},{"cuts",cuts},
            {"transform",{{"translation",vec(c.transform.translation)},{"rotation",Json::array({q.x,q.y,q.z,q.w})},{"scale",vec(c.transform.scale)}}},
            {"envelope",{{"min",vec(c.envelope.min)},{"max",vec(c.envelope.max)}}},
            {"base",{{"enabled",c.base.enabled},{"height",c.base.height},{"transition",c.base.transition}}},
            {"density",c.density},{"blend_width",c.blend_width},{"overlap",c.overlap},
            {"structure_seed",std::to_string(c.structure_seed)},{"detail_seed",std::to_string(c.detail_seed)},
            {"noise",noise_json(c.noise)},{"altitude_density",altitude_density_json(c.altitude_density)},
            {"optics",{{"extinction_scale",c.optics.extinction_scale},{"albedo",c.optics.albedo},{"g",c.optics.g}}}}},
        {"camera",{{"position",vec(s.camera.position)},{"target",vec(s.camera.target)},{"up",vec(s.camera.up)},
            {"vertical_fov_degrees",s.camera.vertical_fov_degrees},{"near_plane",s.camera.near_plane},{"far_plane",s.camera.far_plane}}},
        {"sun",{{"direction_to_light",vec(s.sun.direction_to_light)},{"irradiance",vec(s.sun.irradiance)}}},
        {"exposure_ev",s.exposure_ev},{"preview_approx",{{"enabled",s.preview_approx.enabled},{"strength",s.preview_approx.strength}}}};
    if(s.centerline)j["cloud"]={{"kind","centerline"},{"source",centerline_json(*s.centerline)}};
    else if(s.cumulonimbus)j["cloud"]={{"kind","cumulonimbus"},{"source",cumulonimbus_json(*s.cumulonimbus)}};
    return j.dump(2)+"\n";
}
Scene parse_scene_json(std::string_view text) {
    if(text.size()>max_scene_bytes)throw std::invalid_argument("Scene exceeds 4 MiB limit");
    auto j=Json::parse(text,[](int depth,Json::parse_event_t,Json&){
        if(depth>32)throw std::invalid_argument("Scene nesting exceeds 32 levels");
        return true;
    });
    if((j.value("schema_version",0u)==4||j.value("schema_version",0u)==5||j.value("schema_version",0u)==6))shape(j,{"schema_version","algorithm_version","cloud","camera","sun","exposure_ev","preview_approx"});
    else shape(j,{"schema_version","algorithm_version","cloud","camera","sun","exposure_ev"});
    if(!j.at("schema_version").is_number_unsigned()||!j.at("algorithm_version").is_number_unsigned())throw std::invalid_argument("Version must be an unsigned integer");
    const bool legacy=j.at("schema_version")==1&&j.at("algorithm_version")==1;
    const bool version2=j.at("schema_version")==2&&j.at("algorithm_version")==2;
    const bool old_schema=(j.at("schema_version")==3||j.at("schema_version")==4||j.at("schema_version")==5)&&j.at("algorithm_version")==2;
    const bool current_schema=j.at("schema_version")==6&&j.at("algorithm_version")==3;
    if(!legacy&&!version2&&!old_schema&&!current_schema)throw std::invalid_argument("Unsupported schema/algorithm version");
    if(legacy) {
        shape(j.at("cloud"),{"id","cells","cuts","transform","envelope","base","density","blend_width","overlap","structure_seed","detail_seed","optics"});
        j["cloud"]["noise"]=noise_json(NoiseSettings{}); // exact old shape: all noise amplitudes zero
    }
    if(legacy||version2){shape(j["cloud"]["optics"],{"extinction_scale","albedo"});j["cloud"]["optics"]["g"]=0;}
    Scene s;
    if(j.at("schema_version")==4||j.at("schema_version")==5||j.at("schema_version")==6){const auto& a=j.at("preview_approx");shape(a,{"enabled","strength"});if(!a.at("enabled").is_boolean())throw std::invalid_argument("Approximation enabled must be boolean");s.preview_approx={a.at("enabled").get<bool>(),number(a.at("strength"))};}
    if(!current_schema&&!j.at("cloud").contains("kind")) {
        if(j.at("cloud").contains("altitude_density"))throw std::invalid_argument("Legacy schema cannot contain altitude density fields");
        j["cloud"]["altitude_density"]=altitude_density_json(AltitudeDensityProfile{});
    }
    auto& c=s.cloud;const auto& cj=j.at("cloud");
    if((j.at("schema_version")==5||current_schema)&&cj.is_object()&&cj.contains("kind")) {
        shape(cj,{"kind","source"});
        if(cj.at("kind")=="cumulonimbus") {
            s.cumulonimbus=cumulonimbus_from_json(cj.at("source"));
            c=derive_cumulonimbus_recipe(*s.cumulonimbus);
        } else if(current_schema&&cj.at("kind")=="centerline") {
            s.centerline=centerline_from_json(cj.at("source"));
            c=lower_centerline_to_recipe(*s.centerline);
        } else throw std::invalid_argument("Unsupported cloud object kind");
    } else {
    shape(cj,{"id","cells","cuts","transform","envelope","base","density","blend_width","overlap","structure_seed","detail_seed","optics","noise","altitude_density"});
    c.altitude_density=altitude_density_from_json(cj.at("altitude_density"));
    const auto& noise=cj.at("noise");shape(noise,{"origin","medium_frequency","medium_strength","micro_frequency","micro_erosion","warp_frequency","warp_amplitude"});
    c.noise={vec(noise.at("origin")),number(noise.at("medium_frequency")),number(noise.at("medium_strength")),number(noise.at("micro_frequency")),number(noise.at("micro_erosion")),number(noise.at("warp_frequency")),number(noise.at("warp_amplitude"))};
    c.id=id(cj.at("id"));c.structure_seed=id(cj.at("structure_seed"));c.detail_seed=id(cj.at("detail_seed"));
    const auto& cells=cj.at("cells");const auto& cuts=cj.at("cuts");
    if(!cells.is_array()||cells.size()>8||!cuts.is_array()||cuts.size()>8)throw std::invalid_argument("Scene exceeds 8 cells/cuts or has invalid collections");
    c.cells.clear();c.cuts.clear();
    for(const auto& cell:cells) {
        shape(cell,{"id","center","radii","structure_seed"});
        c.cells.push_back({id(cell.at("id")),vec(cell.at("center")),vec(cell.at("radii")),id(cell.at("structure_seed"))});
    }
    for(const auto& cut:cuts) {
        shape(cut,{"id","center","radii","transition"});
        c.cuts.push_back({id(cut.at("id")),vec(cut.at("center")),vec(cut.at("radii")),number(cut.at("transition"))});
    }
    const auto& t=cj.at("transform");shape(t,{"translation","rotation","scale"});
    c.transform.translation=vec(t.at("translation"));c.transform.scale=vec(t.at("scale"));
    const auto& q=t.at("rotation");if(!q.is_array()||q.size()!=4)throw std::invalid_argument("Rotation requires four components");
    c.transform.rotation={number(q[0]),number(q[1]),number(q[2]),number(q[3])};
    const auto& envelope=cj.at("envelope");shape(envelope,{"min","max"});c.envelope={vec(envelope.at("min")),vec(envelope.at("max"))};
    const auto& base=cj.at("base");shape(base,{"enabled","height","transition"});
    if(!base.at("enabled").is_boolean())throw std::invalid_argument("Base enabled must be boolean");
    c.base={base.at("enabled").get<bool>(),number(base.at("height")),number(base.at("transition"))};
    c.density=number(cj.at("density"));c.blend_width=number(cj.at("blend_width"));c.overlap=number(cj.at("overlap"));
    const auto& optics=cj.at("optics");shape(optics,{"extinction_scale","albedo","g"});
    c.optics={number(optics.at("extinction_scale")),number(optics.at("albedo")),number(optics.at("g"))};
    }
    const auto& camera=j.at("camera");shape(camera,{"position","target","up","vertical_fov_degrees","near_plane","far_plane"});
    s.camera={vec(camera.at("position")),vec(camera.at("target")),vec(camera.at("up")),number(camera.at("vertical_fov_degrees")),number(camera.at("near_plane")),number(camera.at("far_plane"))};
    const auto& sun=j.at("sun");shape(sun,{"direction_to_light","irradiance"});
    s.sun={vec(sun.at("direction_to_light")),vec(sun.at("irradiance"))};s.exposure_ev=number(j.at("exposure_ev"));
    require_valid(s);return s;
}
Scene read_scene(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("Cannot open scene for reading");
    const auto size=in.tellg();
    if(size<0||size>static_cast<std::streamoff>(max_scene_bytes))throw std::runtime_error("Scene exceeds 4 MiB limit or is not seekable");
    std::string bytes(static_cast<std::size_t>(size),'\0');in.seekg(0);
    in.read(bytes.data(),size);
    if(!in||in.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Scene changed or failed while reading");
    return parse_scene_json(bytes);
}
void save_scene_atomic(const Scene& scene,const std::filesystem::path& destination,SaveFault fault) {
    const auto bytes=scene_json(scene);
    if(destination.filename().empty())throw std::invalid_argument("Scene destination must name a file");
    static std::atomic<std::uint64_t> serial{0};
    auto temp=destination;
    temp+=std::string(".tmp-")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial.fetch_add(1));
    int fd=-1;
#ifdef _WIN32
    if(_wsopen_s(&fd,temp.c_str(),_O_CREAT|_O_EXCL|_O_BINARY|_O_WRONLY,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0)throw file_error("Create temporary scene");
#else
    fd=::open(temp.c_str(),O_CREAT|O_EXCL|O_WRONLY,0666);if(fd<0)throw file_error("Create temporary scene");
#endif
    try {
        if(fault==SaveFault::before_write)throw std::runtime_error("Injected write failure");
        size_t written=0;
        while(written<bytes.size()) {
#ifdef _WIN32
            const auto n=_write(fd,bytes.data()+written,static_cast<unsigned>(bytes.size()-written));
#else
            const auto n=::write(fd,bytes.data()+written,bytes.size()-written);
#endif
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw file_error("Write scene");
            written+=static_cast<size_t>(n);
        }
#ifdef _WIN32
        if(_commit(fd)!=0)throw file_error("Flush scene");
        const int close_result=_close(fd);
#else
        if(::fsync(fd)!=0)throw file_error("Flush scene");
        const int close_result=::close(fd);
#endif
        fd=-1;if(close_result!=0)throw file_error("Close scene");
        if(fault==SaveFault::before_publish)throw std::runtime_error("Injected publish failure");
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Publish scene");
#else
        std::filesystem::rename(temp,destination);
#endif
    } catch(...) {
        if(fd>=0) {
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
        }
        std::error_code ignored;std::filesystem::remove(temp,ignored);throw;
    }
}
bool EditorSession::apply(Scene scene) {
    require_valid(scene);if(scene==document_.scene())return false;
    if(drag_)return document_.replace(std::move(scene));
    auto history=undo_;push_bounded(history,document_.scene());
    document_.replace(std::move(scene));undo_.swap(history);redo_.clear();return true;
}
Id EditorSession::add_cell(Cell cell) {
    auto scene=document_.scene();cell.id=next_id(scene);
    if(scene.centerline) {
        scene.centerline->source.modifiers.manual_cells.push_back(cell);
        scene.cloud=lower_centerline_to_recipe(*scene.centerline);
    } else if(scene.cumulonimbus) {
        scene.cumulonimbus->modifiers.manual_cells.push_back(cell);
        scene.cloud=derive_cumulonimbus_recipe(*scene.cumulonimbus);
    } else scene.cloud.cells.push_back(cell);
    apply(std::move(scene));return cell.id;
}
bool EditorSession::remove_cell(Id id) {
    auto scene=document_.scene();
    if(scene.centerline) {
        const auto& ids=scene.centerline->source.cell_ids;
        if(std::find(ids.begin(),ids.end(),id)!=ids.end())
            throw std::invalid_argument("Generated centerline cells cannot be deleted; explicitly convert to Custom Cloud first");
        std::erase_if(scene.centerline->source.modifiers.manual_cells,[=](const auto& c){return c.id==id;});
        scene.cloud=lower_centerline_to_recipe(*scene.centerline);
    } else if(scene.cumulonimbus) {
        const auto& ids=scene.cumulonimbus->cell_ids;
        if(std::find(ids.begin(),ids.end(),id)!=ids.end())
            throw std::invalid_argument("Generated prefab cells cannot be deleted; explicitly convert to Custom Cloud first");
        std::erase_if(scene.cumulonimbus->modifiers.manual_cells,[=](const auto& c){return c.id==id;});
        scene.cloud=derive_cumulonimbus_recipe(*scene.cumulonimbus);
    } else std::erase_if(scene.cloud.cells,[=](const auto& c){return c.id==id;});
    return apply(std::move(scene));
}
void EditorSession::begin_drag() {if(drag_)throw std::logic_error("Drag transaction already active");drag_=document_.scene();}
void EditorSession::end_drag() {
    if(!drag_)throw std::logic_error("No drag transaction");
    if(*drag_!=document_.scene()) {auto history=undo_;push_bounded(history,*drag_);undo_.swap(history);redo_.clear();}
    drag_.reset();
}
void EditorSession::cancel_drag() {if(!drag_)return;document_.replace(*drag_);drag_.reset();}
bool EditorSession::undo() {
    if(!can_undo())return false;
    auto redo=redo_;push_bounded(redo,document_.scene());Scene previous=undo_.back();
    document_.replace(std::move(previous));undo_.pop_back();redo_.swap(redo);return true;
}
bool EditorSession::redo() {
    if(!can_redo())return false;
    auto undo=undo_;push_bounded(undo,document_.scene());Scene next=redo_.back();
    document_.replace(std::move(next));redo_.pop_back();undo_.swap(undo);return true;
}
void EditorSession::save(const std::filesystem::path& path,SaveFault fault) {
    if(drag_)throw std::logic_error("Finish or cancel drag before saving");
    auto snapshot=document_.scene();save_scene_atomic(snapshot,path,fault);saved_=std::move(snapshot);
}
void EditorSession::load(const std::filesystem::path& path,bool discard_unsaved) {
    if(drag_)throw std::logic_error("Finish or cancel drag before loading");
    if(modified()&&!discard_unsaved)throw std::logic_error("Unsaved changes: explicitly save or discard before loading");
    auto scene=read_scene(path);auto saved=scene;
    document_.replace(std::move(scene));saved_=std::move(saved);undo_.clear();redo_.clear();
}
}
