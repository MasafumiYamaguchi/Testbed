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
#include "white/modifiers.hpp"
#include "white/cloud_presets.hpp"
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
    void start_preset_test();
    void preset_test_step(int frame,GpuSpike&);
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
    void start_modifier_test();
    void modifier_test_step(int frame);
    const std::string& status() const{return status_;}
private:
    Id finish_selected_=0;
    std::uint64_t modifier_test_jobs_=0,modifier_test_content_=0;
    void draw_modifier_ui();
    void finish_item(bool,const FinishModifier&);
    std::optional<Scene> generation_initial_,generation_guard_,generation_job_guard_,generation_draft_current_;
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
    void adopt_generation_candidate(bool reset_confirmed=false);
    void discard_generation_candidate();
    void undo_generation_candidate_discard();
    void clone_current_candidate();
    void clone_test_step(int frame,GpuSpike&);
    void draw_preset_ui();
    void prepare_generation_draft(GenerationDraft,bool resets_manual_edits);
    void publish_editor_preview(GpuSpike&);
    Scene generation_candidate_view()const;
    std::optional<Scene> generation_candidate_fixed_,generation_discarded_fixed_;
    bool candidate_preview_active()const{return generation_preview_candidate_&&generation_candidate_fixed_.has_value();}
    bool generation_preview_candidate_=false;
    bool generation_draft_resets_edits_=false,generation_job_resets_edits_=false,generation_candidate_resets_edits_=false;
    std::uint64_t generation_draft_token_=0,generation_job_draft_token_=0,generation_candidate_draft_token_=0,generation_discarded_draft_token_=0;
    bool generation_candidate_is_clone_=false,generation_discarded_is_clone_=false,generation_discarded_resets_edits_=false;
    std::optional<Scene> generation_preview_scene_;
    std::uint64_t generation_preview_epoch_=0;
    int preset_kind_=0,preset_wind_=0,variation_scope_=0;
    std::uint64_t preset_structure_seed_=42,preset_detail_seed_=17,variation_seed_=100;
    double preset_stage_=.8;
    Id variation_target_=0;
    std::string generation_preset_label_;
    std::uint64_t preset_test_jobs_=0;
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
