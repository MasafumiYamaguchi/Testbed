#include "white/gpu_spike.hpp"
#include "white/editor_ui.hpp"
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <charconv>
#include <iostream>
#include <string_view>
#include <cmath>
#include <algorithm>

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
    int frames=0,preset=2;
    std::string capture;
    bool lifecycle=false,self_test=false;
    for(int i=1;i<argc;++i) {
        std::string_view arg(argv[i]);
        if(arg=="--help") {
            std::cout<<"ProjectWhite --frames N --capture output.bmp --self-test --lifecycle-test --scene 0..4\n"
                "Windows D3D12 GPU field validation; startup fails if required GPU features are absent.\n"; return 0;
        }
        if(arg=="--self-test") {self_test=true;continue;}
        if(arg=="--scene" && i+1<argc) {
            std::string_view value(argv[++i]);auto r=std::from_chars(value.data(),value.data()+value.size(),preset);
            if(r.ec==std::errc{}&&r.ptr==value.data()+value.size()&&preset>=0&&preset<=4)continue;
            std::cerr<<"--scene requires 0..4\n";return 2;
        }
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
    if(self_test&&frames>0&&frames<520){std::cerr<<"--self-test requires --frames >= 520 to finish UI checks\n";return 2;}
    if(!SDL_Init(SDL_INIT_VIDEO)) {std::cerr<<SDL_GetError()<<'\n';return 1;}
    int exit_code=0;
    try {
        white::GpuSpike gpu; gpu.initialize();gpu.validate_optics();
        if(self_test) {
            for(auto dims:{std::array<Uint32,3>{1,1,1},{17,19,23},{32,32,32}}) {
                for(Uint32 fixture=0;fixture<2;++fixture) {gpu.create_field(dims,fixture);gpu.validate();}
            }
            // Stress replacement and fenced readback without growing resource
            // ownership; this is bounded and does not replace long-run testing.
            for(int repeat=0;repeat<16;++repeat) {
                gpu.create_field({17,19,23},Uint32(repeat%2));gpu.validate();
            }
        }
        if(self_test)for(int test=0;test<5;++test)gpu.create_cloud(test);
        gpu.create_cloud(preset);
        Ui ui;ui.init(gpu);
        white::EditorUi editor(preset);gpu.set_scene(editor.session.document().scene(),editor.session.document().revision());
        bool running=true,captured=false;
        std::vector<float> baseline_hdr;
        int convergence_frame=-1;
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
            if(self_test&&frame>=180&&frame<=500&&(frame-180)%20==0) {
                if(frame==400){gpu.internal_width=96;gpu.view_steps=32;gpu.shadow_steps=4;gpu.volume_dirty=true;}
                editor.scripted_edit((frame-180)/20);
            }
            ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame();
            if(self_test)editor.scripted_input(frame);
            ImGui::NewFrame();
            editor.draw(gpu);
            if(self_test)editor.verify_scripted_input(frame);
            ImGui::Render();
            auto* cmd=SDL_AcquireGPUCommandBuffer(gpu.device);white::gpu_check(cmd != nullptr,"Acquire frame commands");
            SDL_GPUTexture* swap=nullptr;Uint32 w=0,h=0;
            if(!SDL_WaitAndAcquireGPUSwapchainTexture(cmd,gpu.window,&swap,&w,&h)) {
                SDL_CancelGPUCommandBuffer(cmd);white::gpu_check(false,"Acquire swapchain");
            }
            if(swap) {
                gpu.resize(w,h);gpu.draw(cmd,editor.slice,Uint32(editor.axis));
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
            if(swap && !capture.empty() && !captured && frame>=60) {
                gpu.save_capture(capture);captured=true;std::cout<<"capture="<<capture<<" frame="<<frame<<'\n';
                if(gpu.show_volume&&gpu.fixture==2) {
                    baseline_hdr=gpu.read_hdr();
                    if(self_test){gpu.view_steps*=2;gpu.volume_dirty=true;convergence_frame=frame;}
                }
            }
            if(swap && convergence_frame>=0 && frame>convergence_frame) {
                const auto finer=gpu.read_hdr();double sum=0,maximum=0;
                for(size_t i=0;i<finer.size();++i){double error=std::abs(double(finer[i])-baseline_hdr[i]);sum+=error;maximum=std::max(maximum,error);}
                std::cout<<"Step convergence 64->128 HDR+T max_abs_difference="<<maximum<<" mean_abs_difference="<<sum/finer.size()<<" (measured, not a proof of convergence)\n";
                gpu.save_capture(std::filesystem::path(capture).parent_path()/"step-half.bmp");
                gpu.view_steps/=2;gpu.volume_dirty=true;convergence_frame=-1;baseline_hdr.clear();
            }
            if(self_test&&swap&&(frame==116||frame==156))gpu.save_capture(std::filesystem::path(capture).parent_path()/(frame==116?"gizmo-move.bmp":"gizmo-scale.bmp"));
            if(self_test&&swap&&frame>=190&&frame<=510&&(frame-190)%20==0) {
                gpu.validate();(void)gpu.read_hdr();gpu.save_capture(std::filesystem::path(capture).parent_path()/("editor-step-"+std::to_string((frame-190)/20)+".bmp"));
            }
            SDL_Delay(16);
        }
        if(!capture.empty() && !captured) throw std::runtime_error("No valid frame was available for screenshot");
        std::cout<<"shutdown=clean lifecycle_test="<<lifecycle<<'\n';
    } catch(const std::exception& e) {std::cerr<<"ERROR: "<<e.what()<<'\n';exit_code=1;}
    SDL_Quit();return exit_code;
}
