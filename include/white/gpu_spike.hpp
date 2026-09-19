#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "white/density.hpp"

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
    SDL_GPUComputePipeline* optical_test = nullptr;
    SDL_GPUGraphicsPipeline* volume_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* tonemap_pipeline = nullptr;
    SDL_GPUTexture* hdr = nullptr;
    SDL_GPUSampler* point_sampler = nullptr;
    bool show_volume = true, volume_dirty = true;
    int view_steps = 64, shadow_steps = 8, internal_width = 160;
    float sun_angle = -40, exposure_ev = 1;
    Uint32 hdr_width=0,hdr_height=0;
    std::array<Uint32, 3> extent{17,19,23};
    Uint32 width = 0, height = 0, fixture = 0;
    float max_error = 0, interpolation_error = 0;
    std::string report;
    bool claimed = false;
    int cloud_preset = 2;
    GpuSpike() = default;
    GpuSpike(const GpuSpike&) = delete;
    GpuSpike& operator=(const GpuSpike&) = delete;
    ~GpuSpike();
    void initialize();
    void create_field(std::array<Uint32,3> dims, Uint32 kind);
    void create_cloud(int preset);
    void validate();
    void resize(Uint32 w, Uint32 h);
    void draw(SDL_GPUCommandBuffer* cmd, float slice, Uint32 axis);
    void save_capture(const std::filesystem::path& path);
    void validate_optics();
    std::vector<float> read_hdr();
private:
    std::vector<Uint8> shader(const char* name);
    void initialize_volume();
    void render_volume(SDL_GPUCommandBuffer* cmd);
};
void gpu_check(bool success, const char* operation);
}
