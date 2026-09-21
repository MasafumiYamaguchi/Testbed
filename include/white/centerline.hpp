#pragma once
#include "white/cumulonimbus.hpp"

namespace white {
struct CenterlineSample {
    Vec3 position{},tangent{},normal{},binormal{};
    double radius_scale=1,density_scale=1;
};
std::vector<std::string> validate_centerline(const CenterlineShape&);
CenterlineSample sample_centerline(const CenterlineShape&,double t);
// Conservative analytic bound from Hermite second derivatives and dy/dt=height.
double centerline_curvature_bound(const CenterlineShape&);
// Geometry-only Recipe for diagnostics. Full lowering below retains the exact
// altitude density profile in the shared CPU/HLSL density kernel.
CloudRecipe centerline_geometry_recipe(const CenterlineShape&);
bool centerline_recipe_compatible(const CenterlineShape&);
CloudRecipe lower_centerline_to_recipe(const CenterlineShape&);
FieldGraph lower_centerline_to_graph(const CenterlineShape&);

class CenterlineEvaluationPlan {
public:
    explicit CenterlineEvaluationPlan(CenterlineShape);
    double at(Vec3 local)const;
    double maximum()const;
    Bounds local_support()const{return density_.local_support();}
    Bounds world_support()const{return density_.world_support();}
    const CenterlineShape& shape()const{return shape_;}
    const CloudRecipe& geometry_recipe()const{return geometry_.recipe();}
    // Shared algorithm-3 GPU kernel, including nonuniform altitude density.
    GpuDensityParams gpu_params()const;
private:
    CenterlineShape shape_;
    DensityField geometry_;
    DensityField density_;
};
// Explicit CPU bake of the same evaluator. No GPU upload/publication is implied.
std::vector<float> bake_centerline(const CenterlineEvaluationPlan&,const GridLayout&);

struct CenterlineMovePoint {Id id;Vec3 local_position;};
struct CenterlineSetProfile {Id id;double radius_scale,density_scale;};
struct CenterlineInsertPoint {Id id;double t;};
struct CenterlineRemovePoint {Id id;};
using CenterlineCommand=std::variant<CumulonimbusCommand,CenterlineMovePoint,CenterlineSetProfile,CenterlineInsertPoint,CenterlineRemovePoint>;
CenterlineShape command_centerline(CenterlineShape,const CenterlineCommand&);
// One authoritative source/history path for numeric controls and gizmo handles.
class CenterlineDocument {
public:
    explicit CenterlineDocument(CenterlineShape={});
    const CenterlineShape& shape()const{return shape_;}
    std::uint64_t revision()const{return revision_;}
    bool apply(const CenterlineCommand&);
    bool replace(CenterlineShape);
    void begin_edit();
    void end_edit();
    void cancel_edit();
    bool editing()const{return start_.has_value();}
    bool can_undo()const{return !editing()&&!undo_.empty();}
    bool can_redo()const{return !editing()&&!redo_.empty();}
    bool undo();
    bool redo();
private:
    void advance();
    CenterlineShape shape_;
    std::uint64_t revision_=1;
    std::optional<CenterlineShape> start_;
    std::vector<CenterlineShape> undo_,redo_;
};
}
