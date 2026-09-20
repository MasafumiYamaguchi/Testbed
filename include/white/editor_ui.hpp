#pragma once
#include "white/gpu_spike.hpp"
#include "white/persistence.hpp"
#include "white/cumulonimbus.hpp"
#include "white/centerline_scene.hpp"
#include "white/developed_scene.hpp"
#include "white/top_lobe_scene.hpp"
#include "white/generation.hpp"
#include "white/frozen_cloud.hpp"
#include "white/anvil_scene.hpp"
#include <atomic>
#include <future>
namespace white {
class EditorUi {
public:
    explicit EditorUi(int preset):session(fixture_scene(preset)){}
    EditorSession session;
    float slice=0.5f;
    int axis=2;
    void draw(GpuSpike&);
    void scripted_edit(int step);
    void scripted_input(int frame);
    void verify_scripted_input(int frame);
    void start_prefab_test();
    void prefab_test_input(int frame);
    void verify_prefab_test(int frame);
    void start_freeze_test();
    void freeze_test_step(int frame);
    void start_top_lobes_test();
    void top_lobes_test_step(int frame);
    void start_anvil_test();
    void anvil_test_input(int frame);
    void verify_anvil_test(int frame);
    void start_developed_test();
    void developed_test_input(int frame);
    void verify_developed_test(int frame);
    void start_centerline_test();
    void centerline_test_input(int frame);
    void verify_centerline_test(int frame);
    const std::string& status() const{return status_;}
private:
    std::optional<Scene> generation_initial_,generation_guard_,generation_job_guard_;
    GenerationSettings generation_settings_;
    std::optional<GenerationCandidate> generation_candidate_;
    std::future<GenerationOutcome> generation_job_;
    std::stop_source generation_stop_;
    std::shared_ptr<std::atomic<double>> generation_progress_;
    std::string generation_message_;
    std::uint64_t generation_runs_=0;
    void draw_generation_ui();
    void draw_frozen_ui();
    void launch_generation(bool cancel_before_start=false);
    void adopt_generation_candidate();
    std::optional<GenerationCandidate> generation_discarded_;
    std::optional<Scene> generation_discarded_guard_;
    std::uint64_t freeze_test_jobs_=0;
    void poll_generation();
    Id development_=0;
    bool duplicate_regenerate_=false;
    void draw_developed_ui();
    void draw_top_lobes_ui();
    void top_lobe_item(bool,const TopLobeSettings&);
    int anvil_handle_=0;
    void draw_anvil_ui();
    void anvil_item(bool,const AnvilSettings&);
    void draw_anvil_gizmo(float,float,float,float);
    void developed_item(bool,const DevelopedCommand&);
    Id curve_point_=0;
    bool prefab_group_=true;
    bool focus_noise_=false;
    std::uint64_t last_scene_attempt_=0;
    Scene smoke_original_{},smoke_before_{};
    float smoke_x_=0,smoke_y_=0;
    Id selected_=2;
    bool select_cuts_=false,scale_=false,gizmo_drag_=false,inspector_drag_=false,orbit_drag_=false;
    bool inspector_transport_=true;
    char filename_[512]="cloud.white.json";
    std::string status_="Select a cell; drag the gizmo. Right-drag to orbit.";
    void apply(Scene);
    void inspector_item(bool changed,Scene,bool affects_transport=true);
    void camera_preset(int direction);
    void prefab_item(bool,const CumulonimbusCommand&);
    void centerline_item(bool,const CenterlineCommand&);
};
}
