#include "white/hdr_export.hpp"
#include "white/persistence.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#define require(ok) do {if(!(ok))throw std::runtime_error("HDR contract failed at line "+std::to_string(__LINE__));}while(false)
template<class F> void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed);}
int main(){try{
    white::HdrImage image{3,2,{0,1,2,0, 100,.25f,.5f,.5f, 1e-20f,0,10,1, 4,3,2,.1f, 9,8,7,.9f, 0,0,0,1}};
    const auto bytes=white::encode_exr(image);const auto roundtrip=white::decode_exr(bytes);
    require(roundtrip.width==3&&roundtrip.height==2&&roundtrip.rgba==image.rgba);
    auto root=std::filesystem::temp_directory_path()/("white-hdr-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        white::HdrMetadata meta;meta.scene.exposure_ev=1;meta.frame=99;meta.revision=9007199254740993ull;meta.view_steps=64;meta.shadow_steps=8;const auto before=white::scene_json(meta.scene);
        const auto output=root/"output";white::export_hdr(image,meta,output);
        std::ifstream exr(output/"linear.exr",std::ios::binary);std::vector<unsigned char> disk((std::istreambuf_iterator<char>(exr)),{});exr.close();require(disk==bytes);
        std::ifstream ppm(output/"display.ppm",std::ios::binary);std::string p((std::istreambuf_iterator<char>(ppm)),{});ppm.close();require(p.substr(0,11)=="P6\n3 2\n255\n");require(static_cast<unsigned char>(p[11])==0);require(static_cast<unsigned char>(p[12])==213);require(static_cast<unsigned char>(p[13])==231);require(p.size()==29);
        rejects([&]{white::export_hdr(image,meta,output);});
        rejects([&]{white::export_hdr(image,meta,root/"failure",white::ExportFault::before_publish);});require(!std::filesystem::exists(root/"failure"));
        auto invalid=image;invalid.rgba[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{white::export_hdr(invalid,meta,root/"invalid");});
        require(std::distance(std::filesystem::directory_iterator(root),std::filesystem::directory_iterator{})==1);
        require(before==white::scene_json(meta.scene));
        std::ifstream metadata(output/"metadata.json");std::string json((std::istreambuf_iterator<char>(metadata)),{});metadata.close();require(json.find("9007199254740993")!=std::string::npos&&json.find("transmittance")!=std::string::npos);
    }catch(...){std::filesystem::remove_all(root);throw;}
    std::filesystem::remove_all(root);std::cout<<"HDR FLOAT exact roundtrip: 0, >1, tiny values, RGBA, asymmetric rows; no-clobber/failure cleanup passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
