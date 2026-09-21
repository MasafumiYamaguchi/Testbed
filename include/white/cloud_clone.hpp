#pragma once
#include "white/frozen_cloud.hpp"
#include <optional>
#include <span>

namespace white {
struct IdRemap {
    Id from=0,to=0;
    bool operator==(const IdRemap&)const=default;
};
struct FrozenCloneOptions {
    // Omitted: allocate one monotone range above every source/reserved ID.
    // Explicit ranges must be nonzero, disjoint and large enough without wrap.
    std::optional<Id> first_id;
    std::vector<Id> reserved_ids;
};
struct FrozenCloneResult {
    Scene scene;
    std::vector<IdRemap> ids; // Sorted by original ID, including provenance IDs.
    std::vector<IdRemap> finish_ids; // Independent namespace; never object targets.
};
Id remap_cloned_id(std::span<const IdRemap>,Id);
// Deep copy the present evaluated state without a growth job. Fresh monotone
// IDs preserve merge order; local seed compensation preserves every warp key.
// Remapped provenance stays available as input for a new explicit generation;
// its fresh identities define an independent future random stream.
FrozenCloneResult clone_frozen_cloud(const Scene&,const FrozenCloneOptions& = {});
}
