#pragma once
#include "white/revision_queue.hpp"
#include <SDL3/SDL.h>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace white {
struct BakedDensity {
    SDL_GPUTexture* texture=nullptr;
    DensityJob source;
    double record_ms=0,wait_ms=0;
    std::uint64_t estimated_gpu_bytes=0;
};
class GpuBakeWorker {
public:
    GpuBakeWorker(SDL_GPUDevice*,SDL_GPUComputePipeline*);
    ~GpuBakeWorker();
    void request(DensityJob);
    std::optional<BakedDensity> take();
    void wait_idle();
    bool busy()const;
    std::string take_error();
    std::string take_telemetry();
    void set_test_delay(unsigned milliseconds);
private:
    void run();
    SDL_GPUDevice* device_;SDL_GPUComputePipeline* pipeline_;
    mutable std::mutex mutex_;std::condition_variable cv_;
    RevisionQueue queue_;
    std::optional<BakedDensity> result_;
    std::string error_,telemetry_;
    bool stop_=false;
    unsigned test_delay_ms_=0;
    std::thread thread_;
};
}
