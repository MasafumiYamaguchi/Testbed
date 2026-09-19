#include "white/gpu_spike.hpp"
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <charconv>
#include <iostream>
#include <string_view>

namespace {
struct Ui {
    bool platform=false,renderer=false,context=false;
    SDL_GPUDevice* device=nullptr;
    ~Ui() {
        if(device) SDL_WaitForGPUIdle(device);
        if(renderer) ImGui_ImplSDLGPU3_Shutdown();
        if(platform) ImGui_ImplSDL3_Shutdown();
        if(context) ImGui::DestroyContext();
    }
    void init(white::GpuSpike& gpu) {
        device=gpu.device;
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); context=true;
        ImGui::GetIO().IniFilename=nullptr;
        ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        platform=ImGui_ImplSDL3_InitForSDLGPU(gpu.window);
        white::gpu_check(platform,"Initialize ImGui platform");
        ImGui_ImplSDLGPU3_InitInfo info{};
        info.Device=device; info.ColorTargetFormat=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.MSAASamples=SDL_GPU_SAMPLECOUNT_1;
        renderer=ImGui_ImplSDLGPU3_Init(&info);
        white::gpu_check(renderer,"Initialize ImGui renderer");
    }
};
}
int main(int argc,char** argv) {
    int frames=0;
    std::string capture;
    bool lifecycle=false,self_test=false;
    for(int i=1;i<argc;++i) {
        std::string_view arg(argv[i]);
        if(arg=="--help") {
            std::cout<<"ProjectWhite --frames N --capture output.bmp --self-test --lifecycle-test\n"
                "Windows D3D12 GPU field validation; startup fails if required GPU features are absent.\n"; return 0;
        }
        if(arg=="--self-test") {self_test=true;continue;}
        if(arg=="--lifecycle-test") {lifecycle=true;continue;}
        if(arg=="--capture" && i+1<argc) {capture=argv[++i];continue;}
        if(arg=="--frames" && i+1<argc) {
            std::string_view text(argv[++i]);
            auto r=std::from_chars(text.data(),text.data()+text.size(),frames);
            if(r.ec==std::errc{} && r.ptr==text.data()+text.size() && frames>=90) continue;
            std::cerr<<"--frames requires an integer of at least 90 (capture/lifecycle warmup)\n";return 2;
        }
        std::cerr<<"Unknown or incomplete argument: "<<arg<<'\n';return 2;
    }
    if(!SDL_Init(SDL_INIT_VIDEO)) {std::cerr<<SDL_GetError()<<'\n';return 1;}
    int exit_code=0;
    try {
        white::GpuSpike gpu; gpu.initialize();
        if(self_test) {
            for(auto dims:{std::array<Uint32,3>{1,1,1},{17,19,23},{32,32,32}}) {
                for(Uint32 fixture=0;fixture<2;++fixture) {gpu.create_field(dims,fixture);gpu.validate();}
            }
        }
        gpu.create_field({17,19,23},0);gpu.validate();
        Ui ui;ui.init(gpu);
        float slice=0.5f;int axis=2,kind=0;
        bool running=true,captured=false;
        for(int frame=0;running && (frames==0 || frame<frames);++frame) {
            if(lifecycle) {
                if(frame==10) white::gpu_check(SDL_SetWindowSize(gpu.window,800,520),"Resize smaller");
                if(frame==20) white::gpu_check(SDL_MinimizeWindow(gpu.window),"Minimize");
                if(frame==30) white::gpu_check(SDL_RestoreWindow(gpu.window),"Restore");
                if(frame==40) white::gpu_check(SDL_SetWindowSize(gpu.window,900,600),"Resize original");
            }
            SDL_Event e;
            while(SDL_PollEvent(&e)) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if(e.type==SDL_EVENT_QUIT || e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) running=false;
            }
            if(SDL_GetWindowFlags(gpu.window)&SDL_WINDOW_MINIMIZED) {SDL_Delay(16);continue;}
            ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
            ImGui::SetNextWindowPos({15,15},ImGuiCond_Always);
            ImGui::SetNextWindowSize({260,560},ImGuiCond_Always);
            ImGui::Begin("Field laboratory",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
            ImGui::Text("PROJECT WHITE / PHASE 0");
            ImGui::Separator(); ImGui::TextWrapped("GPU-generated 3D scalar field");
            ImGui::Text("17 x 19 x 23 / R32 float");
            ImGui::Text("Backend: %s",SDL_GetGPUDeviceDriver(gpu.device));
            ImGui::Spacing();
            if(ImGui::Combo("Fixture",&kind,"XYZ ramp\0Center impulse\0")) {gpu.create_field({17,19,23},Uint32(kind));gpu.validate();}
            ImGui::Combo("Slice axis",&axis,"X (YZ)\0Y (XZ)\0Z (XY)\0");
            ImGui::SliderFloat("Position",&slice,0,1,"%.3f");
            if(ImGui::Button("Regenerate and verify")) {gpu.create_field(gpu.extent,gpu.fixture);gpu.validate();}
            ImGui::Spacing();ImGui::Separator();
            ImGui::TextColored({0.4f,0.95f,0.7f,1},"Readback verified");
            ImGui::TextWrapped("%s",gpu.report.c_str());
            ImGui::Text("Max error: %.8f",gpu.max_error);
            if(kind==0) ImGui::Text("Filter error: %.8f",gpu.interpolation_error);
            ImGui::TextWrapped("Row pitch: 256 bytes\nCoordinate: voxel centers\nSampling: linear / clamp");
            ImGui::Spacing();ImGui::Separator();
            ImGui::TextWrapped("This is a technical fixture. Cloud shapes and editing arrive in later issues.");
            ImGui::TextWrapped("Hosted CI results do not establish RTX performance.");
            ImGui::End();
            ImGui::SetNextWindowPos({290,15},ImGuiCond_Always);
            ImGui::SetNextWindowSize({580,40},ImGuiCond_Always);
            ImGui::Begin("Slice label",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoInputs);
            ImGui::Text("%s / %s plane / %.3f",kind==0?"XYZ RAMP":"IMPULSE",axis==0?"YZ":axis==1?"XZ":"XY",slice);
            ImGui::End(); ImGui::Render();
            auto* cmd=SDL_AcquireGPUCommandBuffer(gpu.device);white::gpu_check(cmd != nullptr,"Acquire frame commands");
            SDL_GPUTexture* swap=nullptr;Uint32 w=0,h=0;
            if(!SDL_WaitAndAcquireGPUSwapchainTexture(cmd,gpu.window,&swap,&w,&h)) {
                SDL_CancelGPUCommandBuffer(cmd);white::gpu_check(false,"Acquire swapchain");
            }
            if(swap) {
                gpu.resize(w,h);gpu.draw(cmd,slice,Uint32(axis));
                ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(),cmd);
                SDL_GPUColorTargetInfo color{};color.texture=gpu.target;color.load_op=SDL_GPU_LOADOP_LOAD;color.store_op=SDL_GPU_STOREOP_STORE;
                auto* pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr);
                ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(),cmd,pass);SDL_EndGPURenderPass(pass);
                SDL_GPUBlitInfo blit{};blit.source.texture=gpu.target;blit.source.w=w;blit.source.h=h;
                blit.destination.texture=swap;blit.destination.w=w;blit.destination.h=h;
                blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_NEAREST;
                SDL_BlitGPUTexture(cmd,&blit);
            }
            white::gpu_check(SDL_SubmitGPUCommandBuffer(cmd),"Submit frame");
            if(swap && !capture.empty() && !captured && frame>=60) {gpu.save_capture(capture);captured=true;std::cout<<"capture="<<capture<<" frame="<<frame<<'\n';}
            SDL_Delay(16);
        }
        if(!capture.empty() && !captured) throw std::runtime_error("No valid frame was available for screenshot");
        std::cout<<"shutdown=clean lifecycle_test="<<lifecycle<<'\n';
    } catch(const std::exception& e) {std::cerr<<"ERROR: "<<e.what()<<'\n';exit_code=1;}
    SDL_Quit();return exit_code;
}
