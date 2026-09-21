#include "white/modifiers.hpp"
#include "white/frozen_cloud.hpp"
#include <algorithm>
#include <stdexcept>

namespace white {
Scene scene_with_finish_command(Scene scene,const FinishCommand& command){
    require_valid(scene);if(!scene.frozen)throw std::invalid_argument("Freeze a completed cloud before adding finishing layers");
    scene.finish_stack=finish_stack_command(std::move(scene.finish_stack),command);
    require_valid(scene);return scene;
}
Scene adopt_frozen_with_finish(const Scene& previous,Scene candidate){
    require_valid(previous);require_valid(candidate);
    if(!candidate.frozen)throw std::invalid_argument("Finishing layers can be retained only on a frozen candidate");
    if(!candidate.finish_stack.layers.empty())throw std::invalid_argument("Candidate already owns a finishing stack; explicit reconciliation required");
    auto role=[](const FrozenCloudState& state,Id id){
        if(state.top_enabled&&state.fields.size()==2&&state.fields[1].development_id==id)return 2;
        if(std::any_of(state.curves.begin(),state.curves.end(),[&](const auto& curve){return curve.development_id==id;}))return 1;
        return 0;
    };
    candidate.finish_stack=previous.finish_stack;
    // Keep the same stable references and local coordinates, never reattach by
    // vector index. Validation diagnoses deleted fields before any publication.
    require_valid(candidate);
    if(previous.frozen)for(const auto& layer:previous.finish_stack.layers)if(layer.target_kind==FinishTargetKind::field&&role(*previous.frozen,layer.target_id)!=role(*candidate.frozen,layer.target_id))
        throw std::invalid_argument("Finishing target changed field role; clear or explicitly retarget layers before adopting. The current cloud is retained.");
    return candidate;
}
}
