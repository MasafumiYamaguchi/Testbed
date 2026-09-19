#include "white/persistence.hpp"
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
double number(const Json& j) {if(!j.is_number())throw std::invalid_argument("Expected numeric parameter");return j.get<double>();}
void shape(const Json& j,std::initializer_list<const char*> keys) {
    if(!j.is_object()||j.size()!=keys.size())throw std::invalid_argument("Unexpected object members");
    for(auto key:keys)if(!j.contains(key))throw std::invalid_argument(std::string("Missing required member: ")+key);
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
            {"optics",{{"extinction_scale",c.optics.extinction_scale},{"albedo",c.optics.albedo}}}}},
        {"camera",{{"position",vec(s.camera.position)},{"target",vec(s.camera.target)},{"up",vec(s.camera.up)},
            {"vertical_fov_degrees",s.camera.vertical_fov_degrees},{"near_plane",s.camera.near_plane},{"far_plane",s.camera.far_plane}}},
        {"sun",{{"direction_to_light",vec(s.sun.direction_to_light)},{"irradiance",vec(s.sun.irradiance)}}},
        {"exposure_ev",s.exposure_ev}};
    return j.dump(2)+"\n";
}
Scene parse_scene_json(std::string_view text) {
    if(text.size()>max_scene_bytes)throw std::invalid_argument("Scene exceeds 4 MiB limit");
    auto j=Json::parse(text,[](int depth,Json::parse_event_t,Json&){
        if(depth>32)throw std::invalid_argument("Scene nesting exceeds 32 levels");
        return true;
    });
    shape(j,{"schema_version","algorithm_version","cloud","camera","sun","exposure_ev"});
    if(!j.at("schema_version").is_number_unsigned()||j.at("schema_version")!=1||
       !j.at("algorithm_version").is_number_unsigned()||j.at("algorithm_version")!=1)
        throw std::invalid_argument("Unsupported schema/algorithm version");
    Scene s;auto& c=s.cloud;const auto& cj=j.at("cloud");
    shape(cj,{"id","cells","cuts","transform","envelope","base","density","blend_width","overlap","structure_seed","detail_seed","optics"});
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
    const auto& optics=cj.at("optics");shape(optics,{"extinction_scale","albedo"});
    c.optics={number(optics.at("extinction_scale")),number(optics.at("albedo"))};
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
    auto scene=document_.scene();cell.id=next_id(scene);scene.cloud.cells.push_back(cell);apply(std::move(scene));return cell.id;
}
bool EditorSession::remove_cell(Id id) {
    auto scene=document_.scene();std::erase_if(scene.cloud.cells,[=](const auto& c){return c.id==id;});return apply(std::move(scene));
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
