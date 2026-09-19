#include "white/centerline.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,const char* message,double tolerance=1e-10){check(std::abs(a-b)<=tolerance,message);}
double norm(Vec3 p){return std::sqrt(dot(p,p));}
const Cell& by_id(const CloudRecipe& recipe,Id id){const auto p=std::find_if(recipe.cells.begin(),recipe.cells.end(),[&](const auto& c){return c.id==id;});if(p==recipe.cells.end())throw std::runtime_error("Stable Cell ID lost");return *p;}
void rejection(const CenterlineShape& shape,const char* diagnostic){const auto errors=validate_centerline(shape);check(std::any_of(errors.begin(),errors.end(),[&](const auto& text){return text.find(diagnostic)!=std::string::npos;}),"Missing centerline diagnostic");bool rejected=false;try{(void)CenterlineEvaluationPlan(shape);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid centerline reached evaluator");}
void check_field(const CenterlineShape& shape,std::size_t& points){
    const CenterlineEvaluationPlan plan(shape);const auto bounds=plan.local_support();const GridLayout grid{bounds,{21,25,19}};
    const auto baked=bake_centerline(plan,grid);double peak=0;
    for(unsigned z=0;z<19;++z)for(unsigned y=0;y<25;++y)for(unsigned x=0;x<21;++x){const auto p=index_to_local(grid,{double(x),double(y),double(z)});const auto value=plan.at(p);++points;
        check(std::isfinite(value)&&value>=0&&value<=plan.maximum(),"Centerline violated finite density bound");
        check(baked[(std::size_t(z)*25+y)*21+x]==float(value),"Centerline evaluator and bake disagree");
        if(p.y<=shape.source.parameters.cloud_base)check(value==0,"Curve deformation moved fixed flat base");
        peak=std::max(peak,value);
    }
    check(peak>0,"Nonzero centerline became empty");
    check(plan.at(bounds.min)==0&&plan.at(bounds.max)==0,"Centerline leaked outside support");
    if(centerline_recipe_compatible(shape)){
        const auto graph=lower_centerline_to_graph(shape);const FieldEvaluationPlan graph_plan(graph);
        const auto first=plan.gpu_params(),second=graph_plan.gpu_params();check(std::memcmp(&first,&second,sizeof(first))==0,"Centerline GPU uniform lowering disagrees");
        for(unsigned i=0;i<31;++i){const auto p=index_to_local(grid,{double(i%21),double(i%25),double(i%19)});near(plan.at(p),graph_plan.at(p),"Compatible graph/centerline field disagreement");}
    }
}
}
int main(){try{
    std::size_t points=0;CenterlineShape original;check(validate_centerline(original).empty(),"Default centerline invalid");
    check(lower_centerline_to_recipe(original)==derive_cumulonimbus_recipe(original.source),"Zero curve changed existing prefab");
    check(centerline_curvature_bound(original)==0,"Straight curve curvature nonzero");
    check_field(original,points);
    auto tilted=original;tilted.source.parameters.growth_direction={.6,.8,0};
    check(lower_centerline_to_recipe(tilted)==derive_cumulonimbus_recipe(tilted.source),"Zero curve changed tilted prefab");
    check_field(tilted,points);
    auto bent=original;bent.points[1].offset={18,0,-9};bent.points[2].offset={5,0,8};
    bent.source.cell_adjustments={{bent.source.cell_ids[2],{4,-2,3},{1.1,.9,1.2},UINT64_C(929)}};
    bent.source.modifiers.manual_cells={{100,{200,50,-70},{8,13,11},UINT64_C(111)}};
    bent.source.modifiers.cuts={{101,{5,25,4},{3,6,4},1}};
    bent.source.modifiers.noise.origin={13,-2,19};bent.source.parameters.structure_seed=UINT64_C(0xffffffffffffffff);
    bent.source.modifiers.transform.translation={20,-3,7};bent.source.modifiers.transform.scale={2,1,.5};
    check_field(bent,points);
    const auto recipe=centerline_geometry_recipe(bent);const auto baseline=derive_cumulonimbus_recipe(bent.source);
    check(by_id(recipe,100)==by_id(baseline,100)&&recipe.cuts==baseline.cuts,"Curve changed manual cells/cuts");
    check(recipe.noise==baseline.noise&&recipe.structure_seed==baseline.structure_seed&&recipe.detail_seed==baseline.detail_seed&&recipe.transform==baseline.transform&&recipe.optics==baseline.optics,"Curve reset unrelated modifiers/seeds");
    for(std::size_t i=0;i<recipe.cells.size();++i)check(recipe.cells[i].id==baseline.cells[i].id&&recipe.cells[i].structure_seed==baseline.cells[i].structure_seed,"Curve changed generated stable IDs or local generation sequence");
    check(by_id(recipe,bent.source.cell_ids[2]).structure_seed==929,"Curve discarded local seed override");
    const auto start=sample_centerline(bent,0),end=sample_centerline(bent,1);
    near(start.position.y,bent.source.parameters.cloud_base,"Curve start moved base");near(end.position.y,120,"Curve endpoint height differs");near(end.position.x,5,"Curve endpoint offset differs");
    const auto before=sample_centerline(bent,.5-1e-8),after=sample_centerline(bent,.5+1e-8);
    check(norm(before.position-after.position)<1e-5&&norm(before.tangent-after.tangent)<1e-6,"Curve has a control-knot discontinuity");
    const double curvature=centerline_curvature_bound(bent);check(curvature>0&&curvature<=max_centerline_curvature,"Bent curve bound invalid");
    for(unsigned i=1;i<200;++i){const double t=double(i)/200;const auto sample=sample_centerline(bent,t);near(norm(sample.tangent),1,"Curve tangent is not unit");near(norm(sample.normal),1,"Curve normal is not unit");near(norm(sample.binormal),1,"Curve binormal is not unit");near(dot(sample.tangent,sample.normal),0,"Curve frame is not orthogonal");near(dot(sample.tangent,sample.binormal),0,"Curve frame is not orthogonal");
        const auto a=sample_centerline(bent,t-1e-6),b=sample_centerline(bent,t+1e-6);const double numerical=norm(a.tangent-b.tangent)/norm(a.position-b.position);
        check(numerical<=curvature*1.00001+1e-8,"Numerical curvature exceeds conservative analytic bound");
    }
    // A profile changes one altitude band; density is not approximated by radius.
    auto profiled=bent;profiled.profile={{1,0,1,1},{2,.4,1,1},{3,.6,1.5,.2},{4,.8,1,1},{5,1,1,1}};
    const CenterlineEvaluationPlan profile_plan(profiled);const DensityField geometry(profile_plan.geometry_recipe());check_field(profiled,points);
    check(by_id(profile_plan.geometry_recipe(),profiled.source.cell_ids[0]).radii==by_id(recipe,bent.source.cell_ids[0]).radii,"Local profile changed unrelated lower radius band");
    check(by_id(profile_plan.geometry_recipe(),profiled.source.cell_ids[2]).radii!=by_id(recipe,bent.source.cell_ids[2]).radii,"Radius profile did not change target band");
    for(double t:{.2,.6,.9}){const auto p=sample_centerline(profiled,t).position;const auto weight=sample_centerline(profiled,t).density_scale;near(profile_plan.at(p),geometry.at(p)*weight,"Density profile has incorrect altitude response");}
    check(centerline_recipe_compatible(profiled),"Nonuniform density failed shared-kernel lowering");
    const auto lowered_profile=lower_centerline_to_recipe(profiled);
    check(lowered_profile.altitude_density.enabled&&lowered_profile.altitude_density.knots.size()==profiled.profile.size(),"Nonuniform density profile was dropped by lowering");
    check(lower_to_recipe(lower_centerline_to_graph(profiled))==lowered_profile,"Graph lowered a different density profile");
    auto uniform=bent;for(auto& p:uniform.profile)p.density_scale=.5;check_field(uniform,points);near(AltitudeDensityEvaluator(lower_centerline_to_recipe(uniform).altitude_density).at(50),.5,"Uniform density profile did not lower");
    auto zero=bent;for(auto& p:zero.profile)p.density_scale=0;const CenterlineEvaluationPlan empty(zero);check(empty.maximum()==0&&empty.at({0,50,0})==0,"Zero density profile emitted density");
    auto wide=original;for(auto& p:wide.profile)p.radius_scale=4;check_field(wide,points);
    auto narrow=original;for(auto& p:narrow.profile)p.radius_scale=.125;check_field(narrow,points);
    CenterlineDocument doc(bent),inspector(bent),gizmo(bent);
    const auto curve_snapshot=bent.points;const auto edits_snapshot=bent.source.cell_adjustments;
    const CumulonimbusCommand height{CumulonimbusParameter::height,180.0};doc.apply(height);inspector.apply(height);
    gizmo.begin_edit();for(double h:{125.,140.,160.,180.})gizmo.apply(CumulonimbusCommand{CumulonimbusParameter::height,h});gizmo.end_edit();
    check(doc.shape()==inspector.shape()&&doc.shape()==gizmo.shape(),"Centerline handle and Inspector use different source values");
    check(doc.shape().points==curve_snapshot&&doc.shape().source.cell_adjustments==edits_snapshot&&doc.shape().source.modifiers==bent.source.modifiers,"Height handle rewrote local curve or cell adjustments");
    check(gizmo.undo()&&gizmo.shape()==bent&&!gizmo.can_undo(),"Centerline drag failed single-step Undo");check(gizmo.redo()&&gizmo.shape()==doc.shape(),"Centerline drag failed Redo");
    const auto document_before=doc.shape();const auto revision_before=doc.revision();
    doc.begin_edit();doc.apply(CenterlineMovePoint{2,{22,90,4}});doc.cancel_edit();check(doc.shape()==document_before&&doc.revision()>revision_before,"Cancel failed to restore curve source");
    check(doc.undo()&&doc.can_redo(),"Missing curve redo");doc.begin_edit();doc.apply(CenterlineMovePoint{2,{21,60,2}});doc.cancel_edit();check(doc.can_redo(),"Cancelled curve gesture destroyed redo");
    const auto unchanged=doc.shape();const auto unchanged_revision=doc.revision();
    for(const CenterlineCommand command:{CenterlineCommand{CenterlineMovePoint{2,{0,0,0}}},CenterlineCommand{CenterlineMovePoint{999,{0,60,0}}},CenterlineCommand{CenterlineRemovePoint{1}},CenterlineCommand{CenterlineInsertPoint{2,.25}},CenterlineCommand{CenterlineSetProfile{999,1,1}}}){
        bool rejected=false;try{doc.apply(command);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&doc.shape()==unchanged&&doc.revision()==unchanged_revision,"Rejected curve edit mutated document");}
    CenterlineDocument subdivide(bent);subdivide.apply(CenterlineInsertPoint{7,.25});
    check(subdivide.shape().source==bent.source&&subdivide.shape().points[1].id==7,"Curve subdivision changed IDs/seeds/noise origin");
    subdivide.apply(CenterlineRemovePoint{7});check(subdivide.shape()==bent,"Remove inserted point failed to restore original curve");
    // Horizontal endpoint movement preserves exact endpoint t even when
    // subtraction/division cannot recover 1 from a fractional base/height.
    auto fractional=original;fractional.source.parameters.cloud_base=12345.678;fractional.source.parameters.height=123.456;
    CenterlineDocument fractional_edit(fractional);const double top=fractional.source.parameters.cloud_base+fractional.source.parameters.height;
    fractional_edit.apply(CenterlineMovePoint{3,{3,top,-2}});
    check(fractional_edit.shape().points.back().t==1&&fractional_edit.shape().points.back().offset==Vec3{3,0,-2},"Horizontal fractional-base endpoint movement changed constrained height");
    bool endpoint_rejected=false;try{fractional_edit.apply(CenterlineMovePoint{3,{3,std::nextafter(top,INFINITY),-2}});}catch(const std::invalid_argument&){endpoint_rejected=true;}
    check(endpoint_rejected,"Explicit vertical endpoint movement was silently accepted");
    const auto before_profile=profiled;CenterlineDocument profile_edit(profiled);profile_edit.apply(CenterlineSetProfile{3,2,.3});
    check(profile_edit.shape().source==profiled.source&&profile_edit.shape().profile[2].id==3,"Profile edit rewrote source or stable ID");check(profile_edit.undo()&&profile_edit.shape()==before_profile,"Profile Undo lost source");
    auto bad=original;bad.contract_version=2;rejection(bad,"contract version");
    bad=original;bad.points[1].t=0;rejection(bad,"separation");
    bad=original;bad.points[1].id=1;rejection(bad,"IDs");
    bad=original;bad.points[1].offset.y=1;rejection(bad,"Y must be zero");
    bad=original;bad.points[1].offset.x=std::numeric_limits<double>::quiet_NaN();rejection(bad,"finite");
    bad=original;bad.points[1].offset.x=1000;rejection(bad,"curvature");
    bad=original;bad.points.resize(7);rejection(bad,"2..6");
    bad=original;bad.profile[0].radius_scale=.124;rejection(bad,"Radius profile");
    bad=original;bad.profile[0].radius_scale=4.01;rejection(bad,"Radius profile");
    bad=original;bad.profile[0].density_scale=-.01;rejection(bad,"Density profile");
    bad=original;bad.profile[0].density_scale=1.01;rejection(bad,"Density profile");
    bad=original;bad.profile[1].t=0;rejection(bad,"endpoints");
    bad=original;bad.source.parameters.height=0;rejection(bad,"Height");
    bad=original;bad.source.parameters.cloud_base=100000;bad.source.parameters.height=10;
    bad.profile={{1,0,1,1},{2,.5,1,1},{3,.501,1,0},{4,1,1,0}};
    rejection(bad,"precision");
    bool invalid_sample=false;try{(void)sample_centerline(original,std::numeric_limits<double>::quiet_NaN());}catch(const std::invalid_argument&){invalid_sample=true;}check(invalid_sample,"NaN curve parameter accepted");
    bool invalid_grid=false;try{(void)bake_centerline(profile_plan,{profile_plan.local_support(),{1024,1024,1024}});}catch(const std::invalid_argument&){invalid_grid=true;}check(invalid_grid,"Unbounded curve bake accepted");
    CenterlineDocument history;for(unsigned i=0;i<140;++i)history.apply(CumulonimbusCommand{CumulonimbusParameter::height,121.+i});unsigned undo=0;while(history.undo())++undo;check(undo==128,"Curve history exceeded bound");
    std::cout<<"Centerline v1: "<<points<<" evaluator/bake comparisons; straight/tilted/bent, C1 frames, curvature, profiles, fixed base, stable IDs/local edits, shared GPU profile lowering and grouped Undo passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
