#pragma once
#include "white/anvil.hpp"
#include <span>

namespace white {
struct ModifierFieldScales {
    std::array<double,2> density{1,1},detail{1,1};
};
std::vector<std::string> validate_finish_stack(const FinishStack&,Id object_id,std::span<const Id> fields);
double ellipsoid_mask_weight(const EllipsoidMask&,Vec3 object_local);
ModifierFieldScales evaluate_finish_stack(const FinishStack&,Vec3 object_local,Id object_id,std::span<const Id> fields);
double finish_density_bound(const FinishStack&);
std::uint64_t finish_stack_hash(const FinishStack&);
// Final density bound includes the complete ordered stack. Targets resolve to
// bit masks only after stable-ID validation; missing references never retarget.
struct alignas(16) GpuFinishLayer {
    Float4 center{},radii{},operation{},target{};
};
struct alignas(16) GpuFinishStack {
    std::array<GpuFinishLayer,max_finish_modifiers> layers{};
    Float4 settings{}; // layer count, multiplicative density bound, reserved.
};
struct alignas(16) GpuFinishedParams {
    GpuAnvilParams field;
    GpuFinishStack finish;
};
static_assert(sizeof(GpuFinishLayer)==64);
static_assert(sizeof(GpuFinishStack)==272);
static_assert(sizeof(GpuFinishedParams)==2288);
GpuFinishStack gpu_finish_stack(const FinishStack&,Id object_id,std::span<const Id> fields);
enum class FinishCommandKind {add,replace,duplicate,erase,move_up,move_down};
struct FinishCommand {
    FinishCommandKind kind=FinishCommandKind::add;
    Id id=0;
    FinishModifier value;
};
// Pure stack edit: caller validates references against its immutable source.
FinishStack finish_stack_command(FinishStack,const FinishCommand&);
Id next_finish_id(const FinishStack&);
Scene scene_with_finish_command(Scene,const FinishCommand&);
Scene adopt_frozen_with_finish(const Scene& previous,Scene candidate);
}
