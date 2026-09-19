#include "white/gpu_bake_worker.hpp"
#include "white/density.hpp"
#include "white/dense_cache.hpp"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace white {
namespace {void require(bool ok,const char* name){if(!ok)throw std::runtime_error(std::string(name)+": "+SDL_GetError());}}
GpuBakeWorker::GpuBakeWorker(SDL_GPUDevice* d,SDL_GPUComputePipeline* p):device_(d),pipeline_(p),thread_([this]{run();}){}
GpuBakeWorker::~GpuBakeWorker(){
    {std::lock_guard lock(mutex_);stop_=true;}cv_.notify_all();thread_.join();
    if(result_&&result_->texture)SDL_ReleaseGPUTexture(device_,result_->texture);
}
void GpuBakeWorker::request(DensityJob job){std::lock_guard lock(mutex_);queue_.request(std::move(job));cv_.notify_all();}
std::optional<BakedDensity> GpuBakeWorker::take(){std::lock_guard lock(mutex_);auto result=std::move(result_);result_.reset();cv_.notify_all();return result;}
bool GpuBakeWorker::busy()const{std::lock_guard lock(mutex_);return queue_.running()||queue_.pending();}
void GpuBakeWorker::set_test_delay(unsigned ms){std::lock_guard lock(mutex_);test_delay_ms_=ms;}
std::string GpuBakeWorker::take_error(){std::lock_guard lock(mutex_);return std::exchange(error_,{});}
std::string GpuBakeWorker::take_telemetry(){std::lock_guard lock(mutex_);return std::exchange(telemetry_,{});}
void GpuBakeWorker::wait_idle(){std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return result_.has_value()||(!queue_.running()&&!queue_.pending());});}
void GpuBakeWorker::run(){
    for(;;){
        DensityJob job;
        {std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stop_||(!result_&&queue_.pending());});if(stop_)return;job=*queue_.start();}
        BakedDensity built;built.source=job;bool complete=true;
        try{
            const auto start=std::chrono::steady_clock::now();
            const auto budget=cache_budget(job.extent,job.previous_bytes,job.other_gpu_bytes);built.estimated_gpu_bytes=budget.peak_gpu_buffer_bytes;
            const auto cloud=gpu_density_params(DensityField(job.scene.cloud));
            SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_3D;info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;info.usage=SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width=job.extent[0];info.height=job.extent[1];info.layer_count_or_depth=job.extent[2];info.num_levels=1;
            built.texture=SDL_CreateGPUTexture(device_,&info);require(built.texture!=nullptr,"Create asynchronous bake texture");
            for(Uint32 z=0;z<job.extent[2];z+=16){
                auto* command=SDL_AcquireGPUCommandBuffer(device_);require(command!=nullptr,"Acquire asynchronous bake command");
                const std::array<Uint32,8> params{job.extent[0],job.extent[1],job.extent[2],2,z,0,0,0};
                SDL_PushGPUComputeUniformData(command,0,params.data(),sizeof(params));SDL_PushGPUComputeUniformData(command,1,&cloud,sizeof(cloud));
                SDL_GPUStorageTextureReadWriteBinding binding{};binding.texture=built.texture;
                auto* pass=SDL_BeginGPUComputePass(command,&binding,1,nullptr,0);SDL_BindGPUComputePipeline(pass,pipeline_);
                SDL_DispatchGPUCompute(pass,(job.extent[0]+3)/4,(job.extent[1]+3)/4,(std::min(16u,job.extent[2]-z)+3)/4);SDL_EndGPUComputePass(pass);
                const auto wait_start=std::chrono::steady_clock::now();auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);require(fence!=nullptr,"Submit asynchronous bake");
                const bool ok=SDL_WaitForGPUFences(device_,true,&fence,1);SDL_ReleaseGPUFence(device_,fence);require(ok,"Fence asynchronous bake");
                built.wait_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wait_start).count();
                // A stale running request is discarded only after its current
                // submission's fence; never free memory still owned by the GPU.
                {std::unique_lock lock(mutex_);
                    if(test_delay_ms_>0)cv_.wait_for(lock,std::chrono::milliseconds(test_delay_ms_),[&]{return stop_||!queue_.current(job.input_hash);});
                    if(stop_||!queue_.current(job.input_hash)){complete=false;break;}
                }
            }
            built.record_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()-built.wait_ms;
        }catch(const std::exception& e){std::lock_guard lock(mutex_);error_=e.what();complete=false;}
        {
            std::lock_guard lock(mutex_);
            const bool publish=queue_.finish(job.revision,job.input_hash,complete);
            if(publish)result_=std::move(built);else if(built.texture)SDL_ReleaseGPUTexture(device_,built.texture);
            std::ostringstream trace;trace<<"density_job_revision="<<job.revision<<" input_hash="<<job.input_hash<<" publish="<<publish<<" coalesced="<<queue_.coalesced<<" discarded="<<queue_.discarded<<" pending="<<queue_.pending()<<" running="<<queue_.running()<<" generation_cpu_elapsed_excluding_fences_ms="<<built.record_ms<<" generation_submit_and_fence_wall_ms="<<built.wait_ms<<'\n';telemetry_=trace.str();
        }
        cv_.notify_all();
    }
}
}
