#include "white/developed_scene.hpp"
#include "white/persistence.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iomanip>
int main(int argc,char** argv){
 using clock=std::chrono::steady_clock;
 std::cout << std::fixed << std::setprecision(6);
 for(int a=1;a<argc;++a){
  const auto scene=white::read_scene(argv[a]);
  const bool expected=white::scene_density_requires_direct(scene);
  for(unsigned n : {1000u,2000u,4000u}){
   std::uint64_t sum=0;auto start=clock::now();
   for(unsigned i=0;i<n;++i)sum+=white::scene_density_requires_direct(scene);
   double ms=std::chrono::duration<double,std::milli>(clock::now()-start).count();
   if(sum!=(expected?n:0))return 2;
   std::cout<<"scene="<<argv[a]<<" calls="<<n<<" direct="<<expected<<" elapsed_ms="<<ms<<" per_call_us="<<ms*1000/n<<" extrapolated_300495_ms="<<ms/n*300495<<'\n';
  }
 }
}
