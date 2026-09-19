#pragma once
#include "white/gpu_spike.hpp"
#include "white/persistence.hpp"
#include "white/cumulonimbus.hpp"
#include "white/centerline_scene.hpp"
#include "white/developed_scene.hpp"
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
    void start_developed_test();
    void developed_test_input(int frame);
    void verify_developed_test(int frame);
    void start_centerline_test();
    void centerline_test_input(int frame);
    void verify_centerline_test(int frame);
    const std::string& status() const{return status_;}
private:
    Id development_=0;
    bool duplicate_regenerate_=false;
    void draw_developed_ui();
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
