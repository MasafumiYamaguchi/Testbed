#include "white/progressive.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool ok){if(!ok)throw std::runtime_error("Progressive contract");}
int main(){try{
    white::Scene s;const unsigned settings[]{160,90,64,8,128,0,0};auto key=white::preview_key(s,settings);auto changed=s;changed.exposure_ev=2;check(white::preview_key(changed,settings)==key);
    changed=s;changed.camera.position.x+=1;check(white::preview_key(changed,settings)!=key);changed=s;changed.cloud.optics.g=.5;check(white::preview_key(changed,settings)!=key);
    white::PreviewState state;check(state.update(key,false,0));check(state.editing());check(!state.update(key,false,249));check(state.update(key,false,250));check(!state.editing());state.complete_sample();check(state.samples()==1);
    check(!state.update(key,false,251));check(state.samples()==1);check(state.update(key,true,252));check(state.samples()==0);check(!state.update(key,false,490));check(state.editing());check(state.update(key,false,502));state.complete_sample();
    check(state.update(key+1,false,503));check(state.samples()==0&&state.editing());
    std::array<float,4> mean{};for(unsigned i=0;i<64;++i)mean=white::accumulate_sample(mean,{float(i),100.f,0.f,.5f},i);check(std::abs(mean[0]-31.5f)<1e-6&&mean[1]==100&&mean[2]==0&&mean[3]==.5f);
    std::cout<<"Progressive reset, 250ms hysteresis, exposure reuse and linear HDR/T average passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
