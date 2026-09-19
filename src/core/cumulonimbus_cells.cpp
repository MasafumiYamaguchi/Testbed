#include "white/cumulonimbus_cells.hpp"
#include "white/centerline.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace white {
namespace {
constexpr std::size_t max_cells=8;
bool generated(const CumulonimbusGroup& group,Id id) {
    return std::find(group.cell_ids.begin(),group.cell_ids.end(),id)!=group.cell_ids.end();
}
const Cell& find_cell(const CloudRecipe& recipe,Id id) {
    const auto found=std::find_if(recipe.cells.begin(),recipe.cells.end(),[&](const Cell& c){return c.id==id;});
    if(found==recipe.cells.end())throw std::invalid_argument("Unknown Cumulonimbus Cell ID");
    return *found;
}
void new_id(const CloudRecipe& recipe,Id id) {
    if(id==0||id==recipe.id||
       std::any_of(recipe.cells.begin(),recipe.cells.end(),[&](const Cell& c){return c.id==id;})||
       std::any_of(recipe.cuts.begin(),recipe.cuts.end(),[&](const Cut& c){return c.id==id;}))
        throw std::invalid_argument("New Cell ID must be nonzero and unused by cloud, cells or cuts");
    if(recipe.cells.size()>=max_cells)throw std::invalid_argument("Cumulonimbus v1 supports at most eight cells: five generated and three manual");
}
void canonicalize(CumulonimbusGroup& group) {
    std::sort(group.cell_adjustments.begin(),group.cell_adjustments.end(),[](const auto& a,const auto& b){return a.cell_id<b.cell_id;});
    std::sort(group.modifiers.manual_cells.begin(),group.modifiers.manual_cells.end(),[](const Cell& a,const Cell& b){return a.id<b.id;});
}
CumulonimbusCellAdjustment& adjustment(CumulonimbusGroup& group,Id id) {
    auto found=std::find_if(group.cell_adjustments.begin(),group.cell_adjustments.end(),[&](const auto& a){return a.cell_id==id;});
    if(found==group.cell_adjustments.end()){group.cell_adjustments.push_back({id});return group.cell_adjustments.back();}
    return *found;
}
Cell& manual(CumulonimbusGroup& group,Id id) {
    auto found=std::find_if(group.modifiers.manual_cells.begin(),group.modifiers.manual_cells.end(),[&](const Cell& c){return c.id==id;});
    if(found==group.modifiers.manual_cells.end())throw std::invalid_argument("Unknown manual Cell ID");
    return *found;
}
}
CumulonimbusCellCapabilities cumulonimbus_cell_capabilities(const CumulonimbusGroup& group,Id id) {
    const auto recipe=derive_cumulonimbus_recipe(group);(void)find_cell(recipe,id);
    const bool is_generated=generated(group,id);
    return {is_generated?CumulonimbusCellKind::generated:CumulonimbusCellKind::manual,!is_generated,max_cells-recipe.cells.size()};
}
Id next_cumulonimbus_cell_id(const CumulonimbusGroup& group) {
    const auto recipe=derive_cumulonimbus_recipe(group);Id maximum=recipe.id;
    for(const auto& c:recipe.cells)maximum=std::max(maximum,c.id);
    for(const auto& c:recipe.cuts)maximum=std::max(maximum,c.id);
    if(maximum==std::numeric_limits<Id>::max())throw std::overflow_error("Stable Cell ID namespace exhausted");
    return maximum+1;
}
std::optional<Id> cumulonimbus_cell_selection(const CumulonimbusGroup& group,std::optional<Id> selected) {
    const auto recipe=derive_cumulonimbus_recipe(group);
    if(selected&&std::any_of(recipe.cells.begin(),recipe.cells.end(),[&](const Cell& c){return c.id==*selected;}))return selected;
    return {};
}
CumulonimbusGroup command_cumulonimbus_cell(const CumulonimbusGroup& source,const CumulonimbusCellCommand& command) {
    const auto before=derive_cumulonimbus_recipe(source);auto next=source;
    std::visit([&](const auto& edit) {
        using T=std::decay_t<decltype(edit)>;
        if constexpr(std::is_same_v<T,CumulonimbusAddCell>) {
            new_id(before,edit.cell.id);next.modifiers.manual_cells.push_back(edit.cell);
        } else if constexpr(std::is_same_v<T,CumulonimbusDuplicateCell>) {
            auto copy=find_cell(before,edit.source_id);new_id(before,edit.new_id);copy.id=edit.new_id;
            switch(edit.seed_policy) {
            case CumulonimbusDuplicateSeed::retain:break;
            case CumulonimbusDuplicateSeed::regenerate:
                if(edit.replacement_seed==copy.structure_seed)throw std::invalid_argument("Regenerated duplicate requires a different explicit local seed");
                copy.structure_seed=edit.replacement_seed;break;
            default:throw std::invalid_argument("Unknown duplicate seed policy");
            }
            next.modifiers.manual_cells.push_back(copy);
        } else {
            const auto& old=find_cell(before,edit.id);const bool is_generated=generated(source,edit.id);
            if constexpr(std::is_same_v<T,CumulonimbusRemoveCell>) {
                if(is_generated)throw std::invalid_argument("Generated cells belong to Cumulonimbus v1; explicitly convert to Custom Cloud before deleting them");
                std::erase_if(next.modifiers.manual_cells,[&](const Cell& c){return c.id==edit.id;});
            } else if constexpr(std::is_same_v<T,CumulonimbusMoveCell>) {
                if(edit.local_center!=old.center) {
                    if(is_generated){auto& a=adjustment(next,edit.id);a.center_offset=a.center_offset+(edit.local_center-old.center);}
                    else manual(next,edit.id).center=edit.local_center;
                }
            } else if constexpr(std::is_same_v<T,CumulonimbusScaleCell>) {
                if(edit.local_radii!=old.radii) {
                    if(is_generated){auto& a=adjustment(next,edit.id);a.radius_scale={a.radius_scale.x*(edit.local_radii.x/old.radii.x),a.radius_scale.y*(edit.local_radii.y/old.radii.y),a.radius_scale.z*(edit.local_radii.z/old.radii.z)};}
                    else manual(next,edit.id).radii=edit.local_radii;
                }
            } else if constexpr(std::is_same_v<T,CumulonimbusReseedCell>) {
                if(edit.seed!=old.structure_seed) {
                    if(is_generated)adjustment(next,edit.id).structure_seed=edit.seed;
                    else manual(next,edit.id).structure_seed=edit.seed;
                }
            }
        }
    },command);
    canonicalize(next);(void)derive_cumulonimbus_recipe(next);return next;
}
Scene scene_with_cumulonimbus_cell_command(Scene scene,const CumulonimbusCellCommand& command) {
    require_valid(scene);
    if(scene.centerline) {
        auto& source=scene.centerline->source;const auto base=derive_cumulonimbus_recipe(source);
        auto source_command=command;
        std::visit([&](const auto& edit) {
            using T=std::decay_t<decltype(edit)>;
            if constexpr(std::is_same_v<T,CumulonimbusDuplicateCell>) {
                // Validate the declared policy/ID using the same source command,
                // then replace its captured ellipsoid with the displayed one.
                const auto checked=command_cumulonimbus_cell(source,command);
                auto copy=find_cell(scene.cloud,edit.source_id);copy.id=edit.new_id;
                const auto derived=derive_cumulonimbus_recipe(checked);
                copy.structure_seed=find_cell(derived,edit.new_id).structure_seed;
                source_command=CumulonimbusAddCell{copy};
            } else if constexpr(std::is_same_v<T,CumulonimbusMoveCell>) {
                const auto& visible=find_cell(scene.cloud,edit.id);const auto& original=find_cell(base,edit.id);
                source_command=CumulonimbusMoveCell{edit.id,original.center+(edit.local_center-visible.center)};
            } else if constexpr(std::is_same_v<T,CumulonimbusScaleCell>) {
                const auto& visible=find_cell(scene.cloud,edit.id);const auto& original=find_cell(base,edit.id);
                source_command=CumulonimbusScaleCell{edit.id,{original.radii.x*(edit.local_radii.x/visible.radii.x),
                    original.radii.y*(edit.local_radii.y/visible.radii.y),original.radii.z*(edit.local_radii.z/visible.radii.z)}};
            }
        },command);
        source=command_cumulonimbus_cell(source,source_command);
        scene.cloud=lower_centerline_to_recipe(*scene.centerline);
    } else if(scene.cumulonimbus) {
        scene.cumulonimbus=command_cumulonimbus_cell(*scene.cumulonimbus,command);
        scene.cloud=derive_cumulonimbus_recipe(*scene.cumulonimbus);
    } else throw std::invalid_argument("Cell source commands require a Cumulonimbus or centerline source");
    require_valid(scene);return scene;
}
}
