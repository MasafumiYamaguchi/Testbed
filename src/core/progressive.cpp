#include "white/progressive.hpp"
#include "white/persistence.hpp"
#include <cmath>
#include <stdexcept>
namespace white {
std::uint64_t preview_key(const Scene& input,std::span<const unsigned> settings){Scene scene=input;scene.exposure_ev=0;auto text=scene_json(scene);std::uint64_t hash=14695981039346656037ull;for(unsigned char c:text){hash^=c;hash*=1099511628211ull;}for(auto v:settings)for(int b=0;b<4;++b){hash^=(v>>(b*8))&255;hash*=1099511628211ull;}return hash;}
bool PreviewState::update(std::uint64_t key,bool interacting,double now){
    if(!std::isfinite(now))throw std::invalid_argument("Invalid preview clock");
    bool reset=!initialized_||key!=key_;if(reset||interacting)last_edit_ms_=now;
    const bool editing=interacting||now-last_edit_ms_<250;
    reset=reset||editing!=editing_;if(reset)samples_=0;
    key_=key;initialized_=true;editing_=editing;return reset;
}
std::array<float,4> accumulate_sample(std::array<float,4> mean,std::array<float,4> value,unsigned n){
    if(n>4096)throw std::invalid_argument("Preview sample limit");
    for(int c=0;c<4;++c){if(!std::isfinite(value[c])||(!std::isfinite(mean[c])&&n))throw std::invalid_argument("Invalid accumulation");mean[c]=n?mean[c]+(value[c]-mean[c])/float(n+1):value[c];}return mean;
}
}
