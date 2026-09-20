#include "white/persistence.hpp"
#include "white/anvil_scene.hpp"
#include "white/cumulonimbus.hpp"
#include "white/centerline.hpp"
#include "white/developed_scene.hpp"
#include "white/top_lobe_scene.hpp"
#include "white/frozen_cloud.hpp"
#include "white/generation.hpp"
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
#include <type_traits>
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
Json developed_json(const DevelopedCloud& source) {
    Json cells=Json::array();for(const auto& cell:source.cells)cells.push_back({{"id",std::to_string(cell.id)},
        {"shape",centerline_json(cell.shape)},{"translation",vec(cell.translation)},{"roles",cell.roles}});
    return {{"contract_version",source.contract_version},{"id",std::to_string(source.id)},{"transform",transform_json(source.transform)},
        {"optics",optics_json(source.optics)},{"fusion_width",source.fusion_width},{"overlap",source.overlap},{"cells",cells}};
}
DevelopedCloud developed_from_json(const Json& j) {
    shape(j,{"contract_version","id","transform","optics","fusion_width","overlap","cells"});
    if(!j.at("contract_version").is_number_unsigned()||j.at("contract_version")!=developed_cloud_contract_version)
        throw std::invalid_argument("Unsupported developed source version");
    DevelopedCloud source;source.id=id(j.at("id"));source.transform=transform_from_json(j.at("transform"));source.optics=optics_from_json(j.at("optics"));
    source.fusion_width=number(j.at("fusion_width"));source.overlap=number(j.at("overlap"));const auto& cells=j.at("cells");
    if(!cells.is_array()||cells.size()>max_developed_cells)throw std::invalid_argument("Developed source exceeds two independent cells");
    for(const auto& c:cells) {
        shape(c,{"id","shape","translation","roles"});DevelopedCell cell;cell.id=id(c.at("id"));cell.shape=centerline_from_json(c.at("shape"));cell.translation=vec(c.at("translation"));
        const auto& roles=c.at("roles");if(!roles.is_array()||roles.size()<3||roles.size()>5)throw std::invalid_argument("Development requires 3..5 explicit roles");
        cell.roles.clear();for(const auto& role:roles){if(!role.is_number_unsigned()||role.get<std::uint64_t>()>4)throw std::invalid_argument("Development role must be an unsigned index in 0..4");cell.roles.push_back(role.get<unsigned>());}
        source.cells.push_back(std::move(cell));
    }
    return source;
}
Json top_lobe_json(const TopLobeSource& source) {
    const auto& s=source.settings;Json ids=Json::array();for(auto value:source.lobe_ids)ids.push_back(std::to_string(value));
    const char* mode=s.mode==TopLobeMode::off?"off":s.mode==TopLobeMode::parent?"parent":"children";
    return {{"contract_version",source.contract_version},{"trunk",developed_json(source.trunk)},
        {"target_cell",std::to_string(source.target_cell)},{"field_id",std::to_string(source.field_id)},{"lobe_ids",ids},
        {"settings",{{"mode",mode},{"depth_limit",s.depth_limit},{"child_limit",s.child_limit},{"top_start",s.top_start},
            {"parent_radius",s.parent_radius},{"child_radius_ratio",s.child_radius_ratio},{"hierarchy_density",s.hierarchy_density},
            {"mask_transition",s.mask_transition},{"fusion_width",s.fusion_width},{"density_scale",s.density_scale},{"growth_direction",vec(s.growth_direction)}}}};
}
TopLobeSource top_lobe_from_json(const Json& j) {
    shape(j,{"contract_version","trunk","target_cell","field_id","lobe_ids","settings"});
    if(!j.at("contract_version").is_number_unsigned()||j.at("contract_version")!=top_lobe_contract_version)throw std::invalid_argument("Unsupported top-lobe source version");
    TopLobeSource source;source.trunk=developed_from_json(j.at("trunk"));source.target_cell=id(j.at("target_cell"));source.field_id=id(j.at("field_id"));
    const auto& ids=j.at("lobe_ids");if(!ids.is_array()||ids.size()!=max_top_lobes)throw std::invalid_argument("Top lobes require exactly three reserved IDs");
    for(std::size_t i=0;i<source.lobe_ids.size();++i)source.lobe_ids[i]=id(ids[i]);
    const auto& s=j.at("settings");shape(s,{"mode","depth_limit","child_limit","top_start","parent_radius","child_radius_ratio","hierarchy_density","mask_transition","fusion_width","density_scale","growth_direction"});
    if(s.at("mode")=="off")source.settings.mode=TopLobeMode::off;
    else if(s.at("mode")=="parent")source.settings.mode=TopLobeMode::parent;
    else if(s.at("mode")=="children")source.settings.mode=TopLobeMode::children;
    else throw std::invalid_argument("Unknown top-lobe comparison mode");
    if(!s.at("depth_limit").is_number_unsigned()||s.at("depth_limit").get<std::uint64_t>()>max_top_lobe_depth||!s.at("child_limit").is_number_unsigned()||s.at("child_limit").get<std::uint64_t>()>2)
        throw std::invalid_argument("Invalid bounded top-lobe depth/child limit");
    auto& settings=source.settings;settings.depth_limit=s.at("depth_limit").get<unsigned>();settings.child_limit=s.at("child_limit").get<unsigned>();
    settings.top_start=number(s.at("top_start"));settings.parent_radius=number(s.at("parent_radius"));settings.child_radius_ratio=number(s.at("child_radius_ratio"));
    settings.hierarchy_density=number(s.at("hierarchy_density"));settings.mask_transition=number(s.at("mask_transition"));settings.fusion_width=number(s.at("fusion_width"));settings.density_scale=number(s.at("density_scale"));settings.growth_direction=vec(s.at("growth_direction"));return source;
}
Json anvil_json(const AnvilSource& source){const auto& s=source.settings;return {{"contract_version",source.contract_version},{"cloud",top_lobe_json(source.cloud)},
    {"settings",{{"enabled",s.enabled},{"follow_wind",s.follow_wind},{"start_height",s.start_height},{"thickness",s.thickness},{"width",s.width},{"extension",s.extension},{"direction",vec(s.direction)},{"shear",s.shear},{"edge_fade",s.edge_fade},{"density_scale",s.density_scale}}}};}
AnvilSource anvil_from_json(const Json& j){
    shape(j,{"contract_version","cloud","settings"});if(!j.at("contract_version").is_number_unsigned()||j.at("contract_version")!=anvil_contract_version)throw std::invalid_argument("Unsupported anvil source version");
    AnvilSource source;source.cloud=top_lobe_from_json(j.at("cloud"));const auto& s=j.at("settings");shape(s,{"enabled","follow_wind","start_height","thickness","width","extension","direction","shear","edge_fade","density_scale"});
    if(!s.at("enabled").is_boolean()||!s.at("follow_wind").is_boolean())throw std::invalid_argument("Anvil enabled/follow_wind must be boolean");
    source.settings={s.at("enabled").get<bool>(),s.at("follow_wind").get<bool>(),number(s.at("start_height")),number(s.at("thickness")),number(s.at("width")),number(s.at("extension")),vec(s.at("direction")),number(s.at("shear")),number(s.at("edge_fade")),number(s.at("density_scale"))};return source;
}
bool boolean(const Json& j) {
    if(!j.is_boolean())throw std::invalid_argument("Expected boolean parameter");
    return j.get<bool>();
}
std::uint32_t unsigned32(const Json& j) {
    if(!j.is_number_unsigned()||j.get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Expected uint32 parameter");
    return j.get<std::uint32_t>();
}
const Json& bounded_array(const Json& j,std::size_t minimum,std::size_t maximum,const char* message) {
    if(!j.is_array()||j.size()<minimum||j.size()>maximum)throw std::invalid_argument(message);
    return j;
}
std::string bounded_string(const Json& j,std::size_t maximum) {
    if(!j.is_string()||j.get_ref<const std::string&>().size()>maximum)throw std::invalid_argument("Invalid bounded string");
    return j.get<std::string>();
}
Json bounds_json(const Bounds& b){return {{"min",vec(b.min)},{"max",vec(b.max)}};}
Bounds bounds_from_json(const Json& j){shape(j,{"min","max"});return {vec(j.at("min")),vec(j.at("max"))};}
Json recipe_json(const CloudRecipe& c) {
    Json cells=Json::array(),cuts=Json::array();
    for(const auto& cell:c.cells)cells.push_back(cell_json(cell));
    for(const auto& cut:c.cuts)cuts.push_back(cut_json(cut));
    return {{"id",std::to_string(c.id)},{"cells",cells},{"cuts",cuts},{"transform",transform_json(c.transform)},
        {"envelope",bounds_json(c.envelope)},{"base",{{"enabled",c.base.enabled},{"height",c.base.height},{"transition",c.base.transition}}},
        {"density",c.density},{"blend_width",c.blend_width},{"overlap",c.overlap},
        {"structure_seed",std::to_string(c.structure_seed)},{"detail_seed",std::to_string(c.detail_seed)},
        {"optics",optics_json(c.optics)},{"noise",noise_json(c.noise)},{"altitude_density",altitude_density_json(c.altitude_density)}};
}
CloudRecipe recipe_from_json(const Json& j) {
    shape(j,{"id","cells","cuts","transform","envelope","base","density","blend_width","overlap","structure_seed","detail_seed","optics","noise","altitude_density"});
    const auto& cells=bounded_array(j.at("cells"),0,8,"Recipe exceeds eight cells");
    const auto& cuts=bounded_array(j.at("cuts"),0,8,"Recipe exceeds eight cuts");
    CloudRecipe c;c.id=id(j.at("id"));c.cells.clear();
    for(const auto& cell:cells)c.cells.push_back(cell_from_json(cell));
    for(const auto& cut:cuts)c.cuts.push_back(cut_from_json(cut));
    c.transform=transform_from_json(j.at("transform"));c.envelope=bounds_from_json(j.at("envelope"));
    const auto& base=j.at("base");shape(base,{"enabled","height","transition"});
    c.base={boolean(base.at("enabled")),number(base.at("height")),number(base.at("transition"))};
    c.density=number(j.at("density"));c.blend_width=number(j.at("blend_width"));c.overlap=number(j.at("overlap"));
    c.structure_seed=id(j.at("structure_seed"));c.detail_seed=id(j.at("detail_seed"));
    c.optics=optics_from_json(j.at("optics"));c.noise=noise_from_json(j.at("noise"));
    c.altitude_density=altitude_density_from_json(j.at("altitude_density"));return c;
}
Json generation_recipe_json(const GenerationRecipe& recipe) {
    return std::visit([](const auto& source)->Json {
        using T=std::decay_t<decltype(source)>;
        if constexpr(std::is_same_v<T,CloudRecipe>)return {{"kind","custom"},{"source",recipe_json(source)}};
        else if constexpr(std::is_same_v<T,CumulonimbusGroup>)return {{"kind","cumulonimbus"},{"source",cumulonimbus_json(source)}};
        else if constexpr(std::is_same_v<T,CenterlineShape>)return {{"kind","centerline"},{"source",centerline_json(source)}};
        else if constexpr(std::is_same_v<T,DevelopedCloud>)return {{"kind","developed"},{"source",developed_json(source)}};
        else if constexpr(std::is_same_v<T,TopLobeSource>)return {{"kind","top_lobes"},{"source",top_lobe_json(source)}};
        else return {{"kind","anvil"},{"source",anvil_json(source)}};
    },recipe);
}
GenerationRecipe generation_recipe_from_json(const Json& j) {
    shape(j,{"kind","source"});const auto& source=j.at("source");
    if(j.at("kind")=="custom")return recipe_from_json(source);
    if(j.at("kind")=="cumulonimbus")return cumulonimbus_from_json(source);
    if(j.at("kind")=="centerline")return centerline_from_json(source);
    if(j.at("kind")=="developed")return developed_from_json(source);
    if(j.at("kind")=="top_lobes")return top_lobe_from_json(source);
    if(j.at("kind")=="anvil")return anvil_from_json(source);
    throw std::invalid_argument("Unsupported generation provenance kind");
}
Json generation_settings_json(const GenerationSettings& s) {
    Json wind=Json::array(),cells=Json::array();
    for(const auto& k:s.wind)wind.push_back({{"altitude",k.altitude},{"displacement",vec(k.displacement)}});
    for(const auto& c:s.cells){Json pinned=Json::array();for(auto value:c.pinned_controls)pinned.push_back(std::to_string(value));
        cells.push_back({{"cell_id",std::to_string(c.cell_id)},{"start_stage",c.start_stage},{"amount",c.amount},{"pinned_controls",pinned}});}
    return {{"algorithm_version",s.algorithm_version},{"enabled",s.enabled},{"stage",s.stage},{"initial_height_fraction",s.initial_height_fraction},
        {"wind_base",s.wind_base},{"wind_height",s.wind_height},{"wind",wind},{"reference_translation",vec(s.reference_translation)},{"cells",cells}};
}
GenerationSettings generation_settings_from_json(const Json& j) {
    shape(j,{"algorithm_version","enabled","stage","initial_height_fraction","wind_base","wind_height","wind","reference_translation","cells"});
    GenerationSettings s;s.algorithm_version=unsigned32(j.at("algorithm_version"));s.enabled=boolean(j.at("enabled"));
    s.stage=number(j.at("stage"));s.initial_height_fraction=number(j.at("initial_height_fraction"));
    s.wind_base=number(j.at("wind_base"));s.wind_height=number(j.at("wind_height"));s.reference_translation=vec(j.at("reference_translation"));
    const auto& wind=bounded_array(j.at("wind"),2,max_wind_knots,"Generation provenance requires 2..6 wind knots");
    const auto& cells=bounded_array(j.at("cells"),0,max_developed_cells,"Generation provenance exceeds two growth rules");
    s.wind.clear();for(const auto& k:wind){shape(k,{"altitude","displacement"});s.wind.push_back({number(k.at("altitude")),vec(k.at("displacement"))});}
    for(const auto& c:cells){shape(c,{"cell_id","start_stage","amount","pinned_controls"});
        DevelopmentGrowth growth{id(c.at("cell_id")),number(c.at("start_stage")),number(c.at("amount")),{}};
        const auto& pinned=bounded_array(c.at("pinned_controls"),0,max_centerline_points,"Generation provenance exceeds six pinned controls");
        for(const auto& value:pinned)growth.pinned_controls.push_back(id(value));
        s.cells.push_back(std::move(growth));}
    // The saved format keeps the current units and bounds even when its
    // generation implementation is unavailable. Disabled validation checks
    // settings only; it never prepares a source or evaluates structure.
    auto validation=s;validation.enabled=false;validation.algorithm_version=generation_algorithm_version;
    const auto errors=validate_generation(Scene{},validation);
    if(!errors.empty())throw std::invalid_argument("Invalid generation provenance settings: "+errors.front());
    return s;
}
Json frozen_anvil_json(const FrozenAnvil& a) {
    return {{"center",vec(a.center)},{"direction",vec(a.direction)},{"along_radius",a.along_radius},{"cross_radius",a.cross_radius},
        {"half_thickness",a.half_thickness},{"shear",a.shear},{"edge_fade",a.edge_fade},{"density_scale",a.density_scale},{"start_height",a.start_height}};
}
FrozenAnvil frozen_anvil_from_json(const Json& j) {
    shape(j,{"center","direction","along_radius","cross_radius","half_thickness","shear","edge_fade","density_scale","start_height"});
    return {vec(j.at("center")),vec(j.at("direction")),number(j.at("along_radius")),number(j.at("cross_radius")),number(j.at("half_thickness")),
        number(j.at("shear")),number(j.at("edge_fade")),number(j.at("density_scale")),number(j.at("start_height"))};
}
Json frozen_json(const FrozenCloudState& state,bool include_payload_hash=true) {
    Json fields=Json::array(),curves=Json::array(),hierarchy=Json::array();
    for(const auto& field:state.fields){const auto& l=field.layers;Json evaluated={{"development_id",std::to_string(field.development_id)},
        {"recipe",recipe_json(field.recipe)},{"translation",vec(field.translation)},
        {"layers",{{"base",l.base},{"macro",l.macro},{"medium",l.medium},{"micro",l.micro}}}};
        if(state.contract_version>=2)evaluated["clipping_envelope"]=field.clipping_envelope?bounds_json(*field.clipping_envelope):Json(nullptr);
        fields.push_back(std::move(evaluated));}
    for(const auto& curve:state.curves){Json points=Json::array(),profile=Json::array();
        for(const auto& p:curve.points)points.push_back({{"id",std::to_string(p.id)},{"t",p.t},{"offset",vec(p.offset)}});
        for(const auto& p:curve.profile)profile.push_back({{"id",std::to_string(p.id)},{"t",p.t},{"radius_scale",p.radius_scale},{"density_scale",p.density_scale}});
        curves.push_back({{"development_id",std::to_string(curve.development_id)},{"base",curve.base},{"height",curve.height},
            {"growth_direction",vec(curve.growth_direction)},{"points",points},{"profile",profile}});}
    for(const auto& node:state.hierarchy)hierarchy.push_back({{"id",std::to_string(node.id)},{"parent_id",std::to_string(node.parent_id)},{"depth",node.depth}});
    Json provenance=nullptr;if(state.provenance)provenance={{"initial",generation_recipe_json(state.provenance->initial)},{"settings",generation_settings_json(state.provenance->settings)}};
    Json j={{"contract_version",state.contract_version},{"id",std::to_string(state.id)},
        {"selection",{{"kind",state.selection_kind},{"unit",state.selection_unit},{"value",state.selection_value}}},
        {"generation_version",state.generation_version},{"generation_input_hash",std::to_string(state.generation_input_hash)},
        {"content_hash",std::to_string(state.content_hash)},
        {"transform",transform_json(state.transform)},{"optics",optics_json(state.optics)},{"fields",fields},{"curves",curves},
        {"fusion_width",state.fusion_width},{"overlap",state.overlap},{"top_enabled",state.top_enabled},
        {"top_boundary",state.top_boundary},{"top_mode",state.top_mode},{"hierarchy",hierarchy},
        {"anvil",state.anvil?frozen_anvil_json(*state.anvil):Json(nullptr)},
        {"support",bounds_json(state.support)},{"rho_max",state.rho_max},{"provenance",provenance}};
    if(include_payload_hash)j["payload_hash"]=std::to_string(state.payload_hash);
    return j;
}
FrozenCloudState frozen_from_json(const Json& j) {
    shape(j,{"contract_version","id","selection","generation_version","generation_input_hash","content_hash","payload_hash","transform","optics",
        "fields","curves","fusion_width","overlap","top_enabled","top_boundary","top_mode","hierarchy","anvil","support","rho_max","provenance"});
    const auto contract=unsigned32(j.at("contract_version"));
    if(contract!=1&&contract!=frozen_cloud_contract_version)throw std::invalid_argument("Unsupported frozen cloud contract version");
    FrozenCloudState state;state.contract_version=contract;state.id=id(j.at("id"));const auto& selection=j.at("selection");shape(selection,{"kind","unit","value"});
    state.selection_kind=bounded_string(selection.at("kind"),64);state.selection_unit=bounded_string(selection.at("unit"),64);state.selection_value=number(selection.at("value"));
    state.generation_version=unsigned32(j.at("generation_version"));state.generation_input_hash=id(j.at("generation_input_hash"));state.content_hash=id(j.at("content_hash"));
    state.payload_hash=id(j.at("payload_hash"));
    state.transform=transform_from_json(j.at("transform"));state.optics=optics_from_json(j.at("optics"));
    const auto& fields=bounded_array(j.at("fields"),0,2,"Frozen cloud exceeds two evaluated fields");
    const auto& curves=bounded_array(j.at("curves"),0,max_developed_cells,"Frozen cloud exceeds two evaluated curves");
    const auto& hierarchy=bounded_array(j.at("hierarchy"),0,max_top_lobes,"Frozen cloud exceeds three hierarchy nodes");
    std::size_t primitive_count=0;
    for(const auto& f:fields){
        if(contract==1)shape(f,{"development_id","recipe","translation","layers"});
        else shape(f,{"development_id","recipe","translation","layers","clipping_envelope"});
        const auto& recipe=f.at("recipe");
        primitive_count+=bounded_array(recipe.at("cells"),0,8,"Frozen field exceeds eight primitives").size();
        if(primitive_count>8)throw std::invalid_argument("Frozen cloud exceeds eight aggregate primitives");
        const auto& l=f.at("layers");shape(l,{"base","macro","medium","micro"});
        state.fields.push_back({id(f.at("development_id")),recipe_from_json(recipe),vec(f.at("translation")),
            {boolean(l.at("base")),boolean(l.at("macro")),boolean(l.at("medium")),boolean(l.at("micro"))}});
        if(contract>=2&&!f.at("clipping_envelope").is_null())state.fields.back().clipping_envelope=bounds_from_json(f.at("clipping_envelope"));
    }
    for(const auto& c:curves){shape(c,{"development_id","base","height","growth_direction","points","profile"});
        const auto& points=bounded_array(c.at("points"),2,max_centerline_points,"Frozen curve requires 2..6 control points");
        const auto& profile=bounded_array(c.at("profile"),2,max_centerline_profile_points,"Frozen curve requires 2..8 profile knots");
        FrozenCurve curve;curve.development_id=id(c.at("development_id"));curve.base=number(c.at("base"));curve.height=number(c.at("height"));curve.growth_direction=vec(c.at("growth_direction"));
        for(const auto& p:points){shape(p,{"id","t","offset"});curve.points.push_back({id(p.at("id")),number(p.at("t")),vec(p.at("offset"))});}
        for(const auto& p:profile){shape(p,{"id","t","radius_scale","density_scale"});curve.profile.push_back({id(p.at("id")),number(p.at("t")),number(p.at("radius_scale")),number(p.at("density_scale"))});}
        state.curves.push_back(std::move(curve));}
    for(const auto& n:hierarchy){shape(n,{"id","parent_id","depth"});const auto depth=unsigned32(n.at("depth"));
        if(depth>max_top_lobe_depth)throw std::invalid_argument("Frozen hierarchy exceeds bounded depth");
        state.hierarchy.push_back({id(n.at("id")),id(n.at("parent_id")),depth});}
    state.fusion_width=number(j.at("fusion_width"));state.overlap=number(j.at("overlap"));state.top_enabled=boolean(j.at("top_enabled"));
    state.top_boundary=number(j.at("top_boundary"));state.top_mode=unsigned32(j.at("top_mode"));
    if(!j.at("anvil").is_null())state.anvil=frozen_anvil_from_json(j.at("anvil"));
    state.support=bounds_from_json(j.at("support"));state.rho_max=number(j.at("rho_max"));
    if(!j.at("provenance").is_null()){const auto& p=j.at("provenance");shape(p,{"initial","settings"});
        state.provenance=GenerationProvenance{generation_recipe_from_json(p.at("initial")),generation_settings_from_json(p.at("settings"))};}
    if(frozen_payload_hash(state)!=state.payload_hash)throw std::invalid_argument("Frozen cloud payload hash mismatch");
    if(frozen_content_hash(state)!=state.content_hash)throw std::invalid_argument("Frozen cloud content hash mismatch");
    if(contract==1)migrate_frozen_v1(state);
    return state;
}
Json finishing_json(const FinishStack& stack){
    Json layers=Json::array();for(const auto& layer:stack.layers){const auto kind=layer.kind==FinishModifierKind::cut?"cut":layer.kind==FinishModifierKind::density?"density":"protect_detail";
        layers.push_back({{"id",std::to_string(layer.id)},{"kind",kind},{"enabled",layer.enabled},{"strength",layer.strength},
            {"mask",{{"center",vec(layer.mask.center)},{"radii",vec(layer.mask.radii)},{"falloff",layer.mask.falloff}}},
            {"target",{{"kind",layer.target_kind==FinishTargetKind::object?"object":"field"},{"id",std::to_string(layer.target_id)}}},
            {"density_multiplier",layer.density_multiplier},{"hard_cut",layer.hard_cut}});}
    return {{"contract_version",stack.contract_version},{"space","object_local"},{"layers",layers}};
}
FinishStack finishing_from_json(const Json& j){
    shape(j,{"contract_version","space","layers"});FinishStack stack;stack.contract_version=unsigned32(j.at("contract_version"));
    if(j.at("space")!="object_local")throw std::invalid_argument("Only object-local finishing masks are supported");
    for(const auto& value:bounded_array(j.at("layers"),0,max_finish_modifiers,"Finishing exceeds four layers")){
        shape(value,{"id","kind","enabled","strength","mask","target","density_multiplier","hard_cut"});FinishModifier layer;layer.id=id(value.at("id"));
        if(value.at("kind")=="cut")layer.kind=FinishModifierKind::cut;else if(value.at("kind")=="density")layer.kind=FinishModifierKind::density;else if(value.at("kind")=="protect_detail")layer.kind=FinishModifierKind::protect_detail;else throw std::invalid_argument("Unknown finishing operation");
        layer.enabled=boolean(value.at("enabled"));layer.strength=number(value.at("strength"));layer.density_multiplier=number(value.at("density_multiplier"));layer.hard_cut=boolean(value.at("hard_cut"));
        const auto& mask=value.at("mask");shape(mask,{"center","radii","falloff"});layer.mask={vec(mask.at("center")),vec(mask.at("radii")),number(mask.at("falloff"))};
        const auto& target=value.at("target");shape(target,{"kind","id"});layer.target_id=id(target.at("id"));
        if(target.at("kind")=="object")layer.target_kind=FinishTargetKind::object;else if(target.at("kind")=="field")layer.target_kind=FinishTargetKind::field;else throw std::invalid_argument("Unknown finishing target type");
        stack.layers.push_back(layer);
    }return stack;
}
std::uint64_t json_hash(const Json& j) {
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char byte:j.dump()){hash^=byte;hash*=1099511628211ull;}
    return hash;
}
Json density_recipe_json(const FrozenField& field) {
    auto recipe=frozen_effective_recipe(field);
    std::sort(recipe.cells.begin(),recipe.cells.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    std::sort(recipe.cuts.begin(),recipe.cuts.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    auto j=recipe_json(recipe);j.erase("id");j.erase("optics");j.erase("transform");j.erase("structure_seed");
    // Primitive IDs select warp keys and smooth-min iteration order. Hash those
    // effects instead of treating the IDs as either field data or pure metadata.
    for(std::size_t i=0;i<recipe.cells.size();++i){auto& cell=j["cells"][i];cell.erase("id");cell.erase("structure_seed");
        if(recipe.noise.warp_amplitude>0)cell["warp_key"]=std::to_string(cell_random_key(recipe,recipe.cells[i]));}
    for(auto& cut:j["cuts"])cut.erase("id");
    return j;
}
void push_bounded(std::vector<Scene>& history,Scene scene) {
    if(history.size()==128)history.erase(history.begin());
    history.push_back(std::move(scene));
}
std::runtime_error file_error(const char* operation) {return std::runtime_error(std::string(operation)+": "+std::generic_category().message(errno));}
}
std::uint64_t frozen_payload_hash(const FrozenCloudState& state) {return json_hash(frozen_json(state,false));}
std::uint64_t frozen_content_hash(const FrozenCloudState& state) {
    auto j=frozen_json(state,false);
    for(const char* key:{"content_hash","generation_version","generation_input_hash","provenance","optics","support","rho_max"})j.erase(key);
    for(auto& field:j["fields"]){
        field.erase("layers");auto& recipe=field["recipe"];
        recipe.erase("optics");recipe.erase("detail_seed");recipe.erase("envelope");
        const auto origin=recipe["noise"]["origin"];recipe["noise"]={{"origin",origin}};
    }
    return json_hash(j);
}
std::uint64_t frozen_density_hash(const FrozenCloudState& state) {
    Json fields=Json::array();for(const auto& field:state.fields)fields.push_back({{"recipe",density_recipe_json(field)},{"translation",vec(field.translation)}});
    return json_hash({{"contract_version",state.contract_version},{"transform",transform_json(state.transform)},{"fields",fields},
        {"fusion_width",state.fusion_width},{"overlap",state.overlap},{"top_enabled",state.top_enabled},{"top_boundary",state.top_boundary},
        {"anvil",state.anvil?frozen_anvil_json(*state.anvil):Json(nullptr)},{"support",bounds_json(state.support)},{"rho_max",state.rho_max}});
}
std::string scene_json(const Scene& s) {
    require_valid(s);
    Json j={
        {"schema_version",s.schema_version},{"algorithm_version",s.algorithm_version},
        {"cloud",recipe_json(s.cloud)},
        {"camera",{{"position",vec(s.camera.position)},{"target",vec(s.camera.target)},{"up",vec(s.camera.up)},
            {"vertical_fov_degrees",s.camera.vertical_fov_degrees},{"near_plane",s.camera.near_plane},{"far_plane",s.camera.far_plane}}},
        {"sun",{{"direction_to_light",vec(s.sun.direction_to_light)},{"irradiance",vec(s.sun.irradiance)}}},
        {"exposure_ev",s.exposure_ev},{"preview_approx",{{"enabled",s.preview_approx.enabled},{"strength",s.preview_approx.strength}}}};
    if(s.frozen)j["cloud"]={{"kind","frozen"},{"source",frozen_json(*s.frozen)},{"finishing",finishing_json(s.finish_stack)}};
    else if(s.anvil)j["cloud"]={{"kind","anvil"},{"source",anvil_json(*s.anvil)}};
    else if(s.top_lobes)j["cloud"]={{"kind","top_lobes"},{"source",top_lobe_json(*s.top_lobes)}};
    else if(s.developed)j["cloud"]={{"kind","developed"},{"source",developed_json(*s.developed)}};
    else if(s.centerline)j["cloud"]={{"kind","centerline"},{"source",centerline_json(*s.centerline)}};
    else if(s.cumulonimbus)j["cloud"]={{"kind","cumulonimbus"},{"source",cumulonimbus_json(*s.cumulonimbus)}};
    return j.dump(2)+"\n";
}
Scene parse_scene_json(std::string_view text) {
    if(text.size()>max_scene_bytes)throw std::invalid_argument("Scene exceeds 4 MiB limit");
    auto j=Json::parse(text,[](int depth,Json::parse_event_t,Json&){
        if(depth>32)throw std::invalid_argument("Scene nesting exceeds 32 levels");
        return true;
    });
    if(j.value("schema_version",0u)>=4&&j.value("schema_version",0u)<=11)shape(j,{"schema_version","algorithm_version","cloud","camera","sun","exposure_ev","preview_approx"});
    else shape(j,{"schema_version","algorithm_version","cloud","camera","sun","exposure_ev"});
    if(!j.at("schema_version").is_number_unsigned()||!j.at("algorithm_version").is_number_unsigned())throw std::invalid_argument("Version must be an unsigned integer");
    const bool legacy=j.at("schema_version")==1&&j.at("algorithm_version")==1;
    const bool version2=j.at("schema_version")==2&&j.at("algorithm_version")==2;
    const bool old_schema=(j.at("schema_version")==3||j.at("schema_version")==4||j.at("schema_version")==5)&&j.at("algorithm_version")==2;
    const bool profile_schema=j.at("schema_version")==6&&j.at("algorithm_version")==3;
    const bool developed_schema=j.at("schema_version")==7&&j.at("algorithm_version")==3;
    const bool top_schema=j.at("schema_version")==8&&j.at("algorithm_version")==3;
    const bool anvil_schema=j.at("schema_version")==9&&j.at("algorithm_version")==3;
    const bool frozen_schema=j.at("schema_version")==10&&j.at("algorithm_version")==3;
    const bool current_schema=j.at("schema_version")==11&&j.at("algorithm_version")==3;
    if(!legacy&&!version2&&!old_schema&&!profile_schema&&!developed_schema&&!top_schema&&!anvil_schema&&!frozen_schema&&!current_schema)throw std::invalid_argument("Unsupported schema/algorithm version");
    if(legacy) {
        shape(j.at("cloud"),{"id","cells","cuts","transform","envelope","base","density","blend_width","overlap","structure_seed","detail_seed","optics"});
        j["cloud"]["noise"]=noise_json(NoiseSettings{}); // exact old shape: all noise amplitudes zero
    }
    if(legacy||version2){shape(j["cloud"]["optics"],{"extinction_scale","albedo"});j["cloud"]["optics"]["g"]=0;}
    Scene s;
    if(j.at("schema_version").get<unsigned>()>=4){const auto& a=j.at("preview_approx");shape(a,{"enabled","strength"});if(!a.at("enabled").is_boolean())throw std::invalid_argument("Approximation enabled must be boolean");s.preview_approx={a.at("enabled").get<bool>(),number(a.at("strength"))};}
    if(!current_schema&&!frozen_schema&&!anvil_schema&&!top_schema&&!profile_schema&&!developed_schema&&!j.at("cloud").contains("kind")) {
        if(j.at("cloud").contains("altitude_density"))throw std::invalid_argument("Legacy schema cannot contain altitude density fields");
        j["cloud"]["altitude_density"]=altitude_density_json(AltitudeDensityProfile{});
    }
    auto& c=s.cloud;const auto& cj=j.at("cloud");
    if((j.at("schema_version")==5||profile_schema||developed_schema||top_schema||anvil_schema||frozen_schema||current_schema)&&cj.is_object()&&cj.contains("kind")) {
        if(current_schema&&cj.at("kind")=="frozen")shape(cj,{"kind","source","finishing"});else shape(cj,{"kind","source"});
        if(cj.at("kind")=="cumulonimbus") {
            s.cumulonimbus=cumulonimbus_from_json(cj.at("source"));
            c=derive_cumulonimbus_recipe(*s.cumulonimbus);
        } else if((profile_schema||developed_schema||top_schema||anvil_schema||frozen_schema||current_schema)&&cj.at("kind")=="centerline") {
            s.centerline=centerline_from_json(cj.at("source"));
            c=lower_centerline_to_recipe(*s.centerline);
        } else if((developed_schema||top_schema||anvil_schema||frozen_schema||current_schema)&&cj.at("kind")=="developed") {
            s.developed=developed_from_json(cj.at("source"));
            c=developed_proxy_recipe(*s.developed);
        } else if((frozen_schema||current_schema)&&cj.at("kind")=="frozen") {
            s.frozen=frozen_from_json(cj.at("source"));c=frozen_proxy_recipe(*s.frozen);if(current_schema)s.finish_stack=finishing_from_json(cj.at("finishing"));
        } else if((anvil_schema||frozen_schema||current_schema)&&cj.at("kind")=="anvil") {
            s.anvil=anvil_from_json(cj.at("source"));c=anvil_proxy_recipe(*s.anvil);
        } else if((top_schema||anvil_schema||frozen_schema||current_schema)&&cj.at("kind")=="top_lobes") {
            s.top_lobes=top_lobe_from_json(cj.at("source"));c=top_lobe_proxy_recipe(*s.top_lobes);
        } else throw std::invalid_argument("Unsupported cloud object kind");
    } else {
        c=recipe_from_json(cj);
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
    auto scene=document_.scene();
    if(scene.developed||scene.top_lobes||scene.anvil||scene.frozen)throw std::invalid_argument("Edit developed source cells through development commands");
    cell.id=next_id(scene);
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
    if(scene.developed||scene.top_lobes||scene.anvil||scene.frozen)throw std::invalid_argument("Remove a developed cell through its development command");
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
