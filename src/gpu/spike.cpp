#include "white/gpu_spike.hpp"
#include "white/build_info.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace white {
void gpu_check(bool success, const char* operation) {
    if (!success) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
namespace {
struct Transfer {
    SDL_GPUDevice* device;
    SDL_GPUTransferBuffer* buffer;
    Transfer(SDL_GPUDevice* d, Uint32 size) : device(d) {
        SDL_GPUTransferBufferCreateInfo ci{};
        ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; ci.size = size;
        buffer = SDL_CreateGPUTransferBuffer(d, &ci);
        gpu_check(buffer != nullptr, "Create download buffer");
    }
    ~Transfer() { SDL_ReleaseGPUTransferBuffer(device, buffer); }
};
void submit_wait(SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd) {
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    gpu_check(fence != nullptr, "Submit and acquire fence");
    const bool result = SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    gpu_check(result, "Wait for readback fence");
}
SDL_GPUTexture* texture(SDL_GPUDevice* device, SDL_GPUTextureType type,
    SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage,
    Uint32 x, Uint32 y, Uint32 z) {
    gpu_check(SDL_GPUTextureSupportsFormat(device, format, type, usage), "Required texture format/usage unsupported");
    SDL_GPUTextureCreateInfo ci{};
    ci.type = type; ci.format = format; ci.usage = usage;
    ci.width=x; ci.height=y; ci.layer_count_or_depth=z; ci.num_levels=1;
    auto* result = SDL_CreateGPUTexture(device, &ci);
    gpu_check(result != nullptr, "Create texture"); return result;
}
float reference(Uint32 x, Uint32 y, Uint32 z, std::array<Uint32,3> dims, Uint32 kind) {
    return kind == 0 ? (float(x) + 2*float(y) + 4*float(z))/256.0f
        : (x == dims[0]/2 && y == dims[1]/2 && z == dims[2]/2 ? 1.0f : 0.0f);
}
}
GpuSpike::~GpuSpike() {
    if (device) {
        SDL_WaitForGPUIdle(device);
        if (display) SDL_ReleaseGPUGraphicsPipeline(device, display);
        if (sample) SDL_ReleaseGPUComputePipeline(device, sample);
        if (generate) SDL_ReleaseGPUComputePipeline(device, generate);
        if (sampler) SDL_ReleaseGPUSampler(device, sampler);
        if (target) SDL_ReleaseGPUTexture(device, target);
        if (field) SDL_ReleaseGPUTexture(device, field);
        if (claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
    }
    if (window) SDL_DestroyWindow(window);
}
std::vector<Uint8> GpuSpike::shader(const char* name) {
    const auto path = std::filesystem::path(SDL_GetBasePath()) / "shaders" / name;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Missing compiled shader: " + path.string());
    const auto size = in.tellg();
    if (size <= 0 || size > 16*1024*1024) throw std::runtime_error("Invalid shader size");
    std::vector<Uint8> bytes(static_cast<std::size_t>(size));
    in.seekg(0); in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) throw std::runtime_error("Incomplete shader read");
    return bytes;
}
void GpuSpike::initialize() {
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_INFO);
    window=SDL_CreateWindow("ProjectWhite | GPU field laboratory",900,600,SDL_WINDOW_RESIZABLE);
    gpu_check(window != nullptr,"Create window");
    SDL_SetWindowPosition(window,40,40);
    device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, true, "direct3d12");
    gpu_check(device != nullptr,"Create D3D12 device (no silent CPU fallback)");
    gpu_check(SDL_ClaimWindowForGPUDevice(device,window),"Claim GPU window"); claimed=true;
    std::cout << "backend=" << SDL_GetGPUDeviceDriver(device) << " SDL=" << SDL_GetVersion()
              << " requested_debug=true format=R32_FLOAT dimensions=17x19x23\n";
    auto make_compute=[&](const char* name, bool sampling) {
        const auto bytes=shader(name);
        SDL_GPUComputePipelineCreateInfo ci{};
        ci.code=bytes.data(); ci.code_size=bytes.size(); ci.entrypoint="main";
        ci.format=SDL_GPU_SHADERFORMAT_DXIL; ci.num_uniform_buffers=1;
        ci.num_readwrite_storage_textures=sampling?0:1;
        ci.num_readwrite_storage_buffers=sampling?1:0;
        ci.num_samplers=sampling?1:0;
        ci.threadcount_x=ci.threadcount_y=ci.threadcount_z=sampling?1:4;
        auto* p=SDL_CreateGPUComputePipeline(device,&ci);
        gpu_check(p != nullptr,"Create compute pipeline"); return p;
    };
    generate=make_compute("field.comp.hlsl.dxil",false);
    sample=make_compute("sample.comp.hlsl.dxil",true);
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter=sci.mag_filter=SDL_GPU_FILTER_LINEAR;
    sci.address_mode_u=sci.address_mode_v=sci.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler=SDL_CreateGPUSampler(device,&sci); gpu_check(sampler != nullptr,"Create linear sampler");
    auto make_shader=[&](const char* name, SDL_GPUShaderStage stage) {
        const auto bytes=shader(name);
        SDL_GPUShaderCreateInfo ci{};
        ci.code=bytes.data(); ci.code_size=bytes.size(); ci.entrypoint="main"; ci.format=SDL_GPU_SHADERFORMAT_DXIL;
        ci.stage=stage; ci.num_samplers=stage==SDL_GPU_SHADERSTAGE_FRAGMENT?1:0;
        ci.num_uniform_buffers=stage==SDL_GPU_SHADERSTAGE_FRAGMENT?1:0;
        auto* s=SDL_CreateGPUShader(device,&ci); gpu_check(s != nullptr,"Create display shader"); return s;
    };
    auto* vs=make_shader("fullscreen.vert.hlsl.dxil",SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader* fs=nullptr;
    try {
        fs=make_shader("slice.frag.hlsl.dxil",SDL_GPU_SHADERSTAGE_FRAGMENT);
        SDL_GPUColorTargetDescription color{}; color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo ci{}; ci.vertex_shader=vs; ci.fragment_shader=fs;
        ci.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        ci.target_info.num_color_targets=1; ci.target_info.color_target_descriptions=&color;
        display=SDL_CreateGPUGraphicsPipeline(device,&ci);
        gpu_check(display != nullptr,"Create display pipeline");
    } catch (...) { SDL_ReleaseGPUShader(device,vs); if(fs) SDL_ReleaseGPUShader(device,fs); throw; }
    SDL_ReleaseGPUShader(device,vs); SDL_ReleaseGPUShader(device,fs);
}
void GpuSpike::create_field(std::array<Uint32,3> dims, Uint32 kind) {
    if (kind>1) throw std::invalid_argument("Unknown field fixture");
    if (checked_volume_bytes(dims[0],dims[1],dims[2],4)>64*1024*1024)
        throw std::invalid_argument("GPU spike field exceeds 64 MiB budget");
    auto* replacement=texture(device,SDL_GPU_TEXTURETYPE_3D,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
        SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_TEXTUREUSAGE_SAMPLER,dims[0],dims[1],dims[2]);
    SDL_WaitForGPUIdle(device);
    if(field) SDL_ReleaseGPUTexture(device,field);
    field=replacement; extent=dims; fixture=kind;
    auto* cmd=SDL_AcquireGPUCommandBuffer(device); gpu_check(cmd != nullptr,"Acquire generation command buffer");
    const std::array<Uint32,4> params{dims[0],dims[1],dims[2],kind};
    SDL_PushGPUComputeUniformData(cmd,0,params.data(),sizeof(params));
    SDL_GPUStorageTextureReadWriteBinding binding{}; binding.texture=field;
    auto* pass=SDL_BeginGPUComputePass(cmd,&binding,1,nullptr,0);
    SDL_BindGPUComputePipeline(pass,generate);
    SDL_DispatchGPUCompute(pass,(dims[0]+3)/4,(dims[1]+3)/4,(dims[2]+3)/4);
    SDL_EndGPUComputePass(pass); submit_wait(device,cmd);
}
void GpuSpike::validate() {
    // D3D12 texture row pitch is 256 bytes; explicitly pad the 17-wide fixture.
    const Uint32 pitch=(extent[0]+63)/64*64;
    const auto count=checked_volume_bytes(pitch,extent[1],extent[2],4);
    if(count>std::numeric_limits<Uint32>::max()) throw std::overflow_error("Readback exceeds SDL transfer size");
    Transfer transfer(device,static_cast<Uint32>(count));
    auto* cmd=SDL_AcquireGPUCommandBuffer(device); gpu_check(cmd != nullptr,"Acquire readback command buffer");
    auto* copy=SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region{}; region.texture=field; region.w=extent[0]; region.h=extent[1]; region.d=extent[2];
    SDL_GPUTextureTransferInfo dest{transfer.buffer,0,pitch,extent[1]};
    SDL_DownloadFromGPUTexture(copy,&region,&dest); SDL_EndGPUCopyPass(copy); submit_wait(device,cmd);
    auto* values=static_cast<float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));
    gpu_check(values != nullptr,"Map density readback");
    max_error=0; bool finite=true;
    for(Uint32 z=0;z<extent[2];++z) for(Uint32 y=0;y<extent[1];++y) for(Uint32 x=0;x<extent[0];++x) {
        const float value=values[(z*extent[1]+y)*pitch+x];
        finite=finite && std::isfinite(value);
        max_error=std::max(max_error,std::abs(value-reference(x,y,z,extent,fixture)));
    }
    SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
    if(!finite || max_error>1e-6f) throw std::runtime_error("3D field readback differs from CPU fixture");
    if(fixture==0) {
        SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,5*sizeof(float),0};
        SDL_GPUBuffer* result=SDL_CreateGPUBuffer(device,&bi); gpu_check(result != nullptr,"Create sampling results");
        try {
            Transfer samples(device,5*sizeof(float));
            cmd=SDL_AcquireGPUCommandBuffer(device); gpu_check(cmd != nullptr,"Acquire sampler validation commands");
            const std::array<Uint32,4> params{extent[0],extent[1],extent[2],0};
            SDL_PushGPUComputeUniformData(cmd,0,params.data(),sizeof(params));
            SDL_GPUStorageBufferReadWriteBinding output{}; output.buffer=result;
            auto* pass=SDL_BeginGPUComputePass(cmd,nullptr,0,&output,1);
            SDL_BindGPUComputePipeline(pass,sample);
            SDL_GPUTextureSamplerBinding input{field,sampler}; SDL_BindGPUComputeSamplers(pass,0,&input,1);
            SDL_DispatchGPUCompute(pass,5,1,1); SDL_EndGPUComputePass(pass);
            copy=SDL_BeginGPUCopyPass(cmd);
            SDL_GPUBufferRegion source{result,0,5*sizeof(float)};
            SDL_GPUTransferBufferLocation destination{samples.buffer,0};
            SDL_DownloadFromGPUBuffer(copy,&source,&destination); SDL_EndGPUCopyPass(copy); submit_wait(device,cmd);
            const auto* values=static_cast<const float*>(SDL_MapGPUTransferBuffer(device,samples.buffer,false));
            gpu_check(values != nullptr,"Map sampling results");
            const float top=reference(extent[0]-1,extent[1]-1,extent[2]-1,extent,0);
            const float interior=((extent[0]-1)*0.27f+2*(extent[1]-1)*0.43f+4*(extent[2]-1)*0.61f)/256;
            const std::array<float,5> expected{0,top,interior,0,top};
            interpolation_error=0; finite=true;
            for(size_t i=0;i<5;++i) { finite=finite&&std::isfinite(values[i]); interpolation_error=std::max(interpolation_error,std::abs(values[i]-expected[i])); }
            SDL_UnmapGPUTransferBuffer(device,samples.buffer);
            // Hardware interpolation may quantize fractions. Bound against one
            // 1/256-texel step times the ramp's sum of gradients (7/256).
            if(!finite || interpolation_error>7.0f/65536.0f+1e-6f) throw std::runtime_error("Linear R32F sampling failed");
        } catch(...) { SDL_ReleaseGPUBuffer(device,result); throw; }
        SDL_ReleaseGPUBuffer(device,result);
    }
    report="PASS: all voxels / finite values / axis and pitch";
    std::cout << "fixture=" << fixture << " extent=" << extent[0] << 'x' << extent[1] << 'x' << extent[2]
              << " row_bytes=" << pitch*4 << " max_abs_error=" << max_error
              << " interpolation_error=" << interpolation_error << " PASS\n";
}
void GpuSpike::resize(Uint32 w, Uint32 h) {
    if(w==width && h==height) return;
    if(checked_volume_bytes(w,h,1,4)>64*1024*1024) throw std::invalid_argument("Render target exceeds 64 MiB spike budget");
    auto* replacement=texture(device,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER,w,h,1);
    gpu_check(SDL_WaitForGPUIdle(device),"Wait before resize");
    if(target) SDL_ReleaseGPUTexture(device,target);
    target=replacement; width=w; height=h;
}
void GpuSpike::draw(SDL_GPUCommandBuffer* cmd,float slice,Uint32 axis) {
    SDL_GPUColorTargetInfo color{}; color.texture=target; color.load_op=SDL_GPU_LOADOP_CLEAR; color.store_op=SDL_GPU_STOREOP_STORE;
    color.clear_color={0.02f,0.03f,0.055f,1};
    auto* pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr);
    SDL_BindGPUGraphicsPipeline(pass,display);
    SDL_GPUTextureSamplerBinding input{field,sampler}; SDL_BindGPUFragmentSamplers(pass,0,&input,1);
    struct View {float slice; Uint32 axis,fixture; float padding;} view{slice,axis,fixture,0};
    SDL_PushGPUFragmentUniformData(cmd,0,&view,sizeof(view));
    // Leave room for controls on the left; the remaining rectangle is a slice.
    SDL_GPUViewport viewport{290,60,std::max(1.0f,float(width)-310),std::max(1.0f,float(height)-90),0,1};
    SDL_SetGPUViewport(pass,&viewport); SDL_DrawGPUPrimitives(pass,3,1,0,0); SDL_EndGPURenderPass(pass);
}
void GpuSpike::save_capture(const std::filesystem::path& path) {
    const Uint32 pitch=(width+63)/64*64;
    Transfer transfer(device,static_cast<Uint32>(checked_volume_bytes(pitch,height,1,4)));
    auto* cmd=SDL_AcquireGPUCommandBuffer(device); gpu_check(cmd != nullptr,"Acquire capture commands");
    auto* copy=SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion source{}; source.texture=target; source.w=width; source.h=height; source.d=1;
    SDL_GPUTextureTransferInfo dest{transfer.buffer,0,pitch,height};
    SDL_DownloadFromGPUTexture(copy,&source,&dest); SDL_EndGPUCopyPass(copy); submit_wait(device,cmd);
    void* pixels=SDL_MapGPUTransferBuffer(device,transfer.buffer,false); gpu_check(pixels != nullptr,"Map capture");
    SDL_Surface* surface=SDL_CreateSurfaceFrom(int(width),int(height),SDL_PIXELFORMAT_RGBA32,pixels,int(pitch*4));
    bool saved=false;
    if(surface) { saved=SDL_SaveBMP(surface,path.string().c_str()); SDL_DestroySurface(surface); }
    SDL_UnmapGPUTransferBuffer(device,transfer.buffer); gpu_check(saved,"Save GPU framebuffer screenshot");
}
}
