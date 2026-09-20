#pragma once
#include "white/document.hpp"
#include <array>
#include <span>
namespace white {
std::uint64_t preview_key(const Scene&,std::span<const unsigned> settings);
class PreviewState {
public:
    bool update(std::uint64_t key,bool interacting,double now_ms);
    void complete_sample(){if(!editing_)++samples_;}
    unsigned samples()const{return samples_;}
    bool editing()const{return editing_;}
private:
    bool initialized_=false,editing_=true;
    std::uint64_t key_=0;
    double last_edit_ms_=0;
    unsigned samples_=0;
};
std::array<float,4> accumulate_sample(std::array<float,4> mean,std::array<float,4> value,unsigned previous_samples);
}
