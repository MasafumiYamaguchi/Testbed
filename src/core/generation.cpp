#include "white/generation.hpp"
#include "white/revision_queue.hpp"
#include "white/anvil_scene.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>

namespace white {
namespace {
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool range(double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
double smooth(double t){return t*t*(3-2*t);}
Scene prepared(Scene scene){
    require_valid(scene);
    if(!editable_developed_source(scene))scene=new_developed_scene(std::move(scene));
    return scene;
}
DevelopedCloud& trunk(Scene& s){return s.anvil?s.anvil->cloud.trunk:s.top_lobes?s.top_lobes->trunk:*s.developed;}
const DevelopmentGrowth* rule(const GenerationSettings& s,Id id){
    const auto i=std::find_if(s.cells.begin(),s.cells.end(),[&](const auto& c){return c.cell_id==id;});
    return i==s.cells.end()?nullptr:&*i;
}
void require(const std::vector<std::string>& errors){if(errors.empty())return;std::string message="Invalid generation input:";for(const auto& e:errors)message+="\n- "+e;throw std::invalid_argument(message);}
Vec3 sample_wind_unchecked(const GenerationSettings& s,double y){
    const double t=std::clamp((y-s.wind_base)/s.wind_height,0.,1.);
    auto after=std::upper_bound(s.wind.begin(),s.wind.end(),t,[](double v,const auto& k){return v<k.altitude;});
    if(after==s.wind.begin())return after->displacement;
    if(after==s.wind.end())return s.wind.back().displacement;
    const auto& a=*(after-1);const auto& b=*after;const double u=(t-a.altitude)/(b.altitude-a.altitude);
    return a.displacement*(1-u)+b.displacement*u;
}
std::vector<std::string> settings_errors(const GenerationSettings& s){
    std::vector<std::string> errors;auto check=[&](bool good,const char* message){if(!good)errors.emplace_back(message);};
    check(s.algorithm_version==generation_algorithm_version,"Unsupported generation algorithm version");
    check(range(s.stage,0,1),"Development stage is dimensionless and must be in 0..1");
    check(range(s.initial_height_fraction,.25,1),"Initial height fraction must be in .25..1");
    check(range(s.wind_base,-100000,100000)&&range(s.wind_height,10,10000),"Wind reference needs a finite base and 10..10000 m height");
    check(s.wind.size()>=2&&s.wind.size()<=max_wind_knots,"Wind profile requires 2..6 altitude knots");
    if(s.wind.size()>=2&&s.wind.size()<=max_wind_knots){
        check(s.wind.front().altitude==0&&s.wind.back().altitude==1,"Wind endpoints must be at normalized reference heights 0 and 1");
        for(std::size_t i=0;i<s.wind.size();++i){const auto& k=s.wind[i];
            check(range(k.altitude,0,1),"Wind altitude must be finite in 0..1");
            check(finite(k.displacement)&&k.displacement.y==0&&std::hypot(k.displacement.x,k.displacement.z)<=500,"Wind displacement must be horizontal and at most 500 m per stage");
            if(i)check(k.altitude-s.wind[i-1].altitude>=.01,"Wind knots must be ordered with at least .01 separation");
        }
    }
    check(finite(s.reference_translation)&&s.reference_translation.y==0&&std::hypot(s.reference_translation.x,s.reference_translation.z)<=500,"Reference translation must be horizontal and at most 500 object-local metres");
    check(s.cells.size()<=max_developed_cells,"Too many development growth rules");
    std::set<Id> ids;
    for(const auto& c:s.cells){
        check(c.cell_id!=0&&ids.insert(c.cell_id).second,"Growth rules require unique stable development IDs");
        check(range(c.start_stage,0,.8)&&range(c.amount,0,1),"Cell onset must be 0..0.8 and growth amount 0..1");
        check(c.pinned_controls.size()<=max_centerline_points,"Too many pinned guide controls");
        std::set<Id> controls;for(auto id:c.pinned_controls)check(id!=0&&controls.insert(id).second,"Pinned control IDs must be unique and nonzero");
    }
    return errors;
}
struct Cancelled{};
}
GenerationSettings default_generation_settings(const Scene& input){
    auto scene=prepared(input);const auto& source=trunk(scene);GenerationSettings settings;
    if(!source.cells.empty()){
        settings.wind_base=source.cells.front().shape.source.parameters.cloud_base+source.cells.front().translation.y;
        settings.wind_height=source.cells.front().shape.source.parameters.height;
        for(const auto& c:source.cells){
            DevelopmentGrowth growth;growth.cell_id=c.id;
            for(const auto& p:c.shape.points)if(p.offset.x!=0||p.offset.z!=0)growth.pinned_controls.push_back(p.id);
            settings.cells.push_back(growth);
        }
    }
    require(validate_generation(input,settings));return settings;
}
std::vector<std::string> validate_generation(const Scene& input,const GenerationSettings& settings){
    auto errors=settings_errors(settings);
    try{
        require_valid(input);if(!settings.enabled)return errors;
        auto scene=prepared(input);const auto& source=trunk(scene);
        for(const auto& growth:settings.cells){
            const auto found=std::find_if(source.cells.begin(),source.cells.end(),[&](const auto& c){return c.id==growth.cell_id;});
            if(found==source.cells.end()){errors.emplace_back("Growth rule refers to an unknown development ID");continue;}
            for(auto id:growth.pinned_controls)if(std::none_of(found->shape.points.begin(),found->shape.points.end(),[&](const auto& p){return p.id==id;}))errors.emplace_back("Pinned guide refers to an unknown control ID");
        }
    }catch(const std::exception& e){errors.emplace_back(e.what());}
    return errors;
}
Vec3 sample_generation_wind(const GenerationSettings& s,double y){require(settings_errors(s));if(!std::isfinite(y))throw std::invalid_argument("Nonfinite wind sample altitude");return sample_wind_unchecked(s,y);}
double development_stage(const GenerationSettings& s,Id id){
    const auto* r=rule(s,id);return std::clamp((s.stage-(r?r->start_stage:0))/(1-(r?r->start_stage:0)),0.,1.)*(r?r->amount:1);
}
std::uint64_t generation_input_hash(const Scene& scene,const GenerationSettings& settings){
    require(validate_generation(scene,settings));std::uint64_t hash=density_input_hash(scene);
    auto add=[&](std::uint64_t v){for(int i=0;i<8;++i){hash^=(v>>(8*i))&255;hash*=UINT64_C(1099511628211);}};
    auto real=[&](double v){add(std::bit_cast<std::uint64_t>(v==0?0.:v));};
    add(settings.algorithm_version);add(settings.enabled);real(settings.stage);real(settings.initial_height_fraction);real(settings.wind_base);real(settings.wind_height);
    add(settings.wind.size());for(const auto& k:settings.wind){real(k.altitude);real(k.displacement.x);real(k.displacement.z);}
    real(settings.reference_translation.x);real(settings.reference_translation.z);
    auto cells=settings.cells;std::sort(cells.begin(),cells.end(),[](const auto& a,const auto& b){return a.cell_id<b.cell_id;});add(cells.size());
    for(auto& c:cells){add(c.cell_id);real(c.start_stage);real(c.amount);std::sort(c.pinned_controls.begin(),c.pinned_controls.end());add(c.pinned_controls.size());for(auto id:c.pinned_controls)add(id);}
    return hash;
}
GenerationOutcome generate_cloud_state(const Scene& initial,const GenerationSettings& settings,std::stop_token stop,const std::function<void(double)>& progress){
    const auto begin=std::chrono::steady_clock::now();
    auto checkpoint=[&](double value){if(stop.stop_requested())throw Cancelled{};if(progress)progress(value);if(stop.stop_requested())throw Cancelled{};};
    try{
        checkpoint(0);require(validate_generation(initial,settings));
        GenerationCandidate candidate{initial,settings,initial,generation_input_hash(initial,settings),0};
        if(settings.enabled){
            auto scene=prepared(initial);auto& source=trunk(scene);
            for(std::size_t index=0;index<source.cells.size();++index){
                auto& cell=source.cells[index];const auto original=cell.shape;const auto& original_params=original.source.parameters;auto& params=cell.shape.source.parameters;
                const double stage=development_stage(settings,cell.id),growth=smooth(stage);
                params.height=std::max(10.,original_params.height*(settings.initial_height_fraction+(1-settings.initial_height_fraction)*growth));
                const auto* policy=rule(settings,cell.id);
                for(auto& point:cell.shape.points){
                    const bool pinned=policy&&std::find(policy->pinned_controls.begin(),policy->pinned_controls.end(),point.id)!=policy->pinned_controls.end();
                    if(pinned){
                        const auto guide=sample_centerline(original,point.t).position;
                        point.offset.x=guide.x-params.height*point.t*params.growth_direction.x/params.growth_direction.y;
                        point.offset.z=guide.z-params.height*point.t*params.growth_direction.z/params.growth_direction.y;
                    }else{
                        const double altitude=params.cloud_base+params.height*point.t+cell.translation.y;
                        point.offset=point.offset+sample_wind_unchecked(settings,altitude)*(stage*point.t*point.t);
                    }
                }
                // Different vertical and radial laws: early columns are shorter
                // with a narrower top, not a uniformly scaled mature cloud.
                for(auto& knot:cell.shape.profile){
                    knot.radius_scale*=1-.35*(1-growth)*(.25+.75*knot.t);
                    knot.density_scale*=.6+.4*growth;
                }
                require(validate_centerline(cell.shape));
                checkpoint(.15+.5*double(index+1)/double(std::max<std::size_t>(1,source.cells.size())));
            }
            // The bulk translation is distinct from base-anchored relative wind.
            const auto drift=settings.reference_translation*settings.stage;
            source.transform.translation=local_to_world(source.transform,drift);
            if(editable_top_lobe_source(scene)){
                auto& top=scene.anvil?scene.anvil->cloud:*scene.top_lobes;const double stage=development_stage(settings,top.target_cell);
                top.settings.parent_radius*=.5+.5*smooth(stage);
                const auto& cell=*std::find_if(source.cells.begin(),source.cells.end(),[&](const auto& c){return c.id==top.target_cell;});
                const auto tangent=sample_centerline(cell.shape,std::min(.98,top.settings.top_start+.22)).tangent;
                auto direction=top.settings.growth_direction+Vec3{tangent.x/tangent.y,0,tangent.z/tangent.y};
                const double length=std::sqrt(dot(direction,direction));top.settings.growth_direction=direction*(1/length);
                if(scene.anvil){
                    // Use the already bent curve as the attachment. Wind only
                    // sets orientation/relative extension here, never a second
                    // bulk displacement of the neck.
                    const auto& original_cell=*std::find_if(editable_developed_source(initial)->cells.begin(),editable_developed_source(initial)->cells.end(),[&](const auto& c){return c.id==top.target_cell;});
                    const auto& before=original_cell.shape.source.parameters;const auto& after=cell.shape.source.parameters;
                    const auto original=initial.anvil->settings;auto& anvil=scene.anvil->settings;
                    const double ratio=after.height/before.height,age=smooth(std::clamp((stage-.35)/.65,0.,1.));
                    anvil.start_height=after.cloud_base+(original.start_height-before.cloud_base)*ratio;
                    anvil.thickness=std::max(1.,original.thickness*ratio);
                    // A very shallow initial shape cannot accommodate a 1 m
                    // sheet at its old normalized height. Reject, never clamp
                    // its start through a protected lower region.
                    anvil.width=std::max(8.,original.width*(.6+.4*age));
                    anvil.width=std::max(anvil.width,2*anvil.thickness);
                    anvil.extension=original.extension*age;
                    anvil.edge_fade=std::min(original.edge_fade,anvil.thickness*.125);
                    anvil.density_scale=original.density_scale*age;
                    const double altitude=anvil.start_height+anvil.thickness*.5+cell.translation.y;
                    const auto wind=sample_wind_unchecked(settings,altitude);const double speed=std::hypot(wind.x,wind.z);
                    if(anvil.follow_wind&&speed>1e-9){
                        anvil.direction=wind*(1/speed);
                        anvil.extension=std::min(anvil.width*4,anvil.extension+speed*stage*age*.35);
                        const auto gradient=(sample_wind_unchecked(settings,altitude+anvil.thickness*.5)-sample_wind_unchecked(settings,altitude-anvil.thickness*.5))*(stage/anvil.thickness);
                        anvil.shear=std::clamp(original.shear+dot(gradient,anvil.direction),-4.,4.);
                    }
                    refresh_anvil_scene(scene);
                }else refresh_top_lobe_scene(scene);
            }else refresh_developed_scene(scene);
            checkpoint(.8);candidate.evaluated=std::move(scene);
        }
        require_valid(candidate.evaluated);checkpoint(1);
        candidate.elapsed_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        return {GenerationStatus::completed,std::move(candidate),"Selected state generated"};
    }catch(const Cancelled&){return {GenerationStatus::cancelled,{},"Generation cancelled; previous state preserved"};}
    catch(const std::exception& e){return {GenerationStatus::failed,{},e.what()};}
}
}
