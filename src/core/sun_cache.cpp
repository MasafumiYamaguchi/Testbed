#include "white/sun_cache.hpp"
#include "white/revision_queue.hpp"
#include "white/optics.hpp"
#include <bit>
#include <stdexcept>
namespace white {
std::uint64_t sun_cache_key(const Scene& scene,std::array<std::uint32_t,3> extent,unsigned resolution,unsigned steps){
    auto h=density_job_hash(scene,extent);auto add=[&](std::uint64_t v){for(int b=0;b<8;++b){h^=(v>>(b*8))&255;h*=1099511628211ull;}};
    for(double v:{scene.sun.direction_to_light.x,scene.sun.direction_to_light.y,scene.sun.direction_to_light.z,scene.cloud.optics.extinction_scale,scene.camera.far_plane})add(std::bit_cast<std::uint64_t>(v));
    add(resolution);add(steps);return h;
}
double sun_optical_depth(const Scene& scene,const GridLayout& grid,std::span<const float> density,Vec3 origin,unsigned steps){
    if(!steps||steps>512)throw std::invalid_argument("Invalid sun steps");
    const auto& r=scene.cloud;const auto direction=world_to_local(r.transform,scene.sun.direction_to_light)-world_to_local(r.transform,{0,0,0});
    const auto interval=intersect_bounds(origin,direction,r.envelope,0,scene.camera.far_plane);if(!interval)return 0;
    const double dt=(interval->exit-interval->entry)/steps;double tau=0;
    for(unsigned i=0;i<steps;++i){const auto q=origin+direction*(interval->entry+(i+.5)*dt);if(hard_density_region(r,q))tau+=sample_dense(grid,density,q)*r.optics.extinction_scale*dt;}
    return tau;
}
}
