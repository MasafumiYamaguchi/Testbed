#include "white/document.hpp"
#include "white/anvil_scene.hpp"
#include "white/cumulonimbus.hpp"
#include "white/centerline.hpp"
#include "white/developed_scene.hpp"
#include "white/top_lobe_scene.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace white {
Vec3 operator+(Vec3 a,Vec3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 operator-(Vec3 a,Vec3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 operator*(Vec3 a,double b) {return {a.x*b,a.y*b,a.z*b};}
double dot(Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
namespace {
Vec3 cross(Vec3 a,Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool bounded(Vec3 p) {return finite(p)&&std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})<=1e6;}
bool range(double v,double lo,double hi) {return std::isfinite(v)&&v>=lo&&v<=hi;}
bool radius(Vec3 p) {return range(p.x,1e-4,1e4)&&range(p.y,1e-4,1e4)&&range(p.z,1e-4,1e4);}
bool valid_transform(const Transform& t) {
    const auto q=t.rotation;
    const double n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return bounded(t.translation)&&radius(t.scale)&&std::isfinite(n)&&std::abs(n-1)<=1e-9;
}
bool valid_bounds(const Bounds& b) {
    return bounded(b.min)&&bounded(b.max)&&b.max.x>b.min.x&&b.max.y>b.min.y&&b.max.z>b.min.z;
}
Vec3 rotate(Quaternion q,Vec3 p) {
    const Vec3 v{q.x,q.y,q.z};
    return p+cross(v,p)*(2*q.w)+cross(v,cross(v,p))*2;
}
void validate_grid(const GridLayout& g) {
    if(!valid_bounds(g.local_bounds)||std::any_of(g.extent.begin(),g.extent.end(),[](auto n){return n==0||n>16384;}))
        throw std::invalid_argument("Invalid grid bounds or extent (1..16384)");
}
std::uint64_t mix(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
}
Vec3 local_to_world(const Transform& t,Vec3 p) {
    if(!valid_transform(t)||!finite(p)) throw std::invalid_argument("Invalid local/world transform");
    return rotate(t.rotation,{p.x*t.scale.x,p.y*t.scale.y,p.z*t.scale.z})+t.translation;
}
Vec3 world_to_local(const Transform& t,Vec3 p) {
    if(!valid_transform(t)||!finite(p)) throw std::invalid_argument("Invalid world/local transform");
    auto q=t.rotation;q.x=-q.x;q.y=-q.y;q.z=-q.z;
    const auto r=rotate(q,p-t.translation);return {r.x/t.scale.x,r.y/t.scale.y,r.z/t.scale.z};
}
Vec3 index_to_local(const GridLayout& g,Vec3 p) {
    validate_grid(g);if(!finite(p)) throw std::invalid_argument("Non-finite voxel index");
    const auto size=g.local_bounds.max-g.local_bounds.min;
    return g.local_bounds.min+Vec3{(p.x+0.5)*size.x/g.extent[0],(p.y+0.5)*size.y/g.extent[1],(p.z+0.5)*size.z/g.extent[2]};
}
Vec3 local_to_index(const GridLayout& g,Vec3 p) {
    validate_grid(g);if(!finite(p)) throw std::invalid_argument("Non-finite local position");
    const auto size=g.local_bounds.max-g.local_bounds.min;const auto r=p-g.local_bounds.min;
    return {r.x*g.extent[0]/size.x-0.5,r.y*g.extent[1]/size.y-0.5,r.z*g.extent[2]/size.z-0.5};
}
Dirty operator|(Dirty a,Dirty b) {return Dirty(std::uint32_t(a)|std::uint32_t(b));}
bool has(Dirty a,Dirty b) {return (std::uint32_t(a)&std::uint32_t(b))!=0;}
Dirty classify_change(const Scene& a,const Scene& b) {
    Dirty result=Dirty::none;
    auto ca=a.cloud,cb=b.cloud;ca.optics=cb.optics;
    bool developed_density_change=false;
    if(a.developed||b.developed) {
        auto da=a.developed,db=b.developed;
        auto clear_optics=[](auto& source){if(source){source->optics={};for(auto& cell:source->cells)cell.shape.source.modifiers.optics={};}};
        clear_optics(da);clear_optics(db);developed_density_change=da!=db;
    }
    bool top_density_change=false;
    if(a.top_lobes||b.top_lobes){auto ta=a.top_lobes,tb=b.top_lobes;
        auto clear=[](auto& source){if(source){source->trunk.optics={};for(auto& cell:source->trunk.cells)cell.shape.source.modifiers.optics={};}};
        clear(ta);clear(tb);top_density_change=ta!=tb;
    }
    bool anvil_density_change=false;
    if(a.anvil||b.anvil){auto aa=a.anvil,ab=b.anvil;auto clear=[](auto& source){if(source){source->cloud.trunk.optics={};for(auto& cell:source->cloud.trunk.cells)cell.shape.source.modifiers.optics={};}};clear(aa);clear(ab);anvil_density_change=aa!=ab;}
    if(ca!=cb || a.algorithm_version!=b.algorithm_version || developed_density_change || top_density_change || anvil_density_change) result=result|Dirty::density;
    if(a.cloud.optics!=b.cloud.optics||a.preview_approx!=b.preview_approx) result=result|Dirty::optics;
    if(a.sun!=b.sun) result=result|Dirty::sun;
    if(a.camera!=b.camera) result=result|Dirty::camera;
    if(a.exposure_ev!=b.exposure_ev) result=result|Dirty::display;
    return result;
}
std::vector<std::string> validate(const Scene& s) {
    std::vector<std::string> errors;
    auto check=[&](bool ok,const std::string& text){if(!ok)errors.push_back(text);};
    check(s.schema_version==9,"Unsupported scene schema_version");
    check(std::isfinite(s.preview_approx.strength)&&s.preview_approx.strength>=0&&s.preview_approx.strength<=1,"Preview approximation strength outside 0..1");
    check(s.algorithm_version==3,"Unsupported scene algorithm_version");
    check(unsigned(s.cumulonimbus.has_value())+unsigned(s.centerline.has_value())+unsigned(s.developed.has_value())+unsigned(s.top_lobes.has_value())+unsigned(s.anvil.has_value())<=1,
        "Scene cannot contain multiple source authorities");
    if(s.anvil){const auto source_errors=validate_anvil(*s.anvil);for(const auto& error:source_errors)errors.push_back("Anvil source: "+error);if(source_errors.empty())check(s.cloud==anvil_proxy_recipe(*s.anvil),"Anvil proxy differs from authoritative source");}
    if(s.top_lobes) {
        const auto source_errors=validate_top_lobes(*s.top_lobes);
        for(const auto& error:source_errors)errors.push_back("Top-lobe source: "+error);
        if(source_errors.empty())check(s.cloud==top_lobe_proxy_recipe(*s.top_lobes),"Top-lobe metadata proxy differs from authoritative source");
    }
    if(s.developed) {
        const auto source_errors=validate_developed_cloud(*s.developed);
        for(const auto& error:source_errors)errors.push_back("Developed source: "+error);
        if(source_errors.empty())check(s.cloud==developed_proxy_recipe(*s.developed),
            "Developed metadata proxy differs from authoritative source");
    }
    if(s.centerline) {
        const auto source_errors=validate_centerline(*s.centerline);
        for(const auto& error:source_errors)errors.push_back("Centerline source: "+error);
        if(source_errors.empty())check(s.cloud==lower_centerline_to_recipe(*s.centerline),
            "Centerline derived Recipe differs from authoritative source");
    }
    if(s.cumulonimbus) {
        const auto source_errors=validate_cumulonimbus(*s.cumulonimbus);
        for(const auto& error:source_errors)errors.push_back("Cumulonimbus source: "+error);
        if(source_errors.empty())check(s.cloud==derive_cumulonimbus_recipe(*s.cumulonimbus),
            "Cumulonimbus derived Recipe differs from authoritative source");
    }
    const auto& c=s.cloud;
    for(const auto& error:validate_altitude_density(c.altitude_density))errors.push_back("Altitude density: "+error);
    check(c.id!=0,"Cloud ID must be nonzero");
    check(valid_transform(c.transform),"Cloud transform requires finite translation, positive scale and unit quaternion");
    check(valid_bounds(c.envelope),"Envelope must have finite strictly ordered bounds");
    check(c.cells.size()<=8,"Cloud exceeds 8-cell limit");
    check(c.cuts.size()<=8,"Cloud exceeds 8-cut limit");
    std::set<Id> ids{c.id};
    // Size caps also bound validation work on untrusted decoded collections.
    for(size_t i=0;i<std::min<size_t>(c.cells.size(),8);++i) {
        const auto& cell=c.cells[i];
        check(cell.id!=0&&ids.insert(cell.id).second,"Cell ID must be nonzero and globally unique");
        check(bounded(cell.center)&&radius(cell.radii),"Cell center/radii invalid");
    }
    for(size_t i=0;i<std::min<size_t>(c.cuts.size(),8);++i) {
        const auto& cut=c.cuts[i];
        check(cut.id!=0&&ids.insert(cut.id).second,"Cut ID must be nonzero and globally unique");
        check(bounded(cut.center)&&radius(cut.radii)&&range(cut.transition,0,1e4),"Cut center/radii/transition invalid");
    }
    check(range(c.base.height,-1e6,1e6)&&range(c.base.transition,0,1e4),"Cloud base height/transition invalid");
    check(range(c.density,0,1000)&&range(c.blend_width,0,1e4)&&range(c.overlap,0,1),"Density/blend/overlap invalid");
    const auto& n=c.noise;
    check(bounded(n.origin)&&range(n.medium_frequency,0.0001,2)&&range(n.micro_frequency,0.0001,2)&&range(n.warp_frequency,0.0001,2),"Noise origin/frequencies invalid (0.0001 .. 2 cycles/metre)");
    check(range(n.medium_strength,0,1)&&range(n.micro_erosion,0,20)&&range(n.warp_amplitude,0,20),"Noise strength invalid (medium 0..1; erosion/warp 0..20 metres)");
    check(range(c.optics.extinction_scale,0,1000)&&range(c.optics.albedo,0,1)&&range(c.optics.g,-0.95,0.95),"Optical coefficients invalid");
    const auto& camera=s.camera;const auto forward=camera.target-camera.position;
    check(bounded(camera.position)&&bounded(camera.target)&&finite(camera.up)&&
        dot(forward,forward)>1e-12&&std::abs(dot(camera.up,camera.up)-1)<1e-9&&dot(cross(forward,camera.up),cross(forward,camera.up))>1e-12,
        "Camera basis is non-finite or degenerate");
    check(range(camera.vertical_fov_degrees,1,175)&&range(camera.near_plane,1e-4,1e6)&&range(camera.far_plane,1e-4,1e7)&&camera.far_plane>camera.near_plane,
        "Camera projection values invalid");
    check(finite(s.sun.direction_to_light)&&std::abs(dot(s.sun.direction_to_light,s.sun.direction_to_light)-1)<1e-9,"Sun direction must be unit length");
    check(range(s.sun.irradiance.x,0,1e6)&&range(s.sun.irradiance.y,0,1e6)&&range(s.sun.irradiance.z,0,1e6),"Sun irradiance invalid");
    check(range(s.exposure_ev,-32,32),"Exposure EV must be finite in [-32,32]");
    return errors;
}
void require_valid(const Scene& s) {
    const auto errors=validate(s);if(errors.empty())return;
    std::string message="Scene validation failed:";for(const auto& e:errors)message+="\n- "+e;
    throw std::invalid_argument(message);
}
Id next_id(const Scene& s) {
    require_valid(s);Id max=s.cloud.id;
    for(const auto& c:s.cloud.cells)max=std::max(max,c.id);
    for(const auto& c:s.cloud.cuts)max=std::max(max,c.id);
    if(max==std::numeric_limits<Id>::max())throw std::overflow_error("Stable ID namespace exhausted");
    return max+1;
}
std::uint64_t cell_random_key(const CloudRecipe& c,const Cell& cell) {return mix(c.structure_seed^mix(cell.id)^mix(cell.structure_seed));}
Document::Document(Scene scene) :scene_(std::move(scene)) {require_valid(scene_);}
bool Document::replace(Scene scene) {
    require_valid(scene);if(scene==scene_)return false;
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Document revision exhausted");
    const auto dirty=classify_change(scene_,scene);
    scene_=std::move(scene);last_change_=dirty;++revision_;changed_at_=std::chrono::steady_clock::now();return true;
}
}
