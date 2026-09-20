#include "white/modifiers.hpp"
#include "white/frozen_cloud.hpp"
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
    candidate.finish_stack=previous.finish_stack;
    // Keep the same stable references and local coordinates, never reattach by
    // vector index. Validation diagnoses deleted fields before any publication.
    require_valid(candidate);return candidate;
}
}
