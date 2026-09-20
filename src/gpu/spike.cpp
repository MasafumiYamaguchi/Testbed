#include "white/gpu_spike.hpp"
#include "white/build_info.hpp"
#include "white/optics.hpp"
#include "white/dense_cache.hpp"
#include <chrono>
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
GpuSpike::GpuSpike()=default;
GpuSpike::~GpuSpike() {
    bake_worker_.reset();
    if (device) {
        SDL_WaitForGPUIdle(device);
        if(volume_pipeline)SDL_ReleaseGPUGraphicsPipeline(device,volume_pipeline);
        if(tonemap_pipeline)SDL_ReleaseGPUGraphicsPipeline(device,tonemap_pipeline);
        if(cache_sample_test)SDL_ReleaseGPUComputePipeline(device,cache_sample_test);
        if(optical_test)SDL_ReleaseGPUComputePipeline(device,optical_test);
        if(hdr)SDL_ReleaseGPUTexture(device,hdr);
        if(point_sampler)SDL_ReleaseGPUSampler(device,point_sampler);
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
    SDL_SetWindowMinimumSize(window,640,480);
    device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, true, "direct3d12");
    gpu_check(device != nullptr,"Create D3D12 device (no silent CPU fallback)");
    gpu_check(SDL_ClaimWindowForGPUDevice(device,window),"Claim GPU window"); claimed=true;
    std::cout << "backend=" << SDL_GetGPUDeviceDriver(device) << " SDL=" << SDL_GetVersion()
              << " requested_debug=true format=R32_FLOAT dimensions=17x19x23\n";
    auto make_compute=[&](const char* name, bool sampling) {
        const auto bytes=shader(name);
        SDL_GPUComputePipelineCreateInfo ci{};
        ci.code=bytes.data(); ci.code_size=bytes.size(); ci.entrypoint="main";
        ci.format=SDL_GPU_SHADERFORMAT_DXIL; ci.num_uniform_buffers=sampling?1:2;
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
    initialize_volume();
    bake_worker_=std::make_unique<GpuBakeWorker>(device,generate);
}
void GpuSpike::create_field(std::array<Uint32,3> dims, Uint32 kind) {
    if(kind>2)throw std::invalid_argument("Unknown field fixture");
    const auto old_bytes=field?checked_volume_bytes(extent[0],extent[1],extent[2],4):0;
    const auto budget=cache_budget(dims,old_bytes,std::uint64_t(width)*height*4+std::uint64_t(hdr_width)*hdr_height*16);
    const auto begin=std::chrono::steady_clock::now();
    auto* replacement=texture(device,SDL_GPU_TEXTURETYPE_3D,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
        SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_TEXTUREUSAGE_SAMPLER,dims[0],dims[1],dims[2]);
    try {
        const auto cloud=gpu_density_params(DensityField(scene_snapshot_.cloud));bake_wait_ms=0;
        // Bound each software-GPU dispatch; full volume still publishes atomically.
        for(Uint32 z=0;z<dims[2];z+=16) {
            auto* cmd=SDL_AcquireGPUCommandBuffer(device);gpu_check(cmd!=nullptr,"Acquire bake commands");
            const std::array<Uint32,8> params{dims[0],dims[1],dims[2],kind,z,0,0,0};SDL_PushGPUComputeUniformData(cmd,0,params.data(),sizeof(params));
            SDL_PushGPUComputeUniformData(cmd,1,&cloud,sizeof(cloud));
            SDL_GPUStorageTextureReadWriteBinding binding{};binding.texture=replacement;
            auto* pass=SDL_BeginGPUComputePass(cmd,&binding,1,nullptr,0);SDL_BindGPUComputePipeline(pass,generate);
            SDL_DispatchGPUCompute(pass,(dims[0]+3)/4,(dims[1]+3)/4,(std::min(16u,dims[2]-z)+3)/4);SDL_EndGPUComputePass(pass);
            const auto recorded=std::chrono::steady_clock::now();submit_wait(device,cmd);
            bake_wait_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-recorded).count();
        }
        bake_record_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()-bake_wait_ms;
    }catch(...){SDL_ReleaseGPUTexture(device,replacement);throw;}
    // Publish only a complete replacement. SDL defers release of used resources;
    // the bake fence has already completed. Failed preparation preserves old data.
    if(field)SDL_ReleaseGPUTexture(device,field);
    field=replacement;extent=dims;fixture=kind;field_density_hash_=density_input_hash(scene_snapshot_);volume_dirty=true;cache_reference_.clear();++bake_count;estimated_gpu_bytes=budget.peak_gpu_buffer_bytes;
    std::cout<<"bake="<<bake_count<<" extent="<<dims[0]<<'x'<<dims[1]<<'x'<<dims[2]<<" texture_bytes="<<budget.texture_bytes<<" peak_requested_gpu_bytes="<<estimated_gpu_bytes<<" cpu_reference_bytes="<<budget.cpu_reference_bytes<<" record_cpu_ms="<<bake_record_ms<<" submit_to_fence_wall_ms="<<bake_wait_ms<<" gpu_timestamp_ms=unavailable\n";
}
void GpuSpike::set_cache_resolution(int resolution) {
    if(resolution!=128&&resolution!=256)throw std::invalid_argument("Dense cache resolution must be 128 or 256");
    cache_resolution=resolution;queue_bake({Uint32(resolution),Uint32(resolution),Uint32(resolution)});
}
void GpuSpike::create_cloud(int preset) {
    scene_snapshot_=fixture_scene(preset);cloud_preset=preset;
    create_field({65,67,69},2);validate();
}
void GpuSpike::set_scene(const Scene& scene,std::uint64_t revision,std::chrono::steady_clock::time_point accepted) {
    require_valid(scene);const auto dirty=classify_change(scene_snapshot_,scene);
    const bool density_changed=has(dirty,Dirty::density)||fixture!=2;
    scene_snapshot_=scene;scene_revision=revision;exposure_ev=float(scene.exposure_ev);accepted_=accepted;
    const auto invalidate=invalidation(dirty);if(invalidate.hdr)volume_dirty=true;
    if(density_changed)try{const auto n=Uint32(cache_resolution);queue_bake(use_cache?std::array<Uint32,3>{n,n,n}:std::array<Uint32,3>{65,67,69});}
    catch(const std::exception& e){report=std::string("Bake failed; direct preview active: ")+e.what();}

}
void GpuSpike::queue_bake(std::array<Uint32,3> dims) {
    const std::uint64_t old=64ull*1024*1024; // reserve the largest supported published cache
    const auto other=std::uint64_t(width)*height*4+std::uint64_t(hdr_width)*hdr_height*16;
    (void)cache_budget(dims,old,other);
    bake_worker_->request({scene_revision,density_job_hash(scene_snapshot_,dims),scene_snapshot_,dims,old,other});
    report="Latest density queued; direct preview remains live";
}
void GpuSpike::poll_bakes() {
    if(!bake_worker_)return;
    if(auto ready=bake_worker_->take()) {
        if(density_input_hash(ready->source.scene)==density_input_hash(scene_snapshot_)) {
            if(field)SDL_ReleaseGPUTexture(device,field);
            field=ready->texture;extent=ready->source.extent;fixture=2;
            field_density_hash_=density_input_hash(ready->source.scene);cache_reference_.clear();++bake_count;
            bake_record_ms=ready->record_ms;bake_wait_ms=ready->wait_ms;estimated_gpu_bytes=ready->estimated_gpu_bytes;
            volume_dirty=true;report="Latest density ready";
            std::cout<<"density_publish producer_revision="<<ready->source.revision<<" consumer_revision="<<scene_revision<<" matching_density_hash="<<field_density_hash_<<" peak_requested_gpu_bytes="<<estimated_gpu_bytes<<'\n';
        }else SDL_ReleaseGPUTexture(device,ready->texture);
    }
    if(auto trace=bake_worker_->take_telemetry();!trace.empty())std::cout<<trace;
    if(auto error=bake_worker_->take_error();!error.empty())report="Bake failed; direct preview active: "+error;
}
void GpuSpike::set_test_delay(unsigned milliseconds){bake_worker_->set_test_delay(milliseconds);}
bool GpuSpike::cache_current()const{return field&&field_density_hash_==density_input_hash(scene_snapshot_);}
bool GpuSpike::bake_pending()const{return bake_worker_&&bake_worker_->busy();}
void GpuSpike::wait_bakes() {
    if(!bake_worker_)return;
    do{bake_worker_->wait_idle();poll_bakes();}while(bake_worker_->busy());
    if(field_density_hash_!=density_input_hash(scene_snapshot_))throw std::runtime_error("Latest density bake unavailable");
}
void GpuSpike::set_interacting(bool value){if(value!=interacting_){interacting_=value;volume_dirty=true;}}
void GpuSpike::note_present(std::uint64_t revision) {
    if(revision!=scene_revision||revision==last_present_revision_)return;
    last_present_revision_=revision;
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-accepted_).count();
    if(latency_samples_.size()==512)latency_samples_.erase(latency_samples_.begin());
    latency_samples_.push_back(elapsed);
    auto sorted=latency_samples_;std::sort(sorted.begin(),sorted.end());
    std::cout<<"edit_to_present_submission revision="<<revision<<" ms="<<elapsed<<" samples="<<sorted.size()<<" p50_ms="<<sorted[(sorted.size()-1)/2]<<" p95_ms="<<sorted[std::min(sorted.size()-1,(sorted.size()*95)/100)]<<'\n';
}
void GpuSpike::validate() {
    wait_bakes();
    if(validated_bake_==bake_count&&(!use_cache||!cache_reference_.empty())){std::cout<<"density_validation_reused bake="<<bake_count<<'\n';return;}
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
    max_error=0; interpolation_error=0; bool finite=true;
    if(fixture==2&&use_cache)cache_reference_.resize(size_t(extent[0])*extent[1]*extent[2]);
    const DensityField reference_field(scene_snapshot_.cloud);
    const GridLayout layout{reference_field.local_support(),extent};
    for(Uint32 z=0;z<extent[2];++z) for(Uint32 y=0;y<extent[1];++y) for(Uint32 x=0;x<extent[0];++x) {
        const float value=values[(z*extent[1]+y)*pitch+x];
        finite=finite && std::isfinite(value);
        if(fixture==2&&use_cache)cache_reference_[(size_t(z)*extent[1]+y)*extent[0]+x]=value;
        const float expected=fixture==2?float(reference_field.at(index_to_local(layout,{double(x),double(y),double(z)}))):reference(x,y,z,extent,fixture);
        max_error=std::max(max_error,std::abs(value-expected));
    }
    SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
    const auto& noise=scene_snapshot_.cloud.noise;
    const bool detailed=noise.medium_strength>0||noise.micro_erosion>0||noise.warp_amplitude>0;
    const float tolerance=fixture==2?float((detailed?1e-4:2e-5)*std::max(1.0,reference_field.maximum())):1e-6f;
    std::cout<<"density_reference max_abs_error="<<max_error<<" tolerance="<<tolerance<<" detailed="<<detailed<<'\n';
    if(!finite || max_error>tolerance) throw std::runtime_error("3D field readback differs from CPU fixture");
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
    validated_bake_=bake_count;
    report="PASS: all voxels / finite values / CPU reference";
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
    target=replacement; width=w; height=h;volume_dirty=true;
}
void GpuSpike::draw(SDL_GPUCommandBuffer* cmd,float slice,Uint32 axis) {
    rendered_revision=scene_revision;
    const bool lit=show_volume&&fixture==2;
    if(lit)render_volume(cmd);
    SDL_GPUColorTargetInfo color{}; color.texture=target; color.load_op=SDL_GPU_LOADOP_CLEAR; color.store_op=SDL_GPU_STOREOP_STORE;
    color.clear_color={0.02f,0.03f,0.055f,1};
    auto* pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr);
    SDL_BindGPUGraphicsPipeline(pass,lit?tonemap_pipeline:display);
    SDL_GPUTextureSamplerBinding input{lit?hdr:field,lit?point_sampler:sampler}; SDL_BindGPUFragmentSamplers(pass,0,&input,1);
    struct View {float slice; Uint32 axis,fixture; float padding;} view{slice,axis,fixture,0};
    if(lit) {const Float4 display_params{exposure_ev,0,0,0};SDL_PushGPUFragmentUniformData(cmd,0,&display_params,sizeof(display_params));}
    else SDL_PushGPUFragmentUniformData(cmd,0,&view,sizeof(view));
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
void GpuSpike::initialize_volume() {
    auto make_shader=[&](const char* name,SDL_GPUShaderStage stage,Uint32 samplers,Uint32 uniforms) {
        auto bytes=shader(name);SDL_GPUShaderCreateInfo ci{};
        ci.code=bytes.data();ci.code_size=bytes.size();ci.entrypoint="main";ci.format=SDL_GPU_SHADERFORMAT_DXIL;
        ci.stage=stage;ci.num_samplers=samplers;ci.num_uniform_buffers=uniforms;
        auto* result=SDL_CreateGPUShader(device,&ci);gpu_check(result!=nullptr,"Create volume shader");return result;
    };
    auto make_pipeline=[&](const char* name,bool tone) {
        auto* vs=make_shader("fullscreen.vert.hlsl.dxil",SDL_GPU_SHADERSTAGE_VERTEX,0,0);
        SDL_GPUShader* fs=nullptr;SDL_GPUGraphicsPipeline* p=nullptr;
        try {
            fs=make_shader(name,SDL_GPU_SHADERSTAGE_FRAGMENT,1,tone?1:2);
            SDL_GPUColorTargetDescription color{};color.format=tone?SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
            SDL_GPUGraphicsPipelineCreateInfo ci{};ci.vertex_shader=vs;ci.fragment_shader=fs;ci.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            ci.target_info.num_color_targets=1;ci.target_info.color_target_descriptions=&color;
            p=SDL_CreateGPUGraphicsPipeline(device,&ci);gpu_check(p!=nullptr,"Create volume/tonemap pipeline");
        }catch(...){SDL_ReleaseGPUShader(device,vs);if(fs)SDL_ReleaseGPUShader(device,fs);throw;}
        SDL_ReleaseGPUShader(device,vs);SDL_ReleaseGPUShader(device,fs);return p;
    };
    volume_pipeline=make_pipeline("volume.frag.hlsl.dxil",false);
    tonemap_pipeline=make_pipeline("tonemap.frag.hlsl.dxil",true);
    SDL_GPUSamplerCreateInfo sci{};sci.min_filter=sci.mag_filter=SDL_GPU_FILTER_NEAREST;
    sci.address_mode_u=sci.address_mode_v=sci.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    point_sampler=SDL_CreateGPUSampler(device,&sci);gpu_check(point_sampler!=nullptr,"Create HDR point sampler");
    auto bytes=shader("optics.comp.hlsl.dxil");SDL_GPUComputePipelineCreateInfo ci{};
    ci.code=bytes.data();ci.code_size=bytes.size();ci.entrypoint="main";ci.format=SDL_GPU_SHADERFORMAT_DXIL;
    ci.num_readwrite_storage_buffers=1;ci.threadcount_x=ci.threadcount_y=ci.threadcount_z=1;
    optical_test=SDL_CreateGPUComputePipeline(device,&ci);gpu_check(optical_test!=nullptr,"Create optical test pipeline");
    bytes=shader("cache_samples.comp.hlsl.dxil");ci.code=bytes.data();ci.code_size=bytes.size();ci.num_uniform_buffers=2;ci.num_samplers=1;
    cache_sample_test=SDL_CreateGPUComputePipeline(device,&ci);gpu_check(cache_sample_test!=nullptr,"Create cache sample test pipeline");
}
void GpuSpike::render_volume(SDL_GPUCommandBuffer* cmd) {
    if(!volume_dirty)return;
    if(view_steps<8||view_steps>512||shadow_steps<1||shadow_steps>64||internal_width<64||internal_width>640)
        throw std::invalid_argument("Invalid rendering quality budget");
    rendered_steps_=interacting_?std::min(view_steps,32):view_steps;
    rendered_cache_=use_cache&&field_density_hash_==density_input_hash(scene_snapshot_);
    const int render_width=interacting_?std::min(internal_width,96):internal_width;
    const int render_shadows=interacting_?std::min(shadow_steps,4):shadow_steps;
    const Uint32 w=Uint32(render_width),h=Uint32(std::max(1.0f,float(w)*std::max(1.0f,float(height)-90)/std::max(1.0f,float(width)-310)));
    if(checked_volume_bytes(w,h,1,16)>16*1024*1024)throw std::invalid_argument("HDR target exceeds 16 MiB budget");
    if(!hdr||hdr_width!=w||hdr_height!=h) {
        auto* replacement=texture(device,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER,w,h,1);
        gpu_check(SDL_WaitForGPUIdle(device),"Wait before HDR resize");
        if(hdr)SDL_ReleaseGPUTexture(device,hdr);
        hdr=replacement;hdr_width=w;hdr_height=h;
    }
    auto normalize=[](Vec3 p){return p*(1/std::sqrt(dot(p,p)));};
    auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto& recipe=scene_snapshot_.cloud;const auto field_params=gpu_density_params(DensityField(recipe));
    const auto& camera=scene_snapshot_.camera;const auto eye=camera.position,target_point=camera.target;
    const auto forward=normalize(target_point-eye),right=normalize(cross(forward,camera.up)),up=cross(right,forward);
    const auto origin=world_to_local(recipe.transform,{0,0,0});
    const auto x=world_to_local(recipe.transform,{1,0,0})-origin,y=world_to_local(recipe.transform,{0,1,0})-origin,z=world_to_local(recipe.transform,{0,0,1})-origin;
    const auto sun=scene_snapshot_.sun.direction_to_light;
    auto pack=[](Vec3 v,float w=0){return Float4{float(v.x),float(v.y),float(v.z),w};};
    const std::array<Float4,10> view{
        pack(eye,float(camera.near_plane)),pack(right,float(std::tan(camera.vertical_fov_degrees*3.141592653589793/360))),pack(up),pack(forward,float(recipe.optics.extinction_scale)),
        pack(sun,float(recipe.optics.albedo)),pack(scene_snapshot_.sun.irradiance,float(camera.far_plane)),
        Float4{float(x.x),float(y.x),float(z.x),float(origin.x)},Float4{float(x.y),float(y.y),float(z.y),float(origin.y)},Float4{float(x.z),float(y.z),float(z.z),float(origin.z)},
        Float4{float(rendered_steps_),float(render_shadows),float(w)/float(h),rendered_cache_?1.0f:0.0f}};
    SDL_PushGPUFragmentUniformData(cmd,0,view.data(),sizeof(view));
    SDL_PushGPUFragmentUniformData(cmd,1,&field_params,sizeof(field_params));
    SDL_GPUColorTargetInfo color{};color.texture=hdr;color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    auto* pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr);SDL_BindGPUGraphicsPipeline(pass,volume_pipeline);
    const SDL_GPUTextureSamplerBinding cache{field,sampler};SDL_BindGPUFragmentSamplers(pass,0,&cache,1);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);volume_dirty=false;
}
void GpuSpike::validate_cache_samples() {
    SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,256*4*sizeof(float),0};
    auto* output=SDL_CreateGPUBuffer(device,&bi);gpu_check(output!=nullptr,"Create cache sample buffer");
    try{
        Transfer transfer(device,bi.size);auto* cmd=SDL_AcquireGPUCommandBuffer(device);gpu_check(cmd!=nullptr,"Acquire cache sample commands");
        const auto cloud=gpu_density_params(DensityField(scene_snapshot_.cloud));SDL_PushGPUComputeUniformData(cmd,1,&cloud,sizeof(cloud));
        const Float4 unused{};SDL_PushGPUComputeUniformData(cmd,0,&unused,sizeof(unused));
        SDL_GPUStorageBufferReadWriteBinding binding{};binding.buffer=output;
        auto* pass=SDL_BeginGPUComputePass(cmd,nullptr,0,&binding,1);SDL_BindGPUComputePipeline(pass,cache_sample_test);
        const SDL_GPUTextureSamplerBinding input{field,sampler};SDL_BindGPUComputeSamplers(pass,0,&input,1);
        SDL_DispatchGPUCompute(pass,256,1,1);SDL_EndGPUComputePass(pass);
        auto* copy=SDL_BeginGPUCopyPass(cmd);const SDL_GPUBufferRegion source{output,0,bi.size};const SDL_GPUTransferBufferLocation dest{transfer.buffer,0};
        SDL_DownloadFromGPUBuffer(copy,&source,&dest);SDL_EndGPUCopyPass(copy);submit_wait(device,cmd);
        const auto* values=static_cast<float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));gpu_check(values!=nullptr,"Map cache samples");
        double maximum=0,sum=0;bool finite=true;for(int i=0;i<256;++i){for(int c=0;c<4;++c)finite=finite&&std::isfinite(values[i*4+c]);double delta=std::abs(double(values[i*4+2])-values[i*4]);maximum=std::max(maximum,delta);sum+=delta;}
        const bool protected_base=!scene_snapshot_.cloud.base.enabled||values[2]==0;
        const bool protected_cut=scene_snapshot_.cloud.cuts.empty()||values[6]==0;
        std::cout<<"dense_sample_comparison samples=256 max_abs="<<maximum<<" mean_abs="<<sum/256<<" raw_base_leak="<<values[1]<<" raw_cut_leak="<<values[5]<<" hard_constraints_preserved="<<(protected_base&&protected_cut)<<'\n';
        SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
        if(!finite||!protected_base||!protected_cut)throw std::runtime_error("Cache interpolation violated hard constraints");
    }catch(...){SDL_ReleaseGPUBuffer(device,output);throw;}SDL_ReleaseGPUBuffer(device,output);
}
void GpuSpike::validate_optics() {
    SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,14*4*sizeof(float),0};
    auto* output=SDL_CreateGPUBuffer(device,&bi);gpu_check(output!=nullptr,"Create optical result buffer");
    try {
        Transfer transfer(device,bi.size);auto* cmd=SDL_AcquireGPUCommandBuffer(device);gpu_check(cmd!=nullptr,"Acquire optical test commands");
        SDL_GPUStorageBufferReadWriteBinding binding{};binding.buffer=output;
        auto* pass=SDL_BeginGPUComputePass(cmd,nullptr,0,&binding,1);SDL_BindGPUComputePipeline(pass,optical_test);
        SDL_DispatchGPUCompute(pass,14,1,1);SDL_EndGPUComputePass(pass);
        auto* copy=SDL_BeginGPUCopyPass(cmd);SDL_GPUBufferRegion source{output,0,bi.size};SDL_GPUTransferBufferLocation destination{transfer.buffer,0};
        SDL_DownloadFromGPUBuffer(copy,&source,&destination);SDL_EndGPUCopyPass(copy);submit_wait(device,cmd);
        auto* values=static_cast<float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));gpu_check(values!=nullptr,"Map optical test");
        const double sigma[]={0,0.01,0.1,1,10,0.0000001,0.25,2},distance[]={100,20,15,2,10,100,8,0};
        double error=0;bool finite=true;
        for(int i=0;i<8;++i){double T=std::exp(-sigma[i]*distance[i]);for(int j=0;j<4;++j){finite=finite&&std::isfinite(values[i*4+j]);error=std::max(error,std::abs(double(values[i*4+j])-(j==0?T:0.5*(1-T))));}}
        const std::array<Float4,6> bounds_expected{Float4{1,2,4,1},Float4{1,0,1,1},Float4{0,0,0,1},Float4{1,2,4,1},Float4{1,2,4,1},Float4{1,float(2*std::sqrt(3)),float(4*std::sqrt(3)),1}};
        double bounds_error=0;
        for(int i=0;i<6;++i){const float expected[]{bounds_expected[i].x,bounds_expected[i].y,bounds_expected[i].z,bounds_expected[i].w};for(int j=0;j<4;++j){finite=finite&&std::isfinite(values[(i+8)*4+j]);bounds_error=std::max(bounds_error,std::abs(double(values[(i+8)*4+j])-expected[j]));}}
        SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
        std::cout<<"GPU ray/box six cases max_abs_error="<<bounds_error<<'\n';
        if(!finite||bounds_error>1e-4)throw std::runtime_error("GPU ray/box regression failed");
        std::cout<<"GPU homogeneous optics max_abs_error="<<error<<" tolerance=0.001\n";
        if(!finite||error>1e-3)throw std::runtime_error("GPU optical integration failed analytic comparison");
    }catch(...){SDL_ReleaseGPUBuffer(device,output);throw;}
    SDL_ReleaseGPUBuffer(device,output);
}
std::vector<float> GpuSpike::read_hdr() {
    if(!hdr)throw std::runtime_error("HDR target unavailable");
    const Uint32 pitch=(hdr_width+15)/16*16;Transfer transfer(device,Uint32(checked_volume_bytes(pitch,hdr_height,1,16)));
    auto* cmd=SDL_AcquireGPUCommandBuffer(device);gpu_check(cmd!=nullptr,"Acquire HDR readback");
    auto* copy=SDL_BeginGPUCopyPass(cmd);SDL_GPUTextureRegion source{};source.texture=hdr;source.w=hdr_width;source.h=hdr_height;source.d=1;
    SDL_GPUTextureTransferInfo dest{transfer.buffer,0,pitch,hdr_height};SDL_DownloadFromGPUTexture(copy,&source,&dest);SDL_EndGPUCopyPass(copy);submit_wait(device,cmd);
    std::vector<float> result(size_t(hdr_width)*hdr_height*4);bool valid=true;float maximum=0;
    const auto* values=static_cast<const float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));gpu_check(values!=nullptr,"Map HDR readback");
    for(Uint32 y=0;y<hdr_height;++y)for(Uint32 x=0;x<hdr_width;++x)for(Uint32 c=0;c<4;++c){float value=values[(y*pitch+x)*4+c];result[(y*hdr_width+x)*4+c]=value;valid=valid&&std::isfinite(value)&&value>=0&&(c!=3||value<=1);maximum=std::max(maximum,value);}
    SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
    if(!valid)throw std::runtime_error("HDR contains NaN/Inf/negative radiance or invalid transmittance");
    // Independently integrate the CPU field along every pixel ray. This catches
    // blank images, wrong camera uniforms and mismatched volume coordinates,
    // which finite-value and isolated shader tests cannot detect.
    if(rendered_cache_&&cache_reference_.empty()) {
        // Validation entry points drain jobs before the frame. Never change the
        // texture identity after the HDR image that is being compared.
        if(bake_pending())throw std::runtime_error("Drain bakes before cached HDR validation");
        validate();
    }
    const GridLayout grid{scene_snapshot_.cloud.envelope,extent};
    const DensityField cpu(scene_snapshot_.cloud);const auto& recipe=cpu.recipe();
    const auto& camera=scene_snapshot_.camera;const auto eye=camera.position,target_point=camera.target;
    auto normalize=[](Vec3 v){return v*(1/std::sqrt(dot(v,v)));};
    auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto forward=normalize(target_point-eye),right=normalize(cross(forward,camera.up)),up=cross(right,forward);
    const auto origin=world_to_local(recipe.transform,eye);
    double error=0,min_t=1,direct_error=0,direct_sum=0;
    for(Uint32 y=0;y<hdr_height;++y)for(Uint32 x=0;x<hdr_width;++x) {
        const double px=(2*(x+0.5)/hdr_width-1)*double(hdr_width)/hdr_height,py=1-2*(y+0.5)/hdr_height;
        const auto direction=normalize(forward+(right*px+up*py)*std::tan(camera.vertical_fov_degrees*3.141592653589793/360));
        const auto local_direction=world_to_local(recipe.transform,eye+direction)-origin;
        double tau=0,direct_tau=0;
        if(const auto interval=intersect_bounds(origin,local_direction,recipe.envelope,camera.near_plane,camera.far_plane)) {
            const double dt=(interval->exit-interval->entry)/rendered_steps_;
            for(int i=0;i<rendered_steps_;++i){const auto p=origin+local_direction*(interval->entry+(i+0.5)*dt);const double direct=cpu.at(p);
                const double cached=rendered_cache_?(hard_density_region(recipe,p)?sample_dense(grid,cache_reference_,p):0):direct;
                tau+=cached*recipe.optics.extinction_scale*dt;direct_tau+=direct*recipe.optics.extinction_scale*dt;}
        }
        const double expected=std::exp(-tau),actual=result[(y*hdr_width+x)*4+3];
        error=std::max(error,std::abs(actual-expected));min_t=std::min(min_t,actual);
        const double difference=std::abs(actual-std::exp(-direct_tau));direct_error=std::max(direct_error,difference);direct_sum+=difference;
    }
    std::cout<<"HDR verified "<<hdr_width<<'x'<<hdr_height<<" view_steps="<<view_steps<<" shadow_steps="<<shadow_steps<<" max_channel="<<maximum<<" min_T="<<min_t<<" CPU_T_max_error="<<error<<'\n';
    if(rendered_cache_)std::cout<<"dense_vs_direct_T max="<<direct_error<<" mean="<<direct_sum/(hdr_width*hdr_height)<<" sampler_reference_error="<<error<<" resolution="<<cache_resolution<<'\n';
    if(error>(rendered_cache_?0.005:0.001))throw std::runtime_error("Rendered transmittance disagrees with CPU field/camera reference");
    return result;
}
}
