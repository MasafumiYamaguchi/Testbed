#include "white/developed_scene.hpp"
#include "white/gpu_spike.hpp"
#include "white/benchmark.hpp"
#include "white/phase.hpp"
#include "white/sun_cache.hpp"
#include "white/majorant.hpp"
#include "white/diagnostics.hpp"
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
struct DensityAllowance {bool detailed=false;double profile_bound=0,profile_tolerance=0,anvil_bound=0,anvil_tolerance=0,finish_tolerance=0;};
DensityAllowance density_allowance(const Scene& scene){
    bool detailed=false;double profile_bound=0,profile_tolerance=0;
    auto include_profile=[&](const CloudRecipe& recipe,double translation_y){
        const auto& noise=recipe.noise;
        detailed=detailed||noise.medium_strength>0||noise.micro_erosion>0||noise.warp_amplitude>0;
        double bound=altitude_density_error_bound(recipe.altitude_density);
        if(translation_y!=0){auto shifted=recipe.altitude_density;shifted.base+=translation_y;
            bound=2*(bound+altitude_density_error_bound(shifted));}
        const double unmodulated_max=recipe.cells.empty()?0:recipe.density*(1+recipe.overlap*double(recipe.cells.size()-1));
        profile_bound=std::max(profile_bound,bound);profile_tolerance+=bound*unmodulated_max;
    };
    const auto* grouped=editable_developed_source(scene);
    if(scene.frozen){
        for(const auto& field:scene.frozen->fields)include_profile(frozen_effective_recipe(field),field.translation.y);
        if(scene.frozen->top_enabled){const auto& top=scene.frozen->fields[1].recipe;
            const AltitudeDensityProfile ramp{true,top.base.height,top.base.transition,{{0,0},{1,1}}};
            const double bound=1.5*altitude_density_error_bound(ramp);profile_bound=std::max(profile_bound,bound);profile_tolerance+=bound*top.density;
        }
    }else if(scene_has_active_top_lobes(scene)){
        const auto& source=*editable_top_lobe_source(scene);const DevelopedEvaluationPlan trunk(source.trunk);
        include_profile(trunk.fields()[0].recipe(),trunk.cloud().cells[0].translation.y);
        // The top group uses object-local coordinates; its inherited profile
        // and mask are shifted once when lowered, matching its GPU packet.
        const auto& cell=source.trunk.cells[0];auto top=lower_centerline_to_recipe(cell.shape);
        top.density*=source.settings.density_scale;top.overlap=0;
        if(top.altitude_density.enabled)top.altitude_density.base+=cell.translation.y;
        include_profile(top,0);
        const AltitudeDensityProfile ramp{true,top_lobe_mask_height(source),source.settings.mask_transition,{{0,0},{1,1}}};
        const double mask_bound=1.5*altitude_density_error_bound(ramp);
        profile_bound=std::max(profile_bound,mask_bound);profile_tolerance+=mask_bound*top.density;
    }else if(grouped&&(grouped->cells.size()>1||scene_has_active_anvil(scene))){
        const DevelopedEvaluationPlan plan(*grouped);
        for(size_t i=0;i<plan.fields().size();++i)include_profile(plan.fields()[i].recipe(),plan.cloud().cells[i].translation.y);
        // Each nonnegative profile coefficient has piecewise derivative at
        // most one (coverage + bridge <= 1, overlap <= 1): sum its allowance.
    }else include_profile(scene.cloud,0);
    double anvil_bound=0,anvil_tolerance=0;
    if(scene.frozen&&scene.frozen->anvil){
        anvil_bound=frozen_anvil_edge_error_bound(*scene.frozen);
        const auto recipe=frozen_effective_recipe(scene.frozen->fields.front());
        anvil_tolerance=anvil_bound*recipe.density*scene.frozen->anvil->density_scale*AltitudeDensityEvaluator(recipe.altitude_density).maximum();
    }else if(scene_has_active_anvil(scene)){
        anvil_bound=anvil_edge_error_bound(*scene.anvil);
        const auto& source=*scene.anvil;const auto& cells=source.cloud.trunk.cells;
        const auto& cell=*std::find_if(cells.begin(),cells.end(),[&](const auto& value){return value.id==source.cloud.target_cell;});
        const auto recipe=lower_centerline_to_recipe(cell.shape);
        // max(original, addition) is 1-Lipschitz in both operands. The sheet
        // inherits only factors <= 1 and has no density-overlap multiplier.
        anvil_tolerance=anvil_bound*recipe.density*source.settings.density_scale*AltitudeDensityEvaluator(recipe.altitude_density).maximum();
    }
    const double gain=finish_density_bound(scene.finish_stack);
    const double finish_tolerance=scene.frozen?finish_density_error_bound(*scene.frozen,scene.finish_stack):0;
    return {detailed,profile_bound,profile_tolerance*gain,anvil_bound,anvil_tolerance*gain,finish_tolerance};
}
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
        for(auto* image:accumulation_)if(image)SDL_ReleaseGPUTexture(device,image);
        if(accumulation_pipeline_)SDL_ReleaseGPUGraphicsPipeline(device,accumulation_pipeline_);
        if(majorant_texture_)SDL_ReleaseGPUTexture(device,majorant_texture_);
        if(majorant_generate_)SDL_ReleaseGPUComputePipeline(device,majorant_generate_);
        if(sun_tau_)SDL_ReleaseGPUTexture(device,sun_tau_);
        if(sun_generate_)SDL_ReleaseGPUComputePipeline(device,sun_generate_);
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
void GpuSpike::initialize(bool validation) {
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_INFO);
    window=SDL_CreateWindow("ProjectWhite | GPU field laboratory",900,600,SDL_WINDOW_RESIZABLE);
    gpu_check(window != nullptr,"Create window");
    SDL_SetWindowPosition(window,40,40);
    SDL_SetWindowMinimumSize(window,640,480);
    device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, validation, "direct3d12");
    gpu_check(device != nullptr,"Create D3D12 device (no silent CPU fallback)");
    gpu_check(SDL_ClaimWindowForGPUDevice(device,window),"Claim GPU window"); claimed=true;
    std::cout << "backend=" << SDL_GetGPUDeviceDriver(device) << " SDL=" << SDL_GetVersion()
              << " requested_debug="<<(validation?"true":"false")<<" format=R32_FLOAT dimensions=17x19x23\n";
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
        const auto cloud=gpu_scene_density_params(scene_snapshot_);bake_wait_ms=0;
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
    field=replacement;extent=dims;fixture=kind;field_density_hash_=density_input_hash(scene_snapshot_);volume_dirty=true;cache_reference_.clear();++bake_count;density_producer_revision=scene_revision;estimated_gpu_bytes=budget.peak_gpu_buffer_bytes;
    std::cout<<"bake="<<bake_count<<" extent="<<dims[0]<<'x'<<dims[1]<<'x'<<dims[2]<<" texture_bytes="<<budget.texture_bytes<<" peak_requested_gpu_bytes="<<estimated_gpu_bytes<<" cpu_reference_bytes="<<budget.cpu_reference_bytes<<" record_cpu_ms="<<bake_record_ms<<" submit_to_fence_wall_ms="<<bake_wait_ms<<" gpu_timestamp_ms=unavailable\n";
}
void GpuSpike::set_cache_resolution(int resolution) {
    if(resolution!=128&&resolution!=256)throw std::invalid_argument("Dense cache resolution must be 128 or 256");
    cache_resolution=resolution;queue_bake(scene_density_requires_direct(scene_snapshot_)?std::array<Uint32,3>{65,67,69}:std::array<Uint32,3>{Uint32(resolution),Uint32(resolution),Uint32(resolution)});
}
void GpuSpike::create_cloud(int preset) {
    scene_snapshot_=fixture_scene(preset);cloud_preset=preset;
    create_field({65,67,69},2);validate();
}
void GpuSpike::set_scene(const Scene& scene,std::uint64_t revision,std::chrono::steady_clock::time_point accepted) {
    require_valid(scene);const auto dirty=classify_change(scene_snapshot_,scene);
    const bool density_changed=has(dirty,Dirty::density)||fixture!=2;
    scene_snapshot_=scene;scene_revision=revision;exposure_ev=float(scene.exposure_ev);accepted_=accepted;
    if(scene_density_requires_direct(scene_snapshot_))std::cout<<"density_mode=grouped_direct groups=2 top_lobes="<<scene_has_active_top_lobes(scene_snapshot_)<<" density_cache=false sun_cache=false majorant_skip=false reason=independent_group_hard_constraints\n";
    const auto invalidate=invalidation(dirty);if(invalidate.hdr)volume_dirty=true;
    if(density_changed)try{const auto n=Uint32(cache_resolution);queue_bake(use_cache&&!scene_density_requires_direct(scene_snapshot_)?std::array<Uint32,3>{n,n,n}:std::array<Uint32,3>{65,67,69});}
    catch(const std::exception& e){report=std::string("Bake failed; direct preview active: ")+e.what();}

}
void GpuSpike::queue_bake(std::array<Uint32,3> dims) {
    const std::uint64_t old=64ull*1024*1024; // reserve the largest supported published cache
    const auto other=std::uint64_t(width)*height*4+std::uint64_t(hdr_width)*hdr_height*16*3+3*1024*1024;
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
            field_density_hash_=density_input_hash(ready->source.scene);cache_reference_.clear();++bake_count;density_producer_revision=ready->source.revision;
            bake_record_ms=ready->record_ms;bake_wait_ms=ready->wait_ms;estimated_gpu_bytes=ready->estimated_gpu_bytes;
            volume_dirty=true;report=scene_density_requires_direct(scene_snapshot_)?"Grouped density ready; Direct preview (combined cache masks unsupported)":"Latest density ready";
            std::cout<<"density_publish producer_revision="<<ready->source.revision<<" consumer_revision="<<scene_revision<<" matching_density_hash="<<field_density_hash_<<" peak_requested_gpu_bytes="<<estimated_gpu_bytes<<'\n';
        }else SDL_ReleaseGPUTexture(device,ready->texture);
    }
    if(auto trace=bake_worker_->take_telemetry();!trace.empty())std::cout<<trace;
    if(auto error=bake_worker_->take_error();!error.empty())report="Bake failed; direct preview active: "+error;
}
void GpuSpike::set_test_delay(unsigned milliseconds){bake_worker_->set_test_delay(milliseconds);}
bool GpuSpike::cache_current()const{return !scene_density_requires_direct(scene_snapshot_)&&field&&field_density_hash_==density_input_hash(scene_snapshot_);}
bool GpuSpike::bake_pending()const{return bake_worker_&&bake_worker_->busy();}
void GpuSpike::wait_bakes() {
    if(!bake_worker_)return;
    do{bake_worker_->wait_idle();poll_bakes();}while(bake_worker_->busy());
    if(field_density_hash_!=density_input_hash(scene_snapshot_))throw std::runtime_error("Latest density bake unavailable");
}
void GpuSpike::set_interacting(bool value){if(value!=interacting_){interacting_=value;volume_dirty=true;}}
void GpuSpike::note_present(std::uint64_t revision,std::chrono::steady_clock::time_point submitted) {
    if(revision!=scene_revision||revision==last_present_revision_)return;
    last_present_revision_=revision;
    const double elapsed=std::chrono::duration<double,std::milli>(submitted-accepted_).count();
    if(latency_samples_.size()==512)latency_samples_.erase(latency_samples_.begin());
    latency_samples_.push_back(elapsed);
    std::cout<<"edit_to_present_submission revision="<<revision<<" ms="<<elapsed<<" samples="<<latency_samples_.size()<<" p50_ms="<<percentile(latency_samples_,0.5)<<" p95_ms="<<percentile(latency_samples_,0.95)<<'\n';
}
void GpuSpike::validate() {
    wait_bakes();
    // The scene is fixed for this readback. Source capability checks can build
    // complete top/anvil plans, so resolve them once, not once per voxel.
    const bool direct_required=scene_density_requires_direct(scene_snapshot_);
    const bool retain_cache_reference=fixture==2&&use_cache&&!direct_required;
    if(validated_bake_==bake_count&&(!use_cache||direct_required||!cache_reference_.empty())){std::cout<<"density_validation_reused bake="<<bake_count<<'\n';return;}
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
    if(retain_cache_reference)cache_reference_.resize(size_t(extent[0])*extent[1]*extent[2]);
    const SceneDensityEvaluator reference_field(scene_snapshot_);
    const GridLayout layout{reference_field.local_support(),extent};
    for(Uint32 z=0;z<extent[2];++z) for(Uint32 y=0;y<extent[1];++y) for(Uint32 x=0;x<extent[0];++x) {
        const float value=values[(z*extent[1]+y)*pitch+x];
        finite=finite && std::isfinite(value);
        if(retain_cache_reference)cache_reference_[(size_t(z)*extent[1]+y)*extent[0]+x]=value;
        const float expected=fixture==2?float(reference_field.at(index_to_local(layout,{double(x),double(y),double(z)}))):reference(x,y,z,extent,fixture);
        max_error=std::max(max_error,std::abs(value-expected));
    }
    SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
    const auto allowance=density_allowance(scene_snapshot_);
    const auto detailed=allowance.detailed;const auto profile_bound=allowance.profile_bound,profile_tolerance=allowance.profile_tolerance;
    const float tolerance=fixture==2?float((detailed?1e-4:2e-5)*std::max(1.0,reference_field.maximum())+profile_tolerance+allowance.anvil_tolerance+allowance.finish_tolerance):1e-6f;
    std::cout<<"density_reference max_abs_error="<<max_error<<" tolerance="<<tolerance<<" detailed="<<detailed
        <<" profile_scale_error_bound="<<profile_bound<<" profile_density_tolerance="<<profile_tolerance
        <<" anvil_coverage_error_bound="<<allowance.anvil_bound<<" anvil_density_tolerance="<<allowance.anvil_tolerance
        <<" modifier_density_tolerance="<<allowance.finish_tolerance<<'\n';
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
    SDL_GPUTextureSamplerBinding input{lit?displayed_hdr():field,lit?point_sampler:sampler}; SDL_BindGPUFragmentSamplers(pass,0,&input,1);
    struct View {float slice; Uint32 axis,fixture; float padding;} view{slice,axis,fixture,0};
    if(lit) {const Float4 display_params{exposure_ev,float(diagnostic_mode),diagnostic_scale(diagnostic_mode),0};SDL_PushGPUFragmentUniformData(cmd,0,&display_params,sizeof(display_params));}
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
    auto make_pipeline=[&](const char* name,bool tone,bool accumulation=false) {
        auto* vs=make_shader("fullscreen.vert.hlsl.dxil",SDL_GPU_SHADERSTAGE_VERTEX,0,0);
        SDL_GPUShader* fs=nullptr;SDL_GPUGraphicsPipeline* p=nullptr;
        try {
            fs=make_shader(name,SDL_GPU_SHADERSTAGE_FRAGMENT,tone?1:accumulation?2:3,(tone||accumulation)?1:2);
            SDL_GPUColorTargetDescription color{};color.format=tone?SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
            SDL_GPUGraphicsPipelineCreateInfo ci{};ci.vertex_shader=vs;ci.fragment_shader=fs;ci.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            ci.target_info.num_color_targets=1;ci.target_info.color_target_descriptions=&color;
            p=SDL_CreateGPUGraphicsPipeline(device,&ci);gpu_check(p!=nullptr,"Create volume/tonemap pipeline");
        }catch(...){SDL_ReleaseGPUShader(device,vs);if(fs)SDL_ReleaseGPUShader(device,fs);throw;}
        SDL_ReleaseGPUShader(device,vs);SDL_ReleaseGPUShader(device,fs);return p;
    };
    volume_pipeline=make_pipeline("volume.frag.hlsl.dxil",false);
    tonemap_pipeline=make_pipeline("tonemap.frag.hlsl.dxil",true);
    accumulation_pipeline_=make_pipeline("accumulate.frag.hlsl.dxil",false,true);
    SDL_GPUSamplerCreateInfo sci{};sci.min_filter=sci.mag_filter=SDL_GPU_FILTER_NEAREST;
    sci.address_mode_u=sci.address_mode_v=sci.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    point_sampler=SDL_CreateGPUSampler(device,&sci);gpu_check(point_sampler!=nullptr,"Create HDR point sampler");
    auto bytes=shader("optics.comp.hlsl.dxil");SDL_GPUComputePipelineCreateInfo ci{};
    ci.code=bytes.data();ci.code_size=bytes.size();ci.entrypoint="main";ci.format=SDL_GPU_SHADERFORMAT_DXIL;
    ci.num_readwrite_storage_buffers=1;ci.threadcount_x=ci.threadcount_y=ci.threadcount_z=1;
    optical_test=SDL_CreateGPUComputePipeline(device,&ci);gpu_check(optical_test!=nullptr,"Create optical test pipeline");
    bytes=shader("cache_samples.comp.hlsl.dxil");ci.code=bytes.data();ci.code_size=bytes.size();ci.num_uniform_buffers=2;ci.num_samplers=1;
    cache_sample_test=SDL_CreateGPUComputePipeline(device,&ci);gpu_check(cache_sample_test!=nullptr,"Create cache sample test pipeline");
    bytes=shader("sun_cache.comp.hlsl.dxil");ci.code=bytes.data();ci.code_size=bytes.size();ci.num_readwrite_storage_buffers=0;ci.num_readwrite_storage_textures=1;ci.threadcount_x=ci.threadcount_y=ci.threadcount_z=4;
    sun_generate_=SDL_CreateGPUComputePipeline(device,&ci);gpu_check(sun_generate_!=nullptr,"Create sun cache pipeline");
    bytes=shader("majorant.comp.hlsl.dxil");ci.code=bytes.data();ci.code_size=bytes.size();ci.num_uniform_buffers=1;
    majorant_generate_=SDL_CreateGPUComputePipeline(device,&ci);gpu_check(majorant_generate_!=nullptr,"Create majorant pipeline");
}
SDL_GPUTexture* GpuSpike::displayed_hdr()const{return progressive&&preview_state.samples()>0&&accumulation_[1-accumulation_next_]?accumulation_[1-accumulation_next_]:hdr;}
void GpuSpike::prepare_preview(){
    if(!progressive)return;
    if(progressive_budget<1||progressive_budget>256)throw std::invalid_argument("Progressive budget 1..256");
    const bool cached=use_cache&&cache_current();
    // A new requested resolution can settle while the previous field is still
    // displayed. Publication must reset its history even when density is unchanged.
    const std::array<unsigned,14> settings{width,height,unsigned(internal_width),unsigned(view_steps),unsigned(shadow_steps),unsigned(cached),unsigned(cache_resolution),unsigned(sun_cache_resolution),unsigned(empty_skip),progressive_budget,diagnostic_mode,cached?extent[0]:0,cached?extent[1]:0,cached?extent[2]:0};
    const double now=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(preview_state.update(preview_key(scene_snapshot_,settings),interacting_,now)){volume_dirty=true;accumulation_next_=0;}
    if(!preview_state.editing()&&!progressive_paused&&preview_state.samples()<progressive_budget)volume_dirty=true;
}
void GpuSpike::render_volume(SDL_GPUCommandBuffer* cmd) {
    if(!volume_dirty)return;
    if(view_steps<8||view_steps>512||shadow_steps<1||shadow_steps>64||internal_width<64||internal_width>640)
        throw std::invalid_argument("Invalid rendering quality budget");
    const bool editing=progressive?preview_state.editing():interacting_;
    rendered_steps_=editing?std::min(view_steps,32):view_steps;
    rendered_cache_=use_cache&&cache_current();
    const int render_width=editing?std::min(internal_width,96):internal_width;
    const int render_shadows=editing?std::min(shadow_steps,4):shadow_steps;rendered_shadows_=render_shadows;
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
    const auto& recipe=scene_snapshot_.cloud;const auto field_params=gpu_scene_density_params(scene_snapshot_);
    const auto& camera=scene_snapshot_.camera;const auto eye=camera.position,target_point=camera.target;
    const auto forward=normalize(target_point-eye),right=normalize(cross(forward,camera.up)),up=cross(right,forward);
    const auto origin=world_to_local(recipe.transform,{0,0,0});
    auto linear=recipe.transform;linear.translation={0,0,0};
    const auto x=world_to_local(linear,{1,0,0}),y=world_to_local(linear,{0,1,0}),z=world_to_local(linear,{0,0,1});
    const auto sun=scene_snapshot_.sun.direction_to_light;
    auto pack=[](Vec3 v,float w=0){return Float4{float(v.x),float(v.y),float(v.z),w};};
    if(sun_cache_resolution!=0&&sun_cache_resolution!=32&&sun_cache_resolution!=64)throw std::invalid_argument("Sun cache must be 0/32/64");
    rendered_sun_=sun_cache_resolution!=0&&rendered_cache_&&!editing;
    if(rendered_sun_){
        const auto key=sun_cache_key(scene_snapshot_,extent,unsigned(sun_cache_resolution),unsigned(render_shadows));
        if(!sun_tau_||sun_extent_!=sun_cache_resolution){
            auto* replacement=texture(device,SDL_GPU_TEXTURETYPE_3D,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_TEXTUREUSAGE_SAMPLER,sun_cache_resolution,sun_cache_resolution,sun_cache_resolution);
            if(sun_tau_)SDL_ReleaseGPUTexture(device,sun_tau_);sun_tau_=replacement;sun_extent_=sun_cache_resolution;sun_key_=0;
        }
        if(sun_key_!=key){
            const auto start=std::chrono::steady_clock::now();const auto direction=world_to_local(linear,sun);
            const std::array<Float4,3> params{pack(direction,float(recipe.optics.extinction_scale)),Float4{float(sun_extent_),float(sun_extent_),float(sun_extent_),float(render_shadows)},Float4{float(camera.far_plane),0,0,0}};
            SDL_PushGPUComputeUniformData(cmd,0,params.data(),sizeof(params));SDL_PushGPUComputeUniformData(cmd,1,&field_params,sizeof(field_params));
            SDL_GPUStorageTextureReadWriteBinding output{};output.texture=sun_tau_;
            auto* compute=SDL_BeginGPUComputePass(cmd,&output,1,nullptr,0);SDL_BindGPUComputePipeline(compute,sun_generate_);
            const SDL_GPUTextureSamplerBinding input{field,sampler};SDL_BindGPUComputeSamplers(compute,0,&input,1);
            SDL_DispatchGPUCompute(compute,(sun_extent_+3)/4,(sun_extent_+3)/4,(sun_extent_+3)/4);SDL_EndGPUComputePass(compute);
            sun_key_=key;++sun_cache_builds;sun_producer_revision=scene_revision;
            std::cout<<"sun_cache_build revision="<<scene_revision<<" key="<<key<<" resolution="<<sun_extent_<<" bytes="<<std::uint64_t(sun_extent_)*sun_extent_*sun_extent_*4<<" record_cpu_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" gpu_time=included_in_frame_fence_wall\n";
        }
    }
    rendered_skip_=(empty_skip||diagnostic_mode==6||diagnostic_mode==7||diagnostic_mode==11)&&rendered_cache_;
    if(rendered_skip_){
        const std::array<Uint32,3> dims{(extent[0]+7)/8,(extent[1]+7)/8,(extent[2]+7)/8};
        if(!majorant_texture_||majorant_extent_!=dims){
            auto* replacement=texture(device,SDL_GPU_TEXTURETYPE_3D,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_TEXTUREUSAGE_SAMPLER,dims[0],dims[1],dims[2]);
            if(majorant_texture_)SDL_ReleaseGPUTexture(device,majorant_texture_);majorant_texture_=replacement;majorant_extent_=dims;majorant_key_=0;
        }
        const auto key=density_job_hash(scene_snapshot_,extent);
        if(majorant_key_!=key){
            const auto start=std::chrono::steady_clock::now();const std::array<Uint32,4> params{extent[0],extent[1],extent[2],0};SDL_PushGPUComputeUniformData(cmd,0,params.data(),sizeof(params));
            SDL_GPUStorageTextureReadWriteBinding output{};output.texture=majorant_texture_;
            auto* compute=SDL_BeginGPUComputePass(cmd,&output,1,nullptr,0);SDL_BindGPUComputePipeline(compute,majorant_generate_);
            const SDL_GPUTextureSamplerBinding input{field,sampler};SDL_BindGPUComputeSamplers(compute,0,&input,1);
            SDL_DispatchGPUCompute(compute,(dims[0]+3)/4,(dims[1]+3)/4,(dims[2]+3)/4);SDL_EndGPUComputePass(compute);majorant_key_=key;majorant_producer_revision=scene_revision;
            std::cout<<"majorant_build key="<<key<<" bytes="<<std::uint64_t(dims[0])*dims[1]*dims[2]*4<<" record_cpu_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" execution=in_frame_fence_wall\n";
        }
    }
    const bool accumulate=progressive&&!preview_state.editing()&&!progressive_paused;
    const std::array<Float4,15> view{
        pack(eye,float(camera.near_plane)),pack(right,float(std::tan(camera.vertical_fov_degrees*3.141592653589793/360))),pack(up,float(recipe.optics.g)),pack(forward,float(recipe.optics.extinction_scale)),
        pack(sun,float(recipe.optics.albedo)),pack(scene_snapshot_.sun.irradiance,float(camera.far_plane)),
        Float4{float(x.x),float(y.x),float(z.x),float(origin.x)},Float4{float(x.y),float(y.y),float(z.y),float(origin.y)},Float4{float(x.z),float(y.z),float(z.z),float(origin.z)},
        Float4{float(rendered_steps_),float(render_shadows),float(w)/float(h),rendered_cache_?1.0f:0.0f},Float4{rendered_sun_?1.0f:0.0f,0,0,0},Float4{float(extent[0]),float(extent[1]),float(extent[2]),rendered_skip_?1.f:0.f},Float4{float(preview_state.samples()),accumulate?1.f:0.f,1.f/w,1.f/h},Float4{float(diagnostic_mode),0,0,0},Float4{scene_snapshot_.preview_approx.enabled?1.f:0.f,float(scene_snapshot_.preview_approx.strength),0,0}};
    SDL_PushGPUFragmentUniformData(cmd,0,view.data(),sizeof(view));
    SDL_PushGPUFragmentUniformData(cmd,1,&field_params,sizeof(field_params));
    SDL_GPUColorTargetInfo color{};color.texture=hdr;color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    auto* pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr);SDL_BindGPUGraphicsPipeline(pass,volume_pipeline);
    const SDL_GPUTextureSamplerBinding inputs[3]{{field,sampler},{rendered_sun_?sun_tau_:field,sampler},{rendered_skip_?majorant_texture_:field,point_sampler}};SDL_BindGPUFragmentSamplers(pass,0,inputs,3);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    if(accumulate){
        if(!accumulation_[0]||accumulation_width_!=w||accumulation_height_!=h){
            for(auto*& image:accumulation_){if(image)SDL_ReleaseGPUTexture(device,image);image=nullptr;}
            accumulation_width_=w;accumulation_height_=h;accumulation_next_=0;
            for(auto*& image:accumulation_)image=texture(device,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER,w,h,1);
        }
        SDL_GPUColorTargetInfo output{};output.texture=accumulation_[accumulation_next_];output.load_op=SDL_GPU_LOADOP_DONT_CARE;output.store_op=SDL_GPU_STOREOP_STORE;
        auto* combine=SDL_BeginGPURenderPass(cmd,&output,1,nullptr);SDL_BindGPUGraphicsPipeline(combine,accumulation_pipeline_);
        const SDL_GPUTextureSamplerBinding images[2]{{hdr,point_sampler},{preview_state.samples()?accumulation_[1-accumulation_next_]:hdr,point_sampler}};SDL_BindGPUFragmentSamplers(combine,0,images,2);
        const std::array<unsigned,4> parameters{preview_state.samples(),0,0,0};SDL_PushGPUFragmentUniformData(cmd,0,parameters.data(),sizeof(parameters));SDL_DrawGPUPrimitives(combine,3,1,0,0);SDL_EndGPURenderPass(combine);
        preview_state.complete_sample();accumulation_next_=1-accumulation_next_;
        std::cout<<"progressive_sample="<<preview_state.samples()<<" revision="<<scene_revision<<" width="<<w<<" height="<<h<<" accumulation_bytes="<<std::uint64_t(w)*h*32<<'\n';
    }
    volume_dirty=false;
}
void GpuSpike::validate_majorant(){
    if(!rendered_skip_)throw std::runtime_error("Skipping requested without current dense field");
    if(cache_reference_.empty())validate();
    const auto expected=build_majorant({scene_snapshot_.cloud.envelope,extent},cache_reference_);const auto n=majorant_extent_;const Uint32 pitch=(n[0]+63)/64*64;Transfer transfer(device,pitch*n[1]*n[2]*4);
    auto* cmd=SDL_AcquireGPUCommandBuffer(device);gpu_check(cmd!=nullptr,"Acquire majorant readback");auto* pass=SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion source{};source.texture=majorant_texture_;source.w=n[0];source.h=n[1];source.d=n[2];const SDL_GPUTextureTransferInfo dest{transfer.buffer,0,pitch,n[1]};SDL_DownloadFromGPUTexture(pass,&source,&dest);SDL_EndGPUCopyPass(pass);submit_wait(device,cmd);
    const auto* values=static_cast<float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));gpu_check(values!=nullptr,"Map majorant");bool exact=true;unsigned empty=0;
    for(unsigned z=0;z<n[2];++z)for(unsigned y=0;y<n[1];++y)for(unsigned x=0;x<n[0];++x){float v=values[(z*n[1]+y)*pitch+x];exact=exact&&v==expected.levels[0].maxima[(z*n[1]+y)*n[0]+x];empty+=v==0;}
    SDL_UnmapGPUTransferBuffer(device,transfer.buffer);std::cout<<"majorant_verified bricks="<<n[0]*n[1]*n[2]<<" empty="<<empty<<" CPU_GPU_exact="<<exact<<" hierarchy_levels="<<expected.levels.size()<<'\n';if(!exact)throw std::runtime_error("Majorant differs from conservative CPU reference");
}
void GpuSpike::validate_sun_cache() {
    if(!rendered_sun_)throw std::runtime_error("Sun cache comparison requested but direct fallback rendered");
    if(cache_reference_.empty())validate();
    const Uint32 n=Uint32(sun_extent_),pitch=(n+63)/64*64;Transfer transfer(device,pitch*n*n*4);
    auto* cmd=SDL_AcquireGPUCommandBuffer(device);gpu_check(cmd!=nullptr,"Acquire sun readback");
    auto* pass=SDL_BeginGPUCopyPass(cmd);SDL_GPUTextureRegion source{};source.texture=sun_tau_;source.w=source.h=source.d=n;
    const SDL_GPUTextureTransferInfo target_info{transfer.buffer,0,pitch,n};SDL_DownloadFromGPUTexture(pass,&source,&target_info);SDL_EndGPUCopyPass(pass);submit_wait(device,cmd);
    const auto* values=static_cast<float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));gpu_check(values!=nullptr,"Map sun cache");
    const GridLayout density_grid{scene_snapshot_.cloud.envelope,extent},sun_grid{scene_snapshot_.cloud.envelope,{n,n,n}};
    double error=0,t_error=0;bool valid=true;
    for(Uint32 z=0;z<n;++z)for(Uint32 y=0;y<n;++y)for(Uint32 x=0;x<n;++x){
        const double actual=values[(z*n+y)*pitch+x];valid=valid&&std::isfinite(actual)&&actual>=0;
        const double expected=sun_optical_depth(scene_snapshot_,density_grid,cache_reference_,index_to_local(sun_grid,{double(x),double(y),double(z)}),unsigned(shadow_steps));
        error=std::max(error,std::abs(actual-expected));t_error=std::max(t_error,std::abs(std::exp(-actual)-std::exp(-expected)));
    }
    SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
    std::cout<<"sun_cache_verified voxels="<<n*n*n<<" tau_max_error="<<error<<" T_max_error="<<t_error<<" same_snapshot=true\n";
    if(!valid||t_error>.005)throw std::runtime_error("Sun cache GPU/CPU disagreement");
}
void GpuSpike::validate_cache_samples() {
    SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,256*4*sizeof(float),0};
    auto* output=SDL_CreateGPUBuffer(device,&bi);gpu_check(output!=nullptr,"Create cache sample buffer");
    try{
        Transfer transfer(device,bi.size);auto* cmd=SDL_AcquireGPUCommandBuffer(device);gpu_check(cmd!=nullptr,"Acquire cache sample commands");
        const auto cloud=gpu_scene_density_params(scene_snapshot_);SDL_PushGPUComputeUniformData(cmd,1,&cloud,sizeof(cloud));
        const Float4 unused{};SDL_PushGPUComputeUniformData(cmd,0,&unused,sizeof(unused));
        SDL_GPUStorageBufferReadWriteBinding binding{};binding.buffer=output;
        auto* pass=SDL_BeginGPUComputePass(cmd,nullptr,0,&binding,1);SDL_BindGPUComputePipeline(pass,cache_sample_test);
        const SDL_GPUTextureSamplerBinding input{field,sampler};SDL_BindGPUComputeSamplers(pass,0,&input,1);
        SDL_DispatchGPUCompute(pass,256,1,1);SDL_EndGPUComputePass(pass);
        auto* copy=SDL_BeginGPUCopyPass(cmd);const SDL_GPUBufferRegion source{output,0,bi.size};const SDL_GPUTransferBufferLocation dest{transfer.buffer,0};
        SDL_DownloadFromGPUBuffer(copy,&source,&dest);SDL_EndGPUCopyPass(copy);submit_wait(device,cmd);
        const auto* values=static_cast<float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));gpu_check(values!=nullptr,"Map cache samples");
        if(scene_density_requires_direct(scene_snapshot_)){
            const SceneDensityEvaluator cpu(scene_snapshot_);const auto allowance=density_allowance(scene_snapshot_);
            const double tolerance=(allowance.detailed?1e-4:2e-5)*std::max(1.,cpu.maximum())+allowance.profile_tolerance+allowance.anvil_tolerance+allowance.finish_tolerance;
            bool finite=true;double maximum=0,sum=0;
            for(int i=0;i<256;++i){const Vec3 p{values[4*i+1],values[4*i+2],values[4*i+3]};
                for(int c=0;c<4;++c)finite=finite&&std::isfinite(values[4*i+c]);
                const double error=std::abs(values[4*i]-cpu.at(p));maximum=std::max(maximum,error);sum+=error;}
            bool hard_interiors=true;const auto& layers=scene_snapshot_.finish_stack.layers;
            for(std::size_t layer=0;layer<layers.size();++layer){const auto& item=layers[layer];if(item.enabled&&item.kind==FinishModifierKind::cut&&item.hard_cut&&item.strength==1&&item.target_kind==FinishTargetKind::object)hard_interiors=hard_interiors&&values[4*(16+12*layer)]==0;}
            SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
            std::cout<<"grouped_direct_points samples=256 max_abs="<<maximum<<" mean_abs="<<sum/256<<" tolerance="<<tolerance<<" coordinates=GPU_evaluated combined_cache=false\n";
            if(!layers.empty())std::cout<<"modifier_gpu_probes="<<layers.size()*12<<" mask_boundary_and_feather=true hard_interiors_zero="<<hard_interiors<<" modifier_density_tolerance="<<allowance.finish_tolerance<<'\n';
            if(!finite||!hard_interiors||maximum>tolerance)throw std::runtime_error("Grouped direct GPU points differ from CPU field or modifier hard constraints");
        }else{
        double maximum=0,sum=0;bool finite=true;for(int i=0;i<256;++i){for(int c=0;c<4;++c)finite=finite&&std::isfinite(values[i*4+c]);double delta=std::abs(double(values[i*4+2])-values[i*4]);maximum=std::max(maximum,delta);sum+=delta;}
        const bool protected_base=!scene_snapshot_.cloud.base.enabled||values[2]==0;
        const bool protected_cut=scene_snapshot_.cloud.cuts.empty()||values[6]==0;
        std::cout<<"dense_sample_comparison samples=256 max_abs="<<maximum<<" mean_abs="<<sum/256<<" raw_base_leak="<<values[1]<<" raw_cut_leak="<<values[5]<<" hard_constraints_preserved="<<(protected_base&&protected_cut)<<'\n';
        SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
        if(!finite||!protected_base||!protected_cut)throw std::runtime_error("Cache interpolation violated hard constraints");
        }
    }catch(...){SDL_ReleaseGPUBuffer(device,output);throw;}SDL_ReleaseGPUBuffer(device,output);
}
void GpuSpike::validate_optics() {
    SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,128*4*sizeof(float),0};
    auto* output=SDL_CreateGPUBuffer(device,&bi);gpu_check(output!=nullptr,"Create optical result buffer");
    try {
        Transfer transfer(device,bi.size);auto* cmd=SDL_AcquireGPUCommandBuffer(device);gpu_check(cmd!=nullptr,"Acquire optical test commands");
        SDL_GPUStorageBufferReadWriteBinding binding{};binding.buffer=output;
        auto* pass=SDL_BeginGPUComputePass(cmd,nullptr,0,&binding,1);SDL_BindGPUComputePipeline(pass,optical_test);
        SDL_DispatchGPUCompute(pass,128,1,1);SDL_EndGPUComputePass(pass);
        auto* copy=SDL_BeginGPUCopyPass(cmd);SDL_GPUBufferRegion source{output,0,bi.size};SDL_GPUTransferBufferLocation destination{transfer.buffer,0};
        SDL_DownloadFromGPUBuffer(copy,&source,&destination);SDL_EndGPUCopyPass(copy);submit_wait(device,cmd);
        auto* values=static_cast<float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));gpu_check(values!=nullptr,"Map optical test");
        const double sigma[]={0,0.01,0.1,1,10,0.0000001,0.25,2},distance[]={100,20,15,2,10,100,8,0};
        double error=0;bool finite=true;
        for(int i=0;i<8;++i){double T=std::exp(-sigma[i]*distance[i]);for(int j=0;j<4;++j){finite=finite&&std::isfinite(values[i*4+j]);error=std::max(error,std::abs(double(values[i*4+j])-(j==0?T:0.5*(1-T))));}}
        const std::array<Float4,6> bounds_expected{Float4{1,2,4,1},Float4{1,0,1,1},Float4{0,0,0,1},Float4{1,2,4,1},Float4{1,2,4,1},Float4{1,float(2*std::sqrt(3)),float(4*std::sqrt(3)),1}};
        double bounds_error=0;
        for(int i=0;i<6;++i){const float expected[]{bounds_expected[i].x,bounds_expected[i].y,bounds_expected[i].z,bounds_expected[i].w};for(int j=0;j<4;++j){finite=finite&&std::isfinite(values[(i+8)*4+j]);bounds_error=std::max(bounds_error,std::abs(double(values[(i+8)*4+j])-expected[j]));}}
        const auto a=integrate_constant_source(.1,2,.3),b=integrate_constant_source(.5,1,.2);
        const std::array<OpticalResult,6> expected_segments{integrate_constant_source(0,3,2),integrate_constant_source(1e-8,5,1e-9),compose_front_to_back(a,b),compose_front_to_back(b,a),integrate_constant_source(.025,40,.05),integrate_constant_source(.125,8,.3)};
        bool contract_ok=true;
        for(int i=0;i<6;++i){const int offset=(14+i)*4;const auto expected=expected_segments[i];const double te=std::abs(values[offset]-expected.transmittance),le=std::abs(values[offset+1]-expected.radiance);const double l_abs=i==1?1e-14:2e-5;
            contract_ok=contract_ok&&std::isfinite(values[offset])&&std::isfinite(values[offset+1])&&te<=2e-5&&le<=l_abs+2e-5*std::abs(expected.radiance);
            std::cout<<"GPU optical_contract case="<<i<<" T="<<values[offset]<<" L="<<values[offset+1]<<" T_abs_error="<<te<<" L_abs_error="<<le<<" L_absolute_tolerance="<<l_abs<<" relative_tolerance=2e-5\n";
        }
        double previous=1;for(int i=0;i<3;++i){const double actual=values[(20+i)*4],difference=std::abs(actual-std::exp(-1.2));contract_ok=contract_ok&&std::isfinite(actual)&&difference<previous*.35+2e-6;previous=difference;std::cout<<"GPU optical_convergence steps="<<(16<<i)<<" T_abs_error="<<difference<<'\n';}
        contract_ok=contract_ok&&values[23*4]==0&&values[23*4+1]==0&&values[23*4+2]==0&&values[23*4+3]==1;
        double phase_mu_error=0,phase_pdf_relative=0;const float gs[]{-.95f,-.5f,0,.5f,.95f};
        for(int i=24;i<128;++i){const double g=gs[(i-24)%5],u=phase_random(i,3,0,42),v=phase_random(i,3,1,42);auto expected=sample_hg({0,0,1},g,u,v);
            phase_mu_error=std::max(phase_mu_error,std::abs(double(values[i*4])-expected.direction.z));phase_pdf_relative=std::max(phase_pdf_relative,std::abs(double(values[i*4+1])-expected.pdf)/expected.pdf);
            contract_ok=contract_ok&&std::isfinite(values[i*4])&&std::isfinite(values[i*4+1])&&values[i*4+1]>0&&values[i*4+2]==u&&values[i*4+3]==v;
        }
        contract_ok=contract_ok&&phase_mu_error<2e-5&&phase_pdf_relative<.001;
        std::cout<<"GPU HG samples=104 cosine_max_error="<<phase_mu_error<<" pdf_max_relative_error="<<phase_pdf_relative<<" RNG_exact=true\n";
        SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
        if(!contract_ok)throw std::runtime_error("GPU optical integration contract failed");
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
    auto* copy=SDL_BeginGPUCopyPass(cmd);SDL_GPUTextureRegion source{};source.texture=displayed_hdr();source.w=hdr_width;source.h=hdr_height;source.d=1;
    SDL_GPUTextureTransferInfo dest{transfer.buffer,0,pitch,hdr_height};SDL_DownloadFromGPUTexture(copy,&source,&dest);SDL_EndGPUCopyPass(copy);submit_wait(device,cmd);
    std::vector<float> result(size_t(hdr_width)*hdr_height*4);bool valid=true;float maximum=0;
    const auto* values=static_cast<const float*>(SDL_MapGPUTransferBuffer(device,transfer.buffer,false));gpu_check(values!=nullptr,"Map HDR readback");
    for(Uint32 y=0;y<hdr_height;++y)for(Uint32 x=0;x<hdr_width;++x)for(Uint32 c=0;c<4;++c){float value=values[(y*pitch+x)*4+c];result[(y*hdr_width+x)*4+c]=value;valid=valid&&std::isfinite(value)&&value>=0&&(c!=3||value<=1);maximum=std::max(maximum,value);}
    SDL_UnmapGPUTransferBuffer(device,transfer.buffer);
    if(!valid)throw std::runtime_error("HDR contains NaN/Inf/negative radiance or invalid transmittance");
    if(diagnostic_mode){std::cout<<"Diagnostic HDR mode="<<diagnostic_mode<<" name="<<diagnostic_name(diagnostic_mode)<<" revision="<<rendered_revision<<" max="<<maximum<<" raw_values=true\n";return result;}
    if(progressive&&preview_state.samples()){std::cout<<"Progressive HDR finite readback samples="<<preview_state.samples()<<" max_channel="<<maximum<<" CPU_center_ray_validation=not_applicable_to_jittered_average\n";return result;}
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
    const SceneDensityEvaluator cpu(scene_snapshot_);const auto& recipe=scene_snapshot_.cloud;
    const auto& camera=scene_snapshot_.camera;const auto eye=camera.position,target_point=camera.target;
    auto normalize=[](Vec3 v){return v*(1/std::sqrt(dot(v,v)));};
    auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto forward=normalize(target_point-eye),right=normalize(cross(forward,camera.up)),up=cross(right,forward);
    const auto origin=world_to_local(recipe.transform,eye);
    double error=0,min_t=1,direct_error=0,direct_sum=0;
    for(Uint32 y=0;y<hdr_height;++y)for(Uint32 x=0;x<hdr_width;++x) {
        const double px=(2*(x+0.5)/hdr_width-1)*double(hdr_width)/hdr_height,py=1-2*(y+0.5)/hdr_height;
        const auto direction=normalize(forward+(right*px+up*py)*std::tan(camera.vertical_fov_degrees*3.141592653589793/360));
        auto linear=recipe.transform;linear.translation={0,0,0};
        const auto local_direction=world_to_local(linear,direction);
        double tau=0,direct_tau=0;
        if(const auto interval=intersect_bounds(origin,local_direction,cpu.local_support(),camera.near_plane,camera.far_plane)) {
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
