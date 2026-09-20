#include "white/hdr_export.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
white::HdrImage read(const char* name){std::ifstream f(name,std::ios::binary);if(!f)throw std::runtime_error("Missing EXR");std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});return white::decode_exr(bytes);}
int main(int argc,char** argv){try{if(argc!=3&&argc!=4)return 2;auto a=read(argv[1]),b=read(argv[2]);if(a.width!=b.width||a.height!=b.height)return 2;double max=0,sum=0,t=0;for(size_t i=0;i<a.rgba.size();++i){double d=std::abs(double(a.rgba[i])-b.rgba[i]);if(!std::isfinite(d))return 1;if(i%4==3)t=std::max(t,d);else{max=std::max(max,d);sum+=d;}}std::cout<<"linear_RGB_max="<<max<<" mean="<<sum/(a.width*a.height*3)<<" T_max="<<t<<'\n';if((argc==4&&max>2e-5)||t>1e-6)throw std::runtime_error("Shadow cache changed view transmittance");}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
