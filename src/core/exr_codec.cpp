#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>
#include "white/hdr_export.hpp"
#include <memory>
#include <stdexcept>
namespace white {
std::vector<unsigned char> encode_exr(const HdrImage& image) {
    unsigned char* data=nullptr;const char* error=nullptr;
    int size=SaveEXRToMemory(image.rgba.data(),int(image.width),int(image.height),4,0,&data,&error);
    std::unique_ptr<unsigned char,decltype(&std::free)> owned(data,&std::free);
    if(size<=0){std::string message=error?error:"EXR encode failed";if(error)FreeEXRErrorMessage(error);throw std::runtime_error(message);}
    return {data,data+size};
}
HdrImage decode_exr(std::span<const unsigned char> bytes) {
    EXRVersion version{};EXRHeader header;InitEXRHeader(&header);EXRImage image;InitEXRImage(&image);const char* error=nullptr;
    auto check=[&](int status){if(status!=TINYEXR_SUCCESS){std::string message=error?error:"EXR decode failed";if(error)FreeEXRErrorMessage(error);FreeEXRImage(&image);FreeEXRHeader(&header);throw std::runtime_error(message);}};
    check(ParseEXRVersionFromMemory(&version,bytes.data(),bytes.size()));
    check(ParseEXRHeaderFromMemory(&header,&version,bytes.data(),bytes.size(),&error));
    for(int i=0;i<header.num_channels;++i)header.requested_pixel_types[i]=TINYEXR_PIXELTYPE_FLOAT;
    check(LoadEXRImageFromMemory(&image,&header,bytes.data(),bytes.size(),&error));
    HdrImage result{unsigned(image.width),unsigned(image.height),std::vector<float>(size_t(image.width)*image.height*4)};
    for(int c=0;c<4;++c){const char* name=c==0?"R":c==1?"G":c==2?"B":"A";int channel=-1;for(int j=0;j<header.num_channels;++j)if(std::strcmp(header.channels[j].name,name)==0)channel=j;
        if(channel<0){FreeEXRImage(&image);FreeEXRHeader(&header);throw std::runtime_error("Missing RGBA channel");}
        const auto* values=reinterpret_cast<float*>(image.images[channel]);for(size_t i=0;i<result.rgba.size()/4;++i)result.rgba[i*4+c]=values[i];}
    FreeEXRImage(&image);FreeEXRHeader(&header);return result;
}
}
