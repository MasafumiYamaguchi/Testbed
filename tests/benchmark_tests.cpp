#include "white/benchmark.hpp"
#include <iostream>
#include <limits>
#include <vector>
using namespace white;
int main(){int failed=0;auto check=[&](bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';++failed;}};
 std::vector<double> samples;for(int i=20;i>0;--i)samples.push_back(i);
 check(percentile(samples,.95)==19&&percentile(samples,.5)==10&&percentile(samples,1)==20,"nearest-rank percentile off by one");
 for(auto bad:std::vector<std::vector<double>>{{},{-1},{std::numeric_limits<double>::infinity()}}){bool rejected=false;try{percentile(bad,.95);}catch(...){rejected=true;}check(rejected,"invalid timing samples accepted");}
 Scene base;for(auto track:{EditTrack::density,EditTrack::camera,EditTrack::exposure}){
  auto previous=base;for(unsigned i=0;i<300;++i){auto next=benchmark_scene(base,track,i);check(next!=previous,"trajectory did not create an edit");auto changes=classify_change(base,next);if(track!=EditTrack::density)check(!has(changes,Dirty::density),"view-only trajectory modifies density");check(next.cloud.detail_seed==base.cloud.detail_seed,"trajectory changes random seed");previous=next;}
 }
 const FrameMeasurement row{7,1,2,3,4,7,160,90,true,false};
 check(benchmark_csv(std::span(&row,1)).find("0,7,1,2,3,4,7,160,90,1,0")!=std::string::npos,"CSV omitted or reordered measurements");
 check(benchmark_summary(std::span(&row,1)).find("cached_samples=1 direct_samples=0 hdr_updates=0")!=std::string::npos,"summary disguises density mode or reused HDR");
 return failed?1:0;
}
