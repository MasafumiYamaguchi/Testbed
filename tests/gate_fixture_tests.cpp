#include "white/persistence.hpp"
#include "white/density.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected fixture directory");
    std::size_t count=0;
    for(const auto& file:std::filesystem::directory_iterator(argv[1])){
        const auto scene=white::read_scene(file.path());white::require_valid(scene);
        if(white::parse_scene_json(white::scene_json(scene))!=scene)throw std::runtime_error("Fixed fixture serialization changed scene");
        ++count;
    }
    if(count!=17)throw std::runtime_error("Expected seventeen fixed views");
    const auto a=white::read_scene(std::filesystem::path(argv[1])/"detail-17.white.json");
    auto b=white::read_scene(std::filesystem::path(argv[1])/"detail-18.white.json");
    if(a.cloud.detail_seed==b.cloud.detail_seed)throw std::runtime_error("Seed comparison did not vary seed");
    b.cloud.detail_seed=a.cloud.detail_seed;
    if(a!=b)throw std::runtime_error("Seed comparison changed other inputs");
    std::cout<<"17 fixed recipes validated; detail pair changes only detail seed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
