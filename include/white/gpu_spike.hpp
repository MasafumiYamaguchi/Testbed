#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "white/density.hpp"
#include "white/gpu_bake_worker.hpp"
#include <memory>
#include <chrono>

namespace white {
class GpuSpike {
public:
    SDL_GPUDevice* device = nullptr;
    SDL_Window* window = nullptr;
    SDL_GPUTexture* field = nullptr;
    SDL_GPUTexture* target = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUGraphicsPipeline* display = nullptr;
    SDL_GPUComputePipeline* generate = nullptr;
    SDL_GPUComputePipeline* sample = nullptr;
    SDL_GPUComputePipeline* cache_sample_test=nullptr;
    SDL_GPUComputePipeline* optical_test = nullptr;
    SDL_GPUGraphicsPipeline* volume_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* tonemap_pipeline = nullptr;
    SDL_GPUTexture* hdr = nullptr;
    SDL_GPUSampler* point_sampler = nullptr;
    bool show_volume = true, volume_dirty = true,use_cache=false;
    int cache_resolution=128;
    std::uint64_t bake_count=0,estimated_gpu_bytes=0;
    double bake_record_ms=0,bake_wait_ms=0;
    void set_cache_resolution(int resolution);
    int sun_cache_resolution=0; // 0 direct, 32/64 tau cache
    unsigned sun_cache_builds=0;
    void validate_sun_cache();
    int view_steps = 64, shadow_steps = 8, internal_width = 160;
    float sun_angle = -40, exposure_ev = 1;
    Uint32 hdr_width=0,hdr_height=0;
    std::array<Uint32, 3> extent{17,19,23};
    Uint32 width = 0, height = 0, fixture = 0;
    float max_error = 0, interpolation_error = 0;
    std::string report;
    bool claimed = false;
    int cloud_preset = 2;
    GpuSpike();
    GpuSpike(const GpuSpike&) = delete;
    GpuSpike& operator=(const GpuSpike&) = delete;
    ~GpuSpike();
    void initialize(bool validation=true);
    void create_field(std::array<Uint32,3> dims, Uint32 kind);
    void create_cloud(int preset);
    void set_scene(const Scene&,std::uint64_t revision,std::chrono::steady_clock::time_point accepted=std::chrono::steady_clock::now());
    std::uint64_t scene_revision=0;
    void validate();
    void poll_bakes();
    void wait_bakes();
    void set_interacting(bool);
    void note_present(std::uint64_t revision,std::chrono::steady_clock::time_point submitted=std::chrono::steady_clock::now());
    bool bake_pending()const;
    bool cache_current()const;
    bool rendered_from_cache()const{return rendered_cache_;}
    void set_test_delay(unsigned milliseconds);
    std::uint64_t rendered_revision=0;
    void resize(Uint32 w, Uint32 h);
    void draw(SDL_GPUCommandBuffer* cmd, float slice, Uint32 axis);
    void save_capture(const std::filesystem::path& path);
    void validate_optics();
    void validate_cache_samples();
    std::vector<float> read_hdr();
private:
    SDL_GPUTexture* sun_tau_=nullptr;
    SDL_GPUComputePipeline* sun_generate_=nullptr;
    std::uint64_t sun_key_=0;
    int sun_extent_=0;
    bool rendered_sun_=false;
    Scene scene_snapshot_{};
    std::unique_ptr<GpuBakeWorker> bake_worker_;
    std::uint64_t field_density_hash_=0;
    std::uint64_t validated_bake_=~std::uint64_t(0),last_present_revision_=0;
    std::chrono::steady_clock::time_point accepted_{};
    std::vector<double> latency_samples_;
    bool interacting_=false,rendered_cache_=false;
    int rendered_steps_=64;
    void queue_bake(std::array<Uint32,3>);
    std::vector<float> cache_reference_;
    std::vector<Uint8> shader(const char* name);
    void initialize_volume();
    void render_volume(SDL_GPUCommandBuffer* cmd);
};
void gpu_check(bool success, const char* operation);
}
