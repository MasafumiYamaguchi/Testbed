#include "white/cumulonimbus.hpp"
#include "white/developed_cells.hpp"
#include "white/centerline_scene.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace white {
namespace {
constexpr std::array parameter_info{
    CumulonimbusParameterInfo{CumulonimbusParameter::width,"Width","m","10..2000",CumulonimbusValueKind::scalar},
    CumulonimbusParameterInfo{CumulonimbusParameter::height,"Height","m","10..4000",CumulonimbusValueKind::scalar},
    CumulonimbusParameterInfo{CumulonimbusParameter::cloud_base,"Cloud base","m","-100000..100000",CumulonimbusValueKind::scalar},
    CumulonimbusParameterInfo{CumulonimbusParameter::growth_direction,"Growth direction","unit vector","length 1; y >= 0.2",CumulonimbusValueKind::unit_direction},
    CumulonimbusParameterInfo{CumulonimbusParameter::density,"Density","multiplier","0..1000",CumulonimbusValueKind::scalar},
    CumulonimbusParameterInfo{CumulonimbusParameter::structure_seed,"Structure seed","uint64","0..18446744073709551615",CumulonimbusValueKind::seed},
    CumulonimbusParameterInfo{CumulonimbusParameter::detail_seed,"Detail seed","uint64","0..18446744073709551615",CumulonimbusValueKind::seed}
};
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool range(double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
void require(const std::vector<std::string>& errors) {
    if(errors.empty())return;
    std::string message="Invalid Cumulonimbus source:";
    for(const auto& error:errors)message+="\n- "+error;
    throw std::invalid_argument(message);
}
std::vector<std::string> source_errors(const CumulonimbusGroup& group) {
    std::vector<std::string> errors;
    auto check=[&](bool ok,const char* text){if(!ok)errors.emplace_back(text);};
    check(group.contract_version==cumulonimbus_contract_version,"Unsupported Cumulonimbus contract version; explicit migration or Custom Cloud conversion is required");
    const auto& p=group.parameters;
    check(range(p.width,10,2000),"Width must be finite in 10..2000 metres");
    check(range(p.height,10,4000),"Height must be finite in 10..4000 metres");
    check(range(p.cloud_base,-100000,100000),"Cloud base must be finite in -100000..100000 metres");
    check(finite(p.growth_direction)&&std::abs(dot(p.growth_direction,p.growth_direction)-1)<=1e-9&&p.growth_direction.y>=0.2,
        "Growth direction must be a unit upward vector with y >= 0.2");
    check(range(p.density,0,1000),"Density must be finite in 0..1000");
    std::set<Id> ids;
    check(group.cloud_id!=0&&ids.insert(group.cloud_id).second,"Cloud ID must be nonzero");
    for(auto id:group.cell_ids)check(id!=0&&ids.insert(id).second,"Generated Cell IDs must be nonzero and globally unique");
    check(group.cell_adjustments.size()<=cumulonimbus_generated_cells,"At most five generated-cell adjustments are supported");
    std::set<Id> adjusted;
    for(std::size_t i=0;i<std::min(group.cell_adjustments.size(),cumulonimbus_generated_cells);++i) {
        const auto& a=group.cell_adjustments[i];
        check(std::find(group.cell_ids.begin(),group.cell_ids.end(),a.cell_id)!=group.cell_ids.end()&&adjusted.insert(a.cell_id).second,
            "Cell adjustment must reference one unique generated Cell ID");
        check(finite(a.center_offset)&&std::max({std::abs(a.center_offset.x),std::abs(a.center_offset.y),std::abs(a.center_offset.z)})<=100000,
            "Cell center adjustment must be finite within 100000 local metres");
        check(range(a.radius_scale.x,0.01,4)&&range(a.radius_scale.y,0.01,4)&&range(a.radius_scale.z,0.01,4),
            "Cell radius adjustment must be finite in 0.01..4");
    }
    check(group.modifiers.manual_cells.size()<=8-cumulonimbus_generated_cells,"Prefab permits at most three additional manual cells");
    check(group.modifiers.cuts.size()<=8,"Prefab permits at most eight cuts");
    return errors;
}
CloudRecipe derive_unchecked(const CumulonimbusGroup& group) {
    const auto& p=group.parameters;const auto& m=group.modifiers;
    CloudRecipe recipe;recipe.id=group.cloud_id;recipe.transform=m.transform;
    recipe.density=p.density;recipe.structure_seed=p.structure_seed;recipe.detail_seed=p.detail_seed;
    recipe.optics=m.optics;recipe.noise=m.noise;recipe.blend_width=m.blend_width;recipe.overlap=m.overlap;
    recipe.base={m.base_enabled,p.cloud_base,m.base_transition};recipe.cuts=m.cuts;recipe.cells.clear();
    // Version 1: five upright ellipsoids. Height is nominal vertical extent;
    // direction tilts the centers, while width sets unmodified lobe dimensions.
    constexpr std::array<Vec3,5> centers{{{0,0.18,0},{0,0.42,0},{0,0.70,0},{-0.22,0.72,0.08},{0.22,0.76,-0.08}}};
    constexpr std::array<Vec3,5> radii{{{0.45,0.24,0.38},{0.32,0.32,0.30},{0.40,0.30,0.36},{0.26,0.21,0.24},{0.26,0.20,0.24}}};
    for(std::size_t i=0;i<cumulonimbus_generated_cells;++i) {
        const double y=centers[i].y*p.height;
        Cell cell{group.cell_ids[i],
            {centers[i].x*p.width+y*p.growth_direction.x/p.growth_direction.y,p.cloud_base+y,
             centers[i].z*p.width+y*p.growth_direction.z/p.growth_direction.y},
            {radii[i].x*p.width,radii[i].y*p.height,radii[i].z*p.width},std::uint64_t(i)};
        const auto adjustment=std::find_if(group.cell_adjustments.begin(),group.cell_adjustments.end(),[&](const auto& a){return a.cell_id==cell.id;});
        if(adjustment!=group.cell_adjustments.end()) {
            cell.center=cell.center+adjustment->center_offset;
            cell.radii={cell.radii.x*adjustment->radius_scale.x,cell.radii.y*adjustment->radius_scale.y,cell.radii.z*adjustment->radius_scale.z};
            if(adjustment->structure_seed)cell.structure_seed=*adjustment->structure_seed;
        }
        recipe.cells.push_back(cell);
    }
    recipe.cells.insert(recipe.cells.end(),m.manual_cells.begin(),m.manual_cells.end());
    // Envelope is derived after local edits so growth never clips a preserved
    // manual cell. Smooth union expands the implicit distance by <= (N-1) k/4.
    // The ellipsoid implicit uses its smallest radius; scale the margin by
    // the largest axis ratio to bound anisotropic ellipsoids conservatively.
    const double distance_margin=(recipe.cells.size()-1)*recipe.blend_width/4+recipe.noise.warp_amplitude+2;
    Bounds envelope{{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()},
                    {-std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}};
    for(const auto& cell:recipe.cells) {
        const double smallest=std::min({cell.radii.x,cell.radii.y,cell.radii.z});
        const double factor=1+distance_margin/smallest;
        const auto radius=cell.radii*factor;
        const auto lo=cell.center-radius,hi=cell.center+radius;
        envelope.min={std::min(envelope.min.x,lo.x),std::min(envelope.min.y,lo.y),std::min(envelope.min.z,lo.z)};
        envelope.max={std::max(envelope.max.x,hi.x),std::max(envelope.max.y,hi.y),std::max(envelope.max.z,hi.z)};
    }
    recipe.envelope=envelope;return recipe;
}
template<class T> const T& command_value(const CumulonimbusCommand& command) {
    const auto value=std::get_if<T>(&command.value);
    if(!value)throw std::invalid_argument("Cumulonimbus command value type does not match parameter");
    return *value;
}
void set(CumulonimbusParameters& p,const CumulonimbusCommand& command) {
    switch(command.parameter) {
    case CumulonimbusParameter::width:p.width=command_value<double>(command);break;
    case CumulonimbusParameter::height:p.height=command_value<double>(command);break;
    case CumulonimbusParameter::cloud_base:p.cloud_base=command_value<double>(command);break;
    case CumulonimbusParameter::growth_direction:p.growth_direction=command_value<Vec3>(command);break;
    case CumulonimbusParameter::density:p.density=command_value<double>(command);break;
    case CumulonimbusParameter::structure_seed:p.structure_seed=command_value<std::uint64_t>(command);break;
    case CumulonimbusParameter::detail_seed:p.detail_seed=command_value<std::uint64_t>(command);break;
    default:throw std::invalid_argument("Unknown Cumulonimbus parameter");
    }
}
void push_history(std::vector<CumulonimbusGroup>& history,const CumulonimbusGroup& group) {
    if(history.size()==128)history.erase(history.begin());
    history.push_back(group);
}
}
std::span<const CumulonimbusParameterInfo> cumulonimbus_parameter_info(){return parameter_info;}
CumulonimbusValue cumulonimbus_value(const CumulonimbusParameters& p,CumulonimbusParameter parameter) {
    switch(parameter) {
    case CumulonimbusParameter::width:return p.width;
    case CumulonimbusParameter::height:return p.height;
    case CumulonimbusParameter::cloud_base:return p.cloud_base;
    case CumulonimbusParameter::growth_direction:return p.growth_direction;
    case CumulonimbusParameter::density:return p.density;
    case CumulonimbusParameter::structure_seed:return p.structure_seed;
    case CumulonimbusParameter::detail_seed:return p.detail_seed;
    default:throw std::invalid_argument("Unknown Cumulonimbus parameter");
    }
}
std::vector<std::string> validate_cumulonimbus(const CumulonimbusGroup& group) {
    auto errors=source_errors(group);if(!errors.empty())return errors;
    Scene scene;scene.cloud=derive_unchecked(group);
    for(const auto& error:validate(scene))errors.push_back("Derived Recipe: "+error);
    return errors;
}
CloudRecipe derive_cumulonimbus_recipe(const CumulonimbusGroup& group) {
    require(validate_cumulonimbus(group));return derive_unchecked(group);
}
FieldGraph derive_cumulonimbus_graph(const CumulonimbusGroup& group) {
    return field_graph_from_recipe(derive_cumulonimbus_recipe(group));
}
FieldGraph cumulonimbus_to_custom_cloud(const CumulonimbusGroup& group){return derive_cumulonimbus_graph(group);}
CumulonimbusGroup command_cumulonimbus(CumulonimbusGroup group,const CumulonimbusCommand& command) {
    set(group.parameters,command);require(validate_cumulonimbus(group));return group;
}
CumulonimbusGroup edit_cumulonimbus_recipe(const CumulonimbusGroup& source,const CloudRecipe& requested) {
    const auto before=derive_cumulonimbus_recipe(source);
    Scene validation;validation.cloud=requested;require_valid(validation);
    if(requested.altitude_density!=before.altitude_density)throw std::invalid_argument("Edit altitude density through a centerline source or convert to Custom Cloud");
    if(requested.id!=before.id)throw std::invalid_argument("Prefab cloud ID cannot be changed");
    if(requested.envelope!=before.envelope)throw std::invalid_argument("Prefab envelope is derived; convert to Custom Cloud to edit it");
    auto group=source;
    group.parameters.density=requested.density;group.parameters.cloud_base=requested.base.height;
    group.parameters.structure_seed=requested.structure_seed;group.parameters.detail_seed=requested.detail_seed;
    auto& m=group.modifiers;
    m.transform=requested.transform;m.optics=requested.optics;m.noise=requested.noise;
    m.blend_width=requested.blend_width;m.overlap=requested.overlap;
    m.base_enabled=requested.base.enabled;m.base_transition=requested.base.transition;m.cuts=requested.cuts;
    m.manual_cells.clear();
    auto unadjusted=source;unadjusted.cell_adjustments.clear();
    const auto generated=derive_cumulonimbus_recipe(unadjusted);
    for(auto id:source.cell_ids) {
        const auto wanted=std::find_if(requested.cells.begin(),requested.cells.end(),[&](const auto& c){return c.id==id;});
        if(wanted==requested.cells.end())throw std::invalid_argument("Generated cells belong to the prefab; convert to Custom Cloud before deleting them");
        const auto old=std::find_if(before.cells.begin(),before.cells.end(),[&](const auto& c){return c.id==id;});
        if(*wanted==*old)continue;
        const auto base=std::find_if(generated.cells.begin(),generated.cells.end(),[&](const auto& c){return c.id==id;});
        auto adjustment=std::find_if(group.cell_adjustments.begin(),group.cell_adjustments.end(),[&](const auto& a){return a.cell_id==id;});
        if(adjustment==group.cell_adjustments.end()){group.cell_adjustments.push_back({id});adjustment=std::prev(group.cell_adjustments.end());}
        if(wanted->center!=old->center)adjustment->center_offset=wanted->center-base->center;
        if(wanted->radii!=old->radii)adjustment->radius_scale={wanted->radii.x/base->radii.x,wanted->radii.y/base->radii.y,wanted->radii.z/base->radii.z};
        if(wanted->structure_seed!=old->structure_seed)adjustment->structure_seed=wanted->structure_seed;
    }
    for(const auto& cell:requested.cells)
        if(std::find(source.cell_ids.begin(),source.cell_ids.end(),cell.id)==source.cell_ids.end())m.manual_cells.push_back(cell);
    require(validate_cumulonimbus(group));return group;
}
Scene scene_with_cumulonimbus_command(Scene scene,const CumulonimbusCommand& command) {
    if(scene.centerline)return scene_with_centerline_command(std::move(scene),CenterlineCommand{command});
    if(!scene.cumulonimbus)throw std::invalid_argument("Scene does not contain a Cumulonimbus source");
    scene.cumulonimbus=command_cumulonimbus(*scene.cumulonimbus,command);
    scene.cloud=derive_cumulonimbus_recipe(*scene.cumulonimbus);require_valid(scene);return scene;
}
Scene new_cumulonimbus_scene(Scene scene) {
    scene.developed.reset();scene.centerline.reset();scene.cumulonimbus=CumulonimbusGroup{};scene.cloud=derive_cumulonimbus_recipe(*scene.cumulonimbus);
    scene.camera.target={0,60,0};scene.camera.position={200,110,220};scene.camera.up={0,1,0};
    scene.sun.direction_to_light={0,0.8,0.6};scene.sun.irradiance={15,15,15};scene.exposure_ev=1;
    require_valid(scene);return scene;
}
Scene custom_cloud_scene(Scene scene) {
    require_valid(scene);
    if(scene.developed){scene.cloud=lower_single_developed_recipe(*scene.developed);scene.developed.reset();}
    scene.cumulonimbus.reset();scene.centerline.reset();return scene;
}
CumulonimbusDocument::CumulonimbusDocument(CumulonimbusGroup group):group_(std::move(group)){require(validate_cumulonimbus(group_));}
void CumulonimbusDocument::advance() {
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Cumulonimbus revision exhausted");
    ++revision_;
}
bool CumulonimbusDocument::apply(const CumulonimbusCommand& command) {
    auto next=group_;set(next.parameters,command);return replace(std::move(next));
}
bool CumulonimbusDocument::reset(CumulonimbusParameter parameter){return apply({parameter,cumulonimbus_value(CumulonimbusParameters{},parameter)});}
bool CumulonimbusDocument::replace(CumulonimbusGroup group) {
    require(validate_cumulonimbus(group));if(group==group_)return false;
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Cumulonimbus revision exhausted");
    if(!editing()){push_history(undo_,group_);redo_.clear();}
    group_=std::move(group);advance();return true;
}
void CumulonimbusDocument::begin_edit() {
    if(editing())throw std::logic_error("A Cumulonimbus edit is already active");
    edit_start_=group_;
}
void CumulonimbusDocument::end_edit() {
    if(!editing())throw std::logic_error("No active Cumulonimbus edit");
    if(group_!=*edit_start_){push_history(undo_,*edit_start_);redo_.clear();}
    edit_start_.reset();
}
void CumulonimbusDocument::cancel_edit() {
    if(!editing())throw std::logic_error("No active Cumulonimbus edit");
    if(group_!=*edit_start_){advance();group_=std::move(*edit_start_);}
    edit_start_.reset();
}
bool CumulonimbusDocument::undo() {
    if(!can_undo())return false;
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Cumulonimbus revision exhausted");
    push_history(redo_,group_);group_=std::move(undo_.back());undo_.pop_back();advance();return true;
}
bool CumulonimbusDocument::redo() {
    if(!can_redo())return false;
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Cumulonimbus revision exhausted");
    push_history(undo_,group_);group_=std::move(redo_.back());redo_.pop_back();advance();return true;
}
}
