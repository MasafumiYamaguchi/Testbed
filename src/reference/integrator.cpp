#include "white/reference.hpp"
#include "white/developed_scene.hpp"
#include "white/ratio_tracking.hpp"
#include "white/density.hpp"
#include "white/dense_cache.hpp"
#include "white/editor_geometry.hpp"
#include "white/optics.hpp"
#include "white/phase.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace white {
namespace {
Vec3 normalized(Vec3 p){const double n=std::sqrt(dot(p,p));if(!(n>0)||!std::isfinite(n))throw std::invalid_argument("Invalid reference direction");return p*(1/n);}
void validate_settings(const ReferenceSettings& s){
    if(!s.width||!s.height||s.width>512||s.height>512||!s.samples||s.samples>65536||
       std::uint64_t(s.width)*s.height*s.samples>16777216||!s.bounce_limit||s.bounce_limit>1024||
       s.roulette_start>s.bounce_limit||!s.event_limit||s.event_limit>1000000||
       (s.mode!=ReferenceMode::single_scattering&&s.mode!=ReferenceMode::multiple_scattering))
        throw std::invalid_argument("Reference budget outside bounded small-scene limits");
}
std::optional<RayInterval> interval(const TrackingSnapshot& s,const TrackingRay& r){
    if(!std::isfinite(dot(r.direction_world,r.direction_world))||std::abs(dot(r.direction_world,r.direction_world)-1)>1e-9)
        throw std::invalid_argument("Reference ray needs a unit world direction");
    const auto o=world_to_local(s.scene().cloud.transform,r.origin_world);
    auto linear=s.scene().cloud.transform;linear.translation={};
    const auto d=world_to_local(linear,r.direction_world);
    return intersect_bounds(o,d,s.grid().local_bounds,r.begin,r.end);
}
TrackingRay medium_ray(const TrackingSnapshot& snapshot,Vec3 origin,Vec3 direction){
    // Camera clipping belongs only to the original camera segment. Secondary
    // transport and directional-light visibility must reach the medium exit.
    TrackingRay ray{origin,direction,0,std::numeric_limits<double>::max()};
    const auto hit=interval(snapshot,ray);ray.end=hit?hit->exit:0;return ray;
}
double density_world(const TrackingSnapshot& s,Vec3 p){return s.density_at_world(p);}
TrackingRngKey stream(TrackingRngKey k,unsigned bounce,unsigned id){k.bounce=bounce;k.stream=id;return k;}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&p.x>=0&&p.y>=0&&p.z>=0;}
}
ReferenceSample reference_sample(const TrackingSnapshot& snapshot,const TrackingRay& camera,const ReferenceSettings& settings,TrackingRngKey key){
    validate_settings(settings);
    const auto& scene=snapshot.scene();const auto& optics=scene.cloud.optics;
    const auto sunlight=normalized(scene.sun.direction_to_light);
    const TrackingOptions tracking{TrackingMode::local,settings.event_limit};
    ReferenceSample result;double beta=1;TrackingRay ray=camera;
    try {
        // This independent estimator is also the alpha channel. It is not
        // reused as the collision-path weight, which would count T twice.
        const auto primary=ratio_track(snapshot,camera,stream(key,0,10),tracking);
        result.transmittance=primary.transmittance;result.tracking_events+=primary.events;
        result.radiance=reference_background*result.transmittance;
        if(optics.albedo==0)return result;
        for(unsigned bounce=0;;++bounce){
            const auto collision=delta_track(snapshot,ray,stream(key,bounce,20),tracking);
            result.tracking_events+=collision.candidate_events;
            if(collision.event==TrackingEvent::escape)break;
            // The next collision proves a truncated contribution exists. An
            // escape after the last allowed bounce remains a complete sample.
            if(bounce>=settings.bounce_limit){result.status=ReferenceStatus::bounce_limit;break;}
            ++result.collision_events;
            beta*=optics.albedo; // sigma_s / sigma_t: weighted absorption.
            const auto shadow=medium_ray(snapshot,collision.position_world,sunlight);
            const auto visibility=ratio_track(snapshot,shadow,stream(key,bounce,30),tracking);
            result.tracking_events+=visibility.events;
            const auto backward=normalized(ray.direction_world);
            const double direct=beta*hg_phase(std::clamp(dot(backward,sunlight),-1.,1.),optics.g)*visibility.transmittance;
            result.radiance=result.radiance+scene.sun.irradiance*direct;
            if(settings.mode==ReferenceMode::single_scattering)break;
            if(bounce+1>=settings.roulette_start){
                const double survival=std::clamp(beta,.05,.95);
                if(tracking_uniform(stream(key,bounce,40),0,0)>=survival){++result.roulette_terminations;break;}
                beta/=survival;
            }
            // All directions below are backward (camera-to-scene) rays.
            // Reciprocity leaves their relative angle unchanged.
            const auto direction=sample_hg(backward,optics.g,
                tracking_uniform(stream(key,bounce,50),0,0),tracking_uniform(stream(key,bounce,50),0,1));
            beta*=hg_phase(std::clamp(dot(backward,direction.direction),-1.,1.),optics.g)/direction.pdf;
            ray=medium_ray(snapshot,collision.position_world,direction.direction);
        }
    }catch(const TrackingFailure& e){result.status=e.reason()==TrackingFailureReason::event_budget?ReferenceStatus::event_limit:ReferenceStatus::numerical_failure;}
    if(!finite(result.radiance)||!std::isfinite(result.transmittance)||result.transmittance<0||result.transmittance>1){result.status=ReferenceStatus::numerical_failure;result.radiance={};result.transmittance=0;}
    return result;
}
ReferenceRenderer::ReferenceRenderer(TrackingSnapshot snapshot,ReferenceSettings settings):snapshot_(std::move(snapshot)),settings_(settings){validate_settings(settings_);sum_.resize(size_t(settings.width)*settings.height*4);sum_square_.resize(size_t(settings.width)*settings.height);}
void ReferenceRenderer::advance(unsigned planes){
    if(planes>settings_.samples-completed_)throw std::invalid_argument("Reference advance exceeds requested sample count");
    for(unsigned step=0;step<planes;++step){const unsigned sample=completed_;
        for(unsigned y=0;y<settings_.height;++y)for(unsigned x=0;x<settings_.width;++x){
            const unsigned pixel=y*settings_.width+x;TrackingRngKey key{settings_.seed,pixel,sample,0,0};
            const double dx=settings_.pixel_jitter?tracking_uniform(key,0,0):.5,dy=settings_.pixel_jitter?tracking_uniform(key,0,1):.5;
            const auto c=camera_ray(snapshot_.scene().camera,(x+dx)/settings_.width,(y+dy)/settings_.height,double(settings_.width)/settings_.height);
            const TrackingRay ray{c.origin,c.direction,snapshot_.scene().camera.near_plane,snapshot_.scene().camera.far_plane};
            const auto result=reference_sample(snapshot_,ray,settings_,key);
            const size_t at=size_t(pixel)*4;sum_[at]+=result.radiance.x;sum_[at+1]+=result.radiance.y;sum_[at+2]+=result.radiance.z;sum_[at+3]+=result.transmittance;
            const double mean=(result.radiance.x+result.radiance.y+result.radiance.z)/3;sum_square_[pixel]+=mean*mean;
            ++statistics_.attempted_paths;
            switch(result.status){case ReferenceStatus::complete:++statistics_.complete_paths;break;case ReferenceStatus::event_limit:++statistics_.event_limited_paths;break;case ReferenceStatus::bounce_limit:++statistics_.bounce_limited_paths;break;case ReferenceStatus::numerical_failure:++statistics_.numerical_failures;break;}
            statistics_.collision_events+=result.collision_events;statistics_.tracking_events+=result.tracking_events;statistics_.roulette_terminations+=result.roulette_terminations;
        }
        ++completed_;
    }
}
HdrImage ReferenceRenderer::image()const{if(!completed_)throw std::logic_error("Reference image has no samples");HdrImage out{settings_.width,settings_.height,{}};out.rgba.resize(sum_.size());for(size_t i=0;i<sum_.size();++i)out.rgba[i]=float(sum_[i]/completed_);return out;}
double ReferenceRenderer::mean_radiance()const{if(!completed_)return 0;double total=0;for(size_t i=0;i<sum_.size();i+=4)total+=(sum_[i]+sum_[i+1]+sum_[i+2])/3;return total/(completed_*sum_square_.size());}
double ReferenceRenderer::mean_estimator_variance()const{if(completed_<2)return 0;double total=0;for(size_t p=0;p<sum_square_.size();++p){const double sum=(sum_[p*4]+sum_[p*4+1]+sum_[p*4+2])/3;total+=std::max(0.,(sum_square_[p]-sum*sum/completed_)/(completed_-1)/completed_);}return total/sum_square_.size();}
ReferenceSample reference_raymarch(const TrackingSnapshot& s,const TrackingRay& ray,unsigned view_steps,unsigned shadow_steps){
    if(!view_steps||view_steps>65536||!shadow_steps||shadow_steps>65536||std::uint64_t(view_steps)*shadow_steps>16777216)throw std::invalid_argument("Raymarch quadrature exceeds reference budget");
    ReferenceSample out;out.transmittance=1;const auto hit=interval(s,ray);const auto& scene=s.scene();const auto& optics=scene.cloud.optics;
    if(hit){const auto sun=normalized(scene.sun.direction_to_light);const double dt=(hit->exit-hit->entry)/view_steps;double L=0;
        for(unsigned i=0;i<view_steps;++i){const Vec3 p=ray.origin_world+ray.direction_world*(hit->entry+(i+.5)*dt);const double sigma=density_world(s,p)*optics.extinction_scale;if(sigma==0)continue;
            const auto shadow=medium_ray(s,p,sun);const auto light_hit=interval(s,shadow);double tau=0;
            if(light_hit){const double ds=(light_hit->exit-light_hit->entry)/shadow_steps;for(unsigned j=0;j<shadow_steps;++j)tau+=density_world(s,p+sun*(light_hit->entry+(j+.5)*ds))*optics.extinction_scale*ds;}
            const double source=optics.albedo*hg_phase(std::clamp(dot(normalized(ray.direction_world),sun),-1.,1.),optics.g)*std::exp(-tau);
            const auto segment=integrate_homogeneous(sigma,dt,1,source);L+=out.transmittance*segment.radiance;out.transmittance*=segment.transmittance;
        }
        out.radiance=scene.sun.irradiance*L;
    }
    out.radiance=out.radiance+reference_background*out.transmittance;return out;
}
TrackingSnapshot bake_reference_snapshot(const Scene& scene,unsigned resolution){
    if(resolution<2||resolution>128)throw std::invalid_argument("Reference grid resolution must be 2..128");
    require_valid(scene);
    if(scene_density_requires_direct(scene))
        throw std::invalid_argument("Reference bake does not support independent developments or active top lobes: independent hard-mask frozen-grid contract required");
    const GridLayout grid{scene.cloud.envelope,{resolution,resolution,resolution}};const DensityField field(scene.cloud);std::vector<float> density(size_t(resolution)*resolution*resolution);
    for(unsigned z=0;z<resolution;++z)for(unsigned y=0;y<resolution;++y)for(unsigned x=0;x<resolution;++x)density[(size_t(z)*resolution+y)*resolution+x]=float(field.at(index_to_local(grid,{double(x),double(y),double(z)})));
    return TrackingSnapshot(scene,grid,std::move(density));
}
}
