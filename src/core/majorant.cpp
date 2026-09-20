#include "white/majorant.hpp"
#include "white/build_info.hpp"
#include "white/optics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace white {
namespace {size_t offset(std::array<unsigned,3> n,unsigned x,unsigned y,unsigned z){return (size_t(z)*n[1]+y)*n[0]+x;}}
Majorant build_majorant(const GridLayout& grid,std::span<const float> values){
    const auto n=grid.extent;if(values.size()!=checked_volume_bytes(n[0],n[1],n[2],1))throw std::invalid_argument("Majorant grid size mismatch");
    for(float v:values)if(!std::isfinite(v)||v<0)throw std::invalid_argument("Majorant requires finite nonnegative density");
    Majorant result{grid,{}};MajorantLevel level{{(n[0]+7)/8,(n[1]+7)/8,(n[2]+7)/8},{}};level.maxima.resize(size_t(level.extent[0])*level.extent[1]*level.extent[2]);
    // Spatial brick b spans [8b,8(b+1)] in edge coordinates. Linear
    // interpolation uses floor(edge-.5) and the next voxel: include [8b-1,8b+8].
    for(unsigned z=0;z<level.extent[2];++z)for(unsigned y=0;y<level.extent[1];++y)for(unsigned x=0;x<level.extent[0];++x){float m=0;
        for(int dz=-1;dz<=8;++dz)for(int dy=-1;dy<=8;++dy)for(int dx=-1;dx<=8;++dx){auto ix=unsigned(std::clamp(int(x*8)+dx,0,int(n[0])-1)),iy=unsigned(std::clamp(int(y*8)+dy,0,int(n[1])-1)),iz=unsigned(std::clamp(int(z*8)+dz,0,int(n[2])-1));m=std::max(m,values[offset(n,ix,iy,iz)]);}
        level.maxima[offset(level.extent,x,y,z)]=m;}
    result.levels.push_back(std::move(level));
    while(result.levels.back().maxima.size()>1){const auto& child=result.levels.back();MajorantLevel parent{{(child.extent[0]+1)/2,(child.extent[1]+1)/2,(child.extent[2]+1)/2},{}};parent.maxima.resize(size_t(parent.extent[0])*parent.extent[1]*parent.extent[2]);
        for(unsigned z=0;z<child.extent[2];++z)for(unsigned y=0;y<child.extent[1];++y)for(unsigned x=0;x<child.extent[0];++x){auto& m=parent.maxima[offset(parent.extent,x/2,y/2,z/2)];m=std::max(m,child.maxima[offset(child.extent,x,y,z)]);}
        result.levels.push_back(std::move(parent));
    }
    return result;
}
float majorant_at(const Majorant& m,Vec3 p){
    const auto& b=m.density_grid.local_bounds;if(p.x<b.min.x||p.y<b.min.y||p.z<b.min.z||p.x>b.max.x||p.y>b.max.y||p.z>b.max.z)return 0;
    const auto index=local_to_index(m.density_grid,p);const auto n=m.levels.front().extent;unsigned x=unsigned(std::clamp(std::floor((index.x+.5)/8),0.,double(n[0]-1))),y=unsigned(std::clamp(std::floor((index.y+.5)/8),0.,double(n[1]-1))),z=unsigned(std::clamp(std::floor((index.z+.5)/8),0.,double(n[2]-1)));return m.levels.front().maxima[offset(n,x,y,z)];
}
std::vector<BrickInterval> traverse_bricks(const GridLayout& grid,Vec3 origin,Vec3 direction,double begin,double end){
    if(!std::isfinite(begin)||!std::isfinite(end)||end<begin||!std::isfinite(dot(origin,origin))||!std::isfinite(dot(direction,direction))||dot(direction,direction)==0)throw std::invalid_argument("Invalid traversal ray");
    const auto hit=intersect_bounds(origin,direction,grid.local_bounds,begin,end);if(!hit)return {};
    std::vector<BrickInterval> result;double t=hit->entry;
    const double lo[]{grid.local_bounds.min.x,grid.local_bounds.min.y,grid.local_bounds.min.z},hi[]{grid.local_bounds.max.x,grid.local_bounds.max.y,grid.local_bounds.max.z},o[]{origin.x,origin.y,origin.z},d[]{direction.x,direction.y,direction.z};
    const auto n=grid.extent;const size_t limit=size_t(n[0])+n[1]+n[2]+4;
    std::array<unsigned,3> brick{};double crossing[3];
    auto update=[&](int a){if(d[a]==0){crossing[a]=std::numeric_limits<double>::infinity();return;}const double edge=double(d[a]>0?std::min((brick[a]+1)*8,n[a]):brick[a]*8);crossing[a]=(lo[a]+edge/n[a]*(hi[a]-lo[a])-o[a])/d[a];};
    for(int a=0;a<3;++a){double coordinate=(o[a]+d[a]*t-lo[a])/(hi[a]-lo[a])*n[a]/8;
        if(d[a]!=0)coordinate=std::nextafter(coordinate,d[a]>0?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity());
        brick[a]=unsigned(std::clamp(std::floor(coordinate),0.,double((n[a]+7)/8-1)));update(a);}
    while(t<hit->exit){const double next=std::min({hit->exit,crossing[0],crossing[1],crossing[2]});
        if(next>t)result.push_back({t,next,brick});
        if(result.size()>=limit)throw std::runtime_error("Traversal did not progress");
        if(next>=hit->exit)break;
        bool advanced=false;
        for(int a=0;a<3;++a)if(crossing[a]<=next){
            if(d[a]>0&&brick[a]+1<(n[a]+7)/8){++brick[a];update(a);advanced=true;}
            else if(d[a]<0&&brick[a]>0){--brick[a];update(a);advanced=true;}
            else crossing[a]=std::numeric_limits<double>::infinity();
        }
        if(!advanced&&!(next>t))throw std::runtime_error("Traversal did not progress");
        t=std::max(t,next);
    }
    return result;
}
}
