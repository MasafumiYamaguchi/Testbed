#include "white/cell_actions_ui.hpp"
#include "white/cumulonimbus_cells.hpp"
#include <imgui.h>
#include <algorithm>
#include <exception>
#include <stdexcept>

namespace white {
void draw_cell_actions_ui(EditorSession& session,Id& selected,bool enabled,std::string& status) {
    const auto& scene=session.document().scene();
    const auto* source=scene.centerline?&scene.centerline->source:scene.cumulonimbus?&*scene.cumulonimbus:nullptr;
    if(!source)return;
    const auto found=std::find_if(scene.cloud.cells.begin(),scene.cloud.cells.end(),[&](const Cell& c){return c.id==selected;});
    if(found==scene.cloud.cells.end())return;
    // Preserve all data needed below before any session publication invalidates
    // references to the previous Scene.
    const auto current_seed=found->structure_seed;
    const auto next_seed=current_seed+UINT64_C(0x9e3779b97f4a7c15);
    const auto capabilities=cumulonimbus_cell_capabilities(*source,selected);
    ImGui::PushID("Cell actions");
    ImGui::Text("Local seed: %llu",static_cast<unsigned long long>(current_seed));
    const auto policy_key=ImGui::GetID("Duplicate seed policy");
    int policy=ImGui::GetStateStorage()->GetInt(policy_key,0);
    ImGui::BeginDisabled(!enabled);
    ImGui::RadioButton("Keep seed value",&policy,0);
    ImGui::RadioButton("New seed value",&policy,1);
    ImGui::GetStateStorage()->SetInt(policy_key,policy);
    if(ImGui::Button("Duplicate selected cell")) {
        try {
            const auto& current=session.document().scene();
            const auto& group=current.centerline?current.centerline->source:*current.cumulonimbus;
            const Id id=next_cumulonimbus_cell_id(group);
            const CumulonimbusDuplicateCell command{selected,id,policy?CumulonimbusDuplicateSeed::regenerate:CumulonimbusDuplicateSeed::retain,next_seed};
            session.apply(scene_with_cumulonimbus_cell_command(current,command));selected=id;
            status="Duplicated as a manual cell; Undo restores the previous source";
        } catch(const std::exception& error){status=error.what();}
    }
    if(ImGui::Button("Reseed selected cell")) {
        try {
            const auto& current=session.document().scene();
            const auto selected_cell=std::find_if(current.cloud.cells.begin(),current.cloud.cells.end(),[&](const Cell& c){return c.id==selected;});
            if(selected_cell==current.cloud.cells.end())throw std::invalid_argument("Selected cell no longer exists");
            const auto seed=selected_cell->structure_seed+UINT64_C(0x9e3779b97f4a7c15);
            session.apply(scene_with_cumulonimbus_cell_command(current,CumulonimbusReseedCell{selected,seed}));
            status="Selected cell reseeded; global and other local seeds retained";
        } catch(const std::exception& error){status=error.what();}
    }
    ImGui::EndDisabled();
    ImGui::Text("Manual slots remaining: %u",static_cast<unsigned>(capabilities.remaining_manual_slots));
    ImGui::TextWrapped("Duplicate captures this ellipsoid as a manual cell. The cloud's density profile still applies. New IDs get a new random pattern even when the seed value is kept.");
    ImGui::PopID();
}
}
