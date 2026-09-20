#include "white/hdr_export.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){try{if(argc!=3)return 2;std::ifstream f(argv[1],std::ios::binary);if(!f)throw std::runtime_error("Missing diagnostic EXR");std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});auto image=white::decode_exr(bytes);const float expected=std::stof(argv[2]);for(size_t i=0;i<image.rgba.size();++i)if(i%4!=3&&std::abs(image.rgba[i]-expected)>1e-6)throw std::runtime_error("Analytic empty-volume diagnostic mismatch");std::cout<<"Verified "<<image.width*image.height<<" pixels, expected raw RGB="<<expected<<'\n';}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
