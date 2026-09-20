#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

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
    std::array<Uint32, 3> extent{17,19,23};
    Uint32 width = 0, height = 0, fixture = 0;
    float max_error = 0, interpolation_error = 0;
    std::string report;
    bool claimed = false;
    GpuSpike() = default;
    GpuSpike(const GpuSpike&) = delete;
    GpuSpike& operator=(const GpuSpike&) = delete;
    ~GpuSpike();
    void initialize();
    void create_field(std::array<Uint32,3> dims, Uint32 kind);
    void validate();
    void resize(Uint32 w, Uint32 h);
    void draw(SDL_GPUCommandBuffer* cmd, float slice, Uint32 axis);
    void save_capture(const std::filesystem::path& path);
private:
    std::vector<Uint8> shader(const char* name);
};
void gpu_check(bool success, const char* operation);
}
