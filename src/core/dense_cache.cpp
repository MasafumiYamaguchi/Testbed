#include "white/dense_cache.hpp"
#include "white/build_info.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace white {
CacheBudget cache_budget(std::array<std::uint32_t,3> n,std::uint64_t old,std::uint64_t other,std::uint64_t limit){
    const auto bytes=checked_volume_bytes(n[0],n[1],n[2],4);
    const std::uint64_t pitch=(std::uint64_t(n[0])+63)/64*64;
    if(pitch>UINT32_MAX)throw std::overflow_error("Padded cache row overflow");
    const auto transfer=checked_volume_bytes(std::uint32_t(pitch),n[1],n[2],4);
    std::uint64_t total=0;for(auto size:{bytes,transfer,old,other}){if(size>limit-total)throw std::invalid_argument("Dense cache exceeds 256 MiB working-resource budget");total+=size;}
    return {bytes,transfer,total,bytes};
}
bool hard_density_region(const CloudRecipe& r,Vec3 p){
    const auto& b=r.envelope;if(p.x<=b.min.x||p.y<=b.min.y||p.z<=b.min.z||p.x>=b.max.x||p.y>=b.max.y||p.z>=b.max.z||(r.base.enabled&&p.y<=r.base.height))return false;
    for(const auto& c:r.cuts){Vec3 q=p-c.center;q={q.x/c.radii.x,q.y/c.radii.y,q.z/c.radii.z};if(dot(q,q)<=1)return false;}
    return true;
}
double sample_dense(const GridLayout& grid,std::span<const float> values,Vec3 p){
    const auto& b=grid.local_bounds;if(p.x<=b.min.x||p.y<=b.min.y||p.z<=b.min.z||p.x>=b.max.x||p.y>=b.max.y||p.z>=b.max.z)return 0;
    const auto count=checked_volume_bytes(grid.extent[0],grid.extent[1],grid.extent[2],1);if(values.size()!=count)throw std::invalid_argument("Dense cache reference size mismatch");
    const auto index=local_to_index(grid,p);const double v[]{index.x,index.y,index.z};int base[3];double f[3];
    for(int a=0;a<3;++a){const double c=std::clamp(v[a],0.0,double(grid.extent[a]-1));base[a]=int(std::floor(c));f[a]=c-base[a];}
    double result=0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        const auto ix=std::min(std::uint32_t(base[0]+x),grid.extent[0]-1),iy=std::min(std::uint32_t(base[1]+y),grid.extent[1]-1),iz=std::min(std::uint32_t(base[2]+z),grid.extent[2]-1);
        result+=values[(std::size_t(iz)*grid.extent[1]+iy)*grid.extent[0]+ix]*(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*(z?f[2]:1-f[2]);
    }
    return result;
}
}
