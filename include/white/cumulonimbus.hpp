#pragma once
#include "white/field_graph.hpp"
#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace white {
enum class CumulonimbusParameter {width,height,cloud_base,growth_direction,density,structure_seed,detail_seed};
enum class CumulonimbusValueKind {scalar,unit_direction,seed};
struct CumulonimbusParameterInfo {
    CumulonimbusParameter parameter;
    std::string_view name,unit,range;
    CumulonimbusValueKind kind;
};
std::span<const CumulonimbusParameterInfo> cumulonimbus_parameter_info();
using CumulonimbusValue=std::variant<double,Vec3,std::uint64_t>;
struct CumulonimbusCommand {
    CumulonimbusParameter parameter;
    CumulonimbusValue value;
};
CumulonimbusValue cumulonimbus_value(const CumulonimbusParameters&,CumulonimbusParameter);
std::vector<std::string> validate_cumulonimbus(const CumulonimbusGroup&);
CloudRecipe derive_cumulonimbus_recipe(const CumulonimbusGroup&);
FieldGraph derive_cumulonimbus_graph(const CumulonimbusGroup&);
// Explicit one-way conversion. The caller changes object type only after
// accepting this snapshot; no automatic or inverse conversion is performed.
FieldGraph cumulonimbus_to_custom_cloud(const CumulonimbusGroup&);

// Pure source edits for the application. EditorSession owns the only app history.
CumulonimbusGroup command_cumulonimbus(CumulonimbusGroup,const CumulonimbusCommand&);
CumulonimbusGroup edit_cumulonimbus_recipe(const CumulonimbusGroup&,const CloudRecipe&);
Scene scene_with_cumulonimbus_command(Scene,const CumulonimbusCommand&);
Scene new_cumulonimbus_scene(Scene scene={});
Scene custom_cloud_scene(Scene);


// Shared command/history path for Inspector numbers, gizmos and automation.
// Replacements also support stable-ID local edits without a second value model.
class CumulonimbusDocument {
public:
    explicit CumulonimbusDocument(CumulonimbusGroup group={});
    const CumulonimbusGroup& group()const{return group_;}
    std::uint64_t revision()const{return revision_;}
    bool apply(const CumulonimbusCommand&);
    bool reset(CumulonimbusParameter);
    bool replace(CumulonimbusGroup);
    void begin_edit();
    void end_edit();
    void cancel_edit();
    bool editing()const{return edit_start_.has_value();}
    bool can_undo()const{return !editing()&&!undo_.empty();}
    bool can_redo()const{return !editing()&&!redo_.empty();}
    bool undo();
    bool redo();
private:
    void advance();
    CumulonimbusGroup group_;
    std::uint64_t revision_=1;
    std::optional<CumulonimbusGroup> edit_start_;
    std::vector<CumulonimbusGroup> undo_,redo_;
};
}
