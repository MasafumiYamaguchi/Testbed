#include "white/hdr_export.hpp"
#include "white/gpu_spike.hpp"
#include "white/editor_ui.hpp"
#include "white/benchmark.hpp"
#include <fstream>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <charconv>
#include <chrono>
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
    int frames=0,preset=2,render_width=160,view_steps=64,shadow_steps=8,cache=0,sun_cache=0;
    std::string recipe,benchmark_output="benchmark.csv",benchmark_track="density";
    int benchmark_updates=0;bool validation=true;
    unsigned progressive_samples=0,diagnostic_mode=0;
    bool empty_skip=false;
    std::string capture,hdr_output;
    bool lifecycle=false,self_test=false;
    for(int i=1;i<argc;++i) {
        std::string_view arg(argv[i]);
        if(arg=="--help") {
            std::cout<<"ProjectWhite --export-hdr NEW_DIRECTORY --frames N --capture output.bmp --self-test --lifecycle-test --scene 0..4\n"
                "--recipe FILE --render-width 64..640 --view-steps 8..512 --shadow-steps 1..64 --cache 0|128|256\n"
                "--benchmark-updates 30..10000 --benchmark-track density|camera|exposure --benchmark-output FILE --no-validation\n"
                "Windows D3D12 GPU field validation; startup fails if required GPU features are absent.\n"; return 0;
        }
        if(arg=="--no-validation"){validation=false;continue;}
        if(arg=="--benchmark-output"&&i+1<argc){benchmark_output=argv[++i];continue;}
        if(arg=="--benchmark-track"&&i+1<argc){benchmark_track=argv[++i];try{(void)white::edit_track(benchmark_track);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}continue;}
        if(arg=="--benchmark-updates"&&i+1<argc){std::string_view value(argv[++i]);auto r=std::from_chars(value.data(),value.data()+value.size(),benchmark_updates);if(r.ec==std::errc{}&&r.ptr==value.data()+value.size()&&benchmark_updates>=30&&benchmark_updates<=10000)continue;std::cerr<<"Benchmark updates must be 30..10000\n";return 2;}
        if(arg=="--recipe"&&i+1<argc){recipe=argv[++i];continue;}
        if((arg=="--render-width"||arg=="--view-steps"||arg=="--shadow-steps"||arg=="--cache")&&i+1<argc){
            std::string_view value(argv[++i]);int number=0;auto result=std::from_chars(value.data(),value.data()+value.size(),number);
            bool valid=result.ec==std::errc{}&&result.ptr==value.data()+value.size();
            if(arg=="--render-width"){valid=valid&&number>=64&&number<=640;render_width=number;}
            if(arg=="--view-steps"){valid=valid&&number>=8&&number<=512;view_steps=number;}
            if(arg=="--shadow-steps"){valid=valid&&number>=1&&number<=64;shadow_steps=number;}
            if(arg=="--cache"){valid=valid&&(number==0||number==128||number==256);cache=number;}
            if(!valid){std::cerr<<"Invalid quality/cache argument: "<<arg<<'\n';return 2;}continue;
        }
        if(arg=="--self-test") {self_test=true;continue;}
        if(arg=="--scene" && i+1<argc) {
            std::string_view value(argv[++i]);auto r=std::from_chars(value.data(),value.data()+value.size(),preset);
            if(r.ec==std::errc{}&&r.ptr==value.data()+value.size()&&preset>=0&&preset<=4)continue;
            std::cerr<<"--scene requires 0..4\n";return 2;
        }
        if(arg=="--lifecycle-test") {lifecycle=true;continue;}
        if(arg=="--diagnostic"&&i+1<argc){std::string v=argv[++i];auto r=std::from_chars(v.data(),v.data()+v.size(),diagnostic_mode);if(r.ec==std::errc{}&&r.ptr==v.data()+v.size()&&diagnostic_mode<=11)continue;std::cerr<<"--diagnostic requires 0..11\n";return 2;}
        if(arg=="--progressive"&&i+1<argc){std::string v=argv[++i];auto r=std::from_chars(v.data(),v.data()+v.size(),progressive_samples);if(r.ec==std::errc{}&&r.ptr==v.data()+v.size()&&progressive_samples>=1&&progressive_samples<=256)continue;std::cerr<<"--progressive requires 1..256\n";return 2;}
        if(arg=="--empty-skip"){empty_skip=true;continue;}
        if(arg=="--sun-cache"&&i+1<argc){std::string v=argv[++i];if(v=="0"||v=="32"||v=="64"){sun_cache=std::stoi(v);continue;}std::cerr<<"--sun-cache requires 0/32/64\n";return 2;}
        if(arg=="--export-hdr" && i+1<argc){hdr_output=argv[++i];continue;}
        if(arg=="--capture" && i+1<argc) {capture=argv[++i];continue;}
        if(arg=="--frames" && i+1<argc) {
            std::string_view text(argv[++i]);
            auto r=std::from_chars(text.data(),text.data()+text.size(),frames);
            if(r.ec==std::errc{} && r.ptr==text.data()+text.size() && frames>=90) continue;
            std::cerr<<"--frames requires an integer of at least 90 (capture/lifecycle warmup)\n";return 2;
        }
        std::cerr<<"Unknown or incomplete argument: "<<arg<<'\n';return 2;
    }
    if(!hdr_output.empty()&&(capture.empty()||!frames||self_test||lifecycle)){std::cerr<<"--export-hdr requires --capture and --frames, without self-test/lifecycle\n";return 2;}
    if(progressive_samples&&(self_test||benchmark_updates)){std::cerr<<"Progressive mode excludes fixed self-test/benchmark\n";return 2;}
    if(self_test&&frames>0&&frames<760){std::cerr<<"--self-test requires --frames >= 760 to finish UI checks\n";return 2;}
    if(self_test&&!recipe.empty()){std::cerr<<"Use separate runs for fixed Recipe and scripted UI self-test\n";return 2;}
    if(benchmark_updates){
        if(recipe.empty()||self_test||lifecycle||frames){std::cerr<<"Benchmark requires --recipe and excludes --frames, --self-test and --lifecycle-test\n";return 2;}
        frames=benchmark_updates+90;
    }
    if(!SDL_Init(SDL_INIT_VIDEO)) {std::cerr<<SDL_GetError()<<'\n';return 1;}
    int exit_code=0;
    try {
        white::GpuSpike gpu; gpu.initialize(validation);gpu.diagnostic_mode=diagnostic_mode;gpu.progressive=progressive_samples>0;if(progressive_samples)gpu.progressive_budget=progressive_samples;gpu.empty_skip=empty_skip;gpu.sun_cache_resolution=sun_cache;gpu.validate_optics();
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
        white::EditorUi editor(preset);
        if(!recipe.empty())editor.session.load(std::filesystem::u8path(recipe),true);
        gpu.internal_width=render_width;gpu.view_steps=view_steps;gpu.shadow_steps=shadow_steps;
        gpu.set_scene(editor.session.document().scene(),editor.session.document().revision());
        if(!recipe.empty())white::gpu_check(SDL_SetWindowSize(gpu.window,1110,540),"Set fixed 16:9 viewport");
        if(cache){gpu.set_cache_resolution(cache);gpu.use_cache=true;}
        if(!recipe.empty()){
            gpu.wait_bakes();
            std::cout<<"fixed_recipe="<<recipe<<" density_hash="<<white::density_input_hash(editor.session.document().scene())<<" internal_width="<<render_width<<" view_steps="<<view_steps<<" shadow_steps="<<shadow_steps<<" cache="<<cache<<" camera_sun=from_saved_recipe\n";
        }
        const auto benchmark_base=editor.session.document().scene();
        const auto track=white::edit_track(benchmark_track);
        std::vector<white::FrameMeasurement> measurements;measurements.reserve(benchmark_updates);
        std::uint64_t benchmark_initial_bakes=0;
        std::chrono::steady_clock::time_point benchmark_start;double benchmark_elapsed_ms=0;
        if(benchmark_updates)std::cout<<"benchmark_track="<<benchmark_track<<" warmup_frames=30 requested_updates="<<benchmark_updates<<" cells="<<benchmark_base.cloud.cells.size()<<" frames_in_flight=1 adaptive_quality=false artificial_frame_delay=false validation="<<validation<<'\n';
        bool running=true,captured=false,hdr_exported=false;
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
            const bool measuring=benchmark_updates&&frame>=30&&frame<30+benchmark_updates;
            const auto edit_start=std::chrono::steady_clock::now();
            if(measuring){
                if(frame==30){benchmark_initial_bakes=gpu.bake_count;benchmark_start=edit_start;}
                if(!editor.session.apply(white::benchmark_scene(benchmark_base,track,unsigned(frame-30))))throw std::runtime_error("Benchmark trajectory did not change Document");
            }
            if(self_test&&frame>=180&&frame<=600&&(frame-180)%20==0) {
                if(frame==400){gpu.internal_width=96;gpu.view_steps=32;gpu.shadow_steps=4;gpu.volume_dirty=true;}
                const int step=(frame-180)/20;
                if(step==18){gpu.set_cache_resolution(128);gpu.use_cache=true;gpu.volume_dirty=true;}
                if(step==19){gpu.set_cache_resolution(256);gpu.volume_dirty=true;}
                if(step==20){const auto count=gpu.bake_count;auto scene=editor.session.document().scene();scene.camera.position.x+=5;editor.session.apply(scene);gpu.set_scene(scene,editor.session.document().revision());if(gpu.bake_count!=count)throw std::runtime_error("Camera-only change rebaked density");std::cout<<"camera_only_bake_count_unchanged PASS\n";}
                if(step==21){const auto scene=editor.session.document().scene();const auto count=gpu.bake_count;bool rejected=false;try{gpu.create_field({512,512,512},2);}catch(const std::invalid_argument&){rejected=true;}if(!rejected||scene!=editor.session.document().scene()||count!=gpu.bake_count)throw std::runtime_error("Capacity rejection lost document/cache");std::cout<<"overbudget_document_and_cache_preserved PASS\n";}
                editor.scripted_edit(step);
            }
            if(self_test&&frame==630){gpu.use_cache=false;gpu.set_test_delay(30);}
            if(self_test&&frame>=630&&frame<690) {
                auto scene=editor.session.document().scene();scene.cloud.cells[0].center.x+=0.05;
                if(frame%5==0)scene.sun.direction_to_light.x*=-1;
                if(frame%7==0)scene.exposure_ev=scene.exposure_ev==1?1.5:1;
                editor.session.apply(scene);
                if(frame%11==0){editor.session.undo();editor.session.redo();}
                if(frame==650)white::gpu_check(SDL_SetWindowSize(gpu.window,800,520),"Stress resize smaller");
                if(frame==680)white::gpu_check(SDL_SetWindowSize(gpu.window,900,600),"Stress resize restore");
            }
            if(self_test&&frame==700){gpu.set_test_delay(0);gpu.use_cache=true;gpu.set_cache_resolution(128);gpu.internal_width=256;gpu.view_steps=96;gpu.shadow_steps=8;gpu.volume_dirty=true;}
            ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame();
            if(self_test)editor.scripted_input(frame);
            ImGui::NewFrame();
            editor.draw(gpu);
            gpu.poll_bakes();
            if(self_test&&frame>=630&&frame<690)gpu.set_interacting(true);
            if(benchmark_updates&&frame==benchmark_updates+60)gpu.wait_bakes();
            if(self_test&&frame==710)gpu.wait_bakes();
            if(self_test&&frame>=190&&frame<=610&&(frame-190)%20==0)gpu.wait_bakes();
            if(self_test&&gpu.scene_revision!=editor.session.document().revision())throw std::runtime_error("Self-test preview revision is stale");
            if(self_test)editor.verify_scripted_input(frame);
            ImGui::Render();
            bool hdr_work=false;
            const auto record_start=std::chrono::steady_clock::now();
            auto* cmd=SDL_AcquireGPUCommandBuffer(gpu.device);white::gpu_check(cmd != nullptr,"Acquire frame commands");
            SDL_GPUTexture* swap=nullptr;Uint32 w=0,h=0;
            if(!SDL_WaitAndAcquireGPUSwapchainTexture(cmd,gpu.window,&swap,&w,&h)) {
                SDL_CancelGPUCommandBuffer(cmd);white::gpu_check(false,"Acquire swapchain");
            }
            if(swap) {
                gpu.resize(w,h);gpu.prepare_preview();hdr_work=gpu.volume_dirty&&gpu.show_volume;gpu.draw(cmd,editor.slice,Uint32(editor.axis));
                ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(),cmd);
                SDL_GPUColorTargetInfo color{};color.texture=gpu.target;color.load_op=SDL_GPU_LOADOP_LOAD;color.store_op=SDL_GPU_STOREOP_STORE;
                auto* pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr);
                ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(),cmd,pass);SDL_EndGPURenderPass(pass);
                SDL_GPUBlitInfo blit{};blit.source.texture=gpu.target;blit.source.w=w;blit.source.h=h;
                blit.destination.texture=swap;blit.destination.w=w;blit.destination.h=h;
                blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_NEAREST;
                SDL_BlitGPUTexture(cmd,&blit);
            }
            if(self_test||!recipe.empty()) {
                const auto recorded=std::chrono::steady_clock::now();auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);white::gpu_check(fence!=nullptr,"Submit timed frame");
                const auto submitted=std::chrono::steady_clock::now();
                const bool waited=SDL_WaitForGPUFences(gpu.device,true,&fence,1);SDL_ReleaseGPUFence(gpu.device,fence);white::gpu_check(waited,"Wait timed frame");
                const auto completed=std::chrono::steady_clock::now();
                if(measuring){
                    if(!swap||gpu.rendered_revision!=editor.session.document().revision())throw std::runtime_error("Benchmark frame did not submit latest revision");
                    auto ms=[](auto start,auto end){return std::chrono::duration<double,std::milli>(end-start).count();};
                    if(frame==29+benchmark_updates)benchmark_elapsed_ms=std::chrono::duration<double,std::milli>(completed-benchmark_start).count();
                    measurements.push_back({gpu.rendered_revision,ms(record_start,recorded),ms(recorded,submitted),ms(submitted,completed),ms(edit_start,submitted),ms(edit_start,completed),gpu.hdr_width,gpu.hdr_height,gpu.rendered_from_cache(),hdr_work});
                }
                // Report first-presentation timing after capturing the measured
                // timestamps; console I/O must not inflate fence-wait samples.
                if(swap&&!benchmark_updates)gpu.note_present(gpu.rendered_revision,submitted);
                if(hdr_work&&!benchmark_updates)std::cout<<"frame_revision="<<gpu.scene_revision<<" mode="<<(gpu.use_cache&&gpu.cache_current()?"cache":"direct")<<" record_cpu_ms="<<std::chrono::duration<double,std::milli>(recorded-record_start).count()<<" submit_to_fence_wall_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-recorded).count()<<" gpu_timestamp_ms=unavailable\n";
            }else {white::gpu_check(SDL_SubmitGPUCommandBuffer(cmd),"Submit frame");if(swap)gpu.note_present(gpu.rendered_revision);}
            if(swap && !capture.empty() && !captured && (!progressive_samples||gpu.preview_state.samples()>=progressive_samples) && frame>=(benchmark_updates?benchmark_updates+60:60)) {
                gpu.save_capture(capture);captured=true;std::cout<<"capture="<<capture<<" frame="<<frame<<'\n';
                if(gpu.show_volume&&gpu.fixture==2) {
                    baseline_hdr=gpu.read_hdr();if(empty_skip)gpu.validate_majorant();if(sun_cache)gpu.validate_sun_cache();
                    if(!hdr_output.empty()){
                        if(gpu.rendered_revision!=editor.session.document().revision())throw std::runtime_error("Export frame revision mismatch");
                        white::export_hdr({gpu.hdr_width,gpu.hdr_height,baseline_hdr},{editor.session.document().scene(),std::uint64_t(frame),gpu.rendered_revision,unsigned(gpu.view_steps),unsigned(gpu.shadow_steps),gpu.rendered_from_cache(),unsigned(gpu.sun_cache_resolution),gpu.empty_skip,gpu.progressive?gpu.preview_state.samples():1,gpu.diagnostic_mode},std::filesystem::u8path(hdr_output));
                        hdr_exported=true;std::cout<<"HDR exported="<<hdr_output<<'\n';
                    }
                    if(!recipe.empty())std::cout<<"fixed_capture width="<<gpu.hdr_width<<" height="<<gpu.hdr_height<<" revision="<<gpu.rendered_revision<<" pending="<<gpu.bake_pending()<<'\n';
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
            if(self_test&&swap&&frame>=190&&frame<=610&&(frame-190)%20==0) {
                gpu.validate();if(gpu.use_cache)gpu.validate_cache_samples();(void)gpu.read_hdr();gpu.save_capture(std::filesystem::path(capture).parent_path()/("editor-step-"+std::to_string((frame-190)/20)+".bmp"));
            }
            if(self_test&&swap&&frame==730){gpu.wait_bakes();gpu.validate();gpu.save_capture(std::filesystem::path(capture).parent_path()/"stress-idle.bmp");std::cout<<"stress_final_revision="<<gpu.scene_revision<<" expected="<<editor.session.document().revision()<<" idle_width=256 view_steps=96 shadow_steps=8 pending="<<gpu.bake_pending()<<'\n';if(gpu.scene_revision!=editor.session.document().revision()||gpu.bake_pending())throw std::runtime_error("Stress did not settle latest revision");}
            if(!benchmark_updates)SDL_Delay(16);
        }
        if(benchmark_updates){
            if(measurements.size()!=std::size_t(benchmark_updates))throw std::runtime_error("Benchmark incomplete: window closed or frames skipped");
            if(track!=white::EditTrack::density&&gpu.bake_count!=benchmark_initial_bakes)throw std::runtime_error("View-only benchmark rebuilt density");
            std::ofstream output(std::filesystem::u8path(benchmark_output),std::ios::binary|std::ios::trunc);output<<white::benchmark_csv(measurements);output.close();if(!output)throw std::runtime_error("Cannot write benchmark CSV");
            std::cout<<white::benchmark_summary(measurements)<<"benchmark_wall_ms="<<benchmark_elapsed_ms<<" completed_update_hz="<<1000*benchmark_updates/benchmark_elapsed_ms<<'\n'<<"benchmark_bakes="<<gpu.bake_count-benchmark_initial_bakes<<" final_revision="<<gpu.scene_revision<<" requested_cache="<<cache<<" csv="<<benchmark_output<<'\n';
        }
        if(!hdr_output.empty()&&!hdr_exported)throw std::runtime_error("No valid HDR frame was exported");
        if(!capture.empty() && !captured) throw std::runtime_error("No valid frame was available for screenshot");
        std::cout<<"shutdown=clean lifecycle_test="<<lifecycle<<'\n';
    } catch(const std::exception& e) {std::cerr<<"ERROR: "<<e.what()<<'\n';exit_code=1;}
    SDL_Quit();return exit_code;
}
