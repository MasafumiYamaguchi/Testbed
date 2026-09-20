#include "white/density_export.hpp"
#include "white/anvil_scene.hpp"
#include "white/generation.hpp"
#include "white/persistence.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;
namespace {
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char* message){try{fn();}catch(const std::exception&){return;}throw std::runtime_error(message);}
bool close(Vec3 a,Vec3 b,double tolerance=1e-12){return std::abs(a.x-b.x)<=tolerance&&std::abs(a.y-b.y)<=tolerance&&std::abs(a.z-b.z)<=tolerance;}
Scene static_frozen(Scene scene){GenerationSettings settings;settings.enabled=false;return freeze_candidate(generate_cloud_state(scene,settings));}
Scene asymmetric(){
    Scene scene;scene.cloud.envelope={{-6,0,-14},{12,23,8}};
    scene.cloud.cells={{2,{2,11,-3},{5,7,9},91}};scene.cloud.base.enabled=false;
    scene.cloud.density=2;scene.cloud.blend_width=0;
    scene.cloud.transform.translation={11.25,-3.5,5.75};
    scene.cloud.transform.rotation={0,std::sqrt(.5),0,std::sqrt(.5)};
    scene.cloud.transform.scale={2,3,.5};return static_frozen(scene);
}
std::array<std::int32_t,3> nearest_index(const DensityExportLayout& layout,Vec3 world){
    const auto index=export_world_to_index(layout,world);
    return {std::int32_t(std::llround(index.x)),std::int32_t(std::llround(index.y)),std::int32_t(std::llround(index.z))};
}
double trilinear(const DensityExportSnapshot& snapshot,Vec3 world){
    const auto p=export_world_to_index(snapshot.layout(),world);
    const std::array<std::int32_t,3> lower{std::int32_t(std::floor(p.x)),std::int32_t(std::floor(p.y)),std::int32_t(std::floor(p.z))};
    const std::array<double,3> f{p.x-lower[0],p.y-lower[1],p.z-lower[2]};double sum=0;
    for(unsigned corner=0;corner<8;++corner){auto index=lower;double weight=1;
        for(unsigned axis=0;axis<3;++axis){const bool upper=bool(corner&(1u<<axis));index[axis]+=upper?1:0;weight*=upper?f[axis]:1-f[axis];}
        sum+=weight*snapshot.sample(index);
    }
    return sum;
}
void coordinate_contract(){
    const auto layout=density_export_layout({{11.2,-3.3,5.4},{13.3,2.1,14.5}},{.75,2});
    check(layout.extent==std::array<std::uint32_t,3>{8,12,17}&&layout.sample_count==1632,"Asymmetric extent or padding differs");
    check(layout.world_bounds==Bounds{{9,-5.25,3.75},{15,3.75,16.5}},"Bounds must be outward world voxel faces");
    check(layout.index_zero_world==Vec3{9.375,-4.875,4.125},"Integer index is not the voxel center");
    check(export_index_to_world(layout,{1,2,3})==Vec3{10.125,-3.375,6.375},"Index axes swapped or origin treated as corner");
    check(export_index_to_world(layout,{-.5,-.5,-.5})==layout.world_bounds.min,"Minimum face does not map to -0.5");
    check(export_index_to_world(layout,{7.5,11.5,16.5})==layout.world_bounds.max,"Maximum face differs from extent minus 0.5");
    for(const auto index:std::array{Vec3{0,0,0},Vec3{2.25,7.75,5.5},Vec3{-2,20,31}})
        check(close(export_world_to_index(layout,export_index_to_world(layout,index)),index),"Fractional index/world roundtrip failed");
    // Independent asymmetric reference carried forward from Issue #13. It is
    // point-sampled rho, not a distance, opacity or per-voxel volume integral.
    const auto index=export_world_to_index(layout,{10.125,-3.375,6.375});
    check((1+index.x+2*index.y+4*index.z)/256==18./256,"Asymmetric reference density/world mapping differs");
    const double infinity=std::numeric_limits<double>::infinity();
    rejects([&]{density_export_layout({}, {0,1});},"Zero voxel size accepted");
    rejects([&]{density_export_layout({}, {infinity,1});},"Nonfinite voxel size accepted");
    rejects([&]{density_export_layout({}, {1,0});},"Missing interpolation padding accepted");
    rejects([&]{density_export_layout({{0,0,0},{0,1,1}},{1,1});},"Degenerate support accepted");
    rejects([&]{density_export_layout({{0,0,0},{3e9,1,1}},{1,1});},"Index extent overflow accepted");
    rejects([&]{density_export_layout({{0,0,0},{2e9,2e9,2e9}},{1,1});},"Total sample byte overflow accepted");
    rejects([&]{density_export_layout({{1e20,0,0},{1e20+1e6,1,1}},{1,1});},"Unrepresentable lattice origin accepted");
    rejects([&]{export_index_to_world(layout,{infinity,0,0});},"Nonfinite index accepted");
    std::cout<<"density_export_coordinates asymmetric_axes=true center_origin=true outward_padding=true overflow_rejected=true PASS\n";
}
void frozen_transform_and_repeatability(){
    auto scene=asymmetric();const auto jobs=generation_job_count();const DensityExportSnapshot snapshot(scene,{.75,1});
    check(close(snapshot.layout().source_world_support.min,{4.25,-3.5,-18.25})&&
          close(snapshot.layout().source_world_support.max,{15.25,65.5,17.75}),"Object TRS was omitted or applied twice");
    check(snapshot.density_at_world({9.75,29.5,1.75})==2,"Known transformed ellipsoid interior has wrong rho");
    check(snapshot.density_at_world({19.75,29.5,1.75})==0,"Asymmetric world position sampled wrong axis");
    rejects([&]{snapshot.density_at_world({std::numeric_limits<double>::quiet_NaN(),0,0});},"NaN world sample silently became density");
    rejects([&]{snapshot.density_at_world({0,0,std::numeric_limits<double>::infinity()});},"Infinite world sample silently became density");
    const DensityExportSnapshot reloaded(parse_scene_json(scene_json(scene)),{.75,1});
    std::size_t nonzero=0;const auto extent=snapshot.layout().extent;
    for(std::uint32_t z=0;z<extent[2];++z)for(std::uint32_t y=0;y<extent[1];++y)for(std::uint32_t x=0;x<extent[0];++x){
        const auto value=snapshot.sample({std::int32_t(x),std::int32_t(y),std::int32_t(z)});
        check(value==reloaded.sample({std::int32_t(x),std::int32_t(y),std::int32_t(z)}),"Re-capture after Native reload changed a stored float sample");
        check(std::isfinite(value)&&value>=0&&value<=2,"Export introduced negative, nonfinite or normalized density");
        if(x==0||y==0||z==0||x+1==extent[0]||y+1==extent[1]||z+1==extent[2])check(value==0,"Conservative padding contains density");
        if(value>0)++nonzero;
    }
    check(nonzero>100&&snapshot.sample({-1,7,11})==0&&snapshot.sample({std::int32_t(extent[0]),7,11})==0,"Zero background or fixture occupancy incorrect");
    check(reloaded.metadata_json()==snapshot.metadata_json()&&reloaded.request_hash()==snapshot.request_hash(),"Same native state/settings changed export identity");
    const DensityExportSnapshot finer(scene,{.5,1});
    check(finer.snapshot_hash()==snapshot.snapshot_hash()&&finer.finishing_snapshot_hash()==snapshot.finishing_snapshot_hash()&&finer.request_hash()!=snapshot.request_hash(),"Resolution changed source identity or missed request identity");
    scene.camera.position.x+=4;scene.sun.irradiance.x+=.2;scene.exposure_ev=2;scene.preview_approx.enabled=!scene.preview_approx.enabled;
    const DensityExportSnapshot different_view(scene,{.75,1});
    check(different_view.snapshot_hash()==snapshot.snapshot_hash()&&different_view.metadata_json()==snapshot.metadata_json(),"View/light/preview appearance entered density export authority");
    check(generation_job_count()==jobs,"Capture, sampling or reload regenerated frozen structure");
    rejects([&]{DensityExportSnapshot not_fixed(Scene{});},"Live procedural authority was silently generated for export");
    std::cout<<"density_export_snapshot transformed_rho=true exact_repeat=true view_independent=true zero_generation_jobs=true PASS\n";
}
void finishing_and_interpolation(){
    Scene initial;initial.cloud.cells={{2,{.5,10.5,.5},{12,15,18},1}};initial.cloud.base.enabled=false;
    initial.cloud.envelope={{-20,-10,-25},{25,35,30}};initial.cloud.density=2;
    auto scene=static_frozen(initial);FinishModifier cut;cut.id=1;cut.target_id=scene.frozen->id;
    cut.mask={{.5,10.5,.5},{.75,.75,.75},1};scene=scene_with_finish_command(scene,{FinishCommandKind::add,0,cut});
    const auto jobs=generation_job_count();const DensityExportSnapshot snapshot(scene,{1,1});
    const auto source_hash=snapshot.snapshot_hash(),finish_hash=snapshot.finishing_snapshot_hash();
    const Vec3 interior{1.2,10.5,.5};check(snapshot.density_at_world(interior)==0,"Final hard cut was not in sampled source");
    check(snapshot.sample(nearest_index(snapshot.layout(),cut.mask.center))==0,"Voxel-centered hard cut was not baked into density");
    const auto reconstructed=trilinear(snapshot,interior);
    check(reconstructed>0&&reconstructed<2,"Fixture did not expose finite-resolution leakage into an analytic hard cut");
    auto amplify=cut;amplify.id=2;amplify.kind=FinishModifierKind::density;amplify.density_multiplier=3;
    amplify.mask={{7,10.5,.5},{3,3,3},2};scene=scene_with_finish_command(scene,{FinishCommandKind::add,0,amplify});
    const DensityExportSnapshot changed(scene,{1,1});
    check(changed.snapshot_hash()!=source_hash&&changed.finishing_snapshot_hash()!=finish_hash,"Final finishing not included in snapshot identity");
    check(changed.density_at_world({7,10.5,.5})==6&&snapshot.density_at_world({7,10.5,.5})==2,"Post-capture stack edit entered immutable export");
    const auto field=scene.frozen->fields.front();auto noise=field.recipe.noise;noise.medium_strength=.5;
    scene=scene_with_frozen_detail(scene,field.development_id,noise,987,field.layers);
    const DensityExportSnapshot detailed(scene,{1,1});
    check(detailed.finishing_snapshot_hash()!=changed.finishing_snapshot_hash()&&scene.frozen->content_hash==snapshot.scene().frozen->content_hash,"Detail seed/noise omitted from finishing identity or changed frozen shape");
    auto protect=amplify;protect.id=3;protect.kind=FinishModifierKind::protect_detail;
    scene=scene_with_finish_command(scene,{FinishCommandKind::add,0,protect});const DensityExportSnapshot protected_detail(scene,{1,1});
    check(protected_detail.density_at_world({7,10.5,.5})==6&&protected_detail.density_at_world(cut.mask.center)==0,
          "Final snapshot omitted detail protection or revived an earlier hard cut");
    const auto metadata=nlohmann::json::parse(snapshot.metadata_json());
    check(metadata["grid"]["type"]=="FloatGrid"&&metadata["grid"]["class"]=="fog_volume"&&metadata["grid"]["background"]==0&&
          metadata["supported_finishing"]["paint"]==false&&metadata["dcc_metadata_auto_application_assumed"]==false,"Supported-version/recipient metadata contract missing");
    check(generation_job_count()==jobs,"Finishing export or samples started generation");
    std::cout<<"density_export_finishing hard_cut_baked=true capture_isolated=true detail_hashed=true interpolation_leak="<<reconstructed<<" paint_supported=false PASS\n";
}
void wind_unknown_version_and_candidate_isolation(){
    const auto initial=new_cumulonimbus_scene();auto settings=default_generation_settings(initial);settings.stage=.8;
    const auto calm=freeze_candidate(generate_cloud_state(initial,settings));settings.wind.back().displacement={40,0,12};
    auto current=freeze_candidate(generate_cloud_state(initial,settings));
    const auto jobs=generation_job_count();const DensityExportSnapshot calm_snapshot(calm,{3,1}),wind_snapshot(current,{3,1});
    std::size_t different=0;for(int y=10;y<110;y+=5)for(int x=-40;x<90;x+=5){const Vec3 point{double(x),double(y),0};if(std::abs(calm_snapshot.density_at_world(point)-wind_snapshot.density_at_world(point))>1e-6)++different;}
    check(different>20&&generation_job_count()==jobs,"Wind remained metadata-only or sampling regenerated it");
    const auto saved=wind_snapshot.metadata_json();const auto probe=wind_snapshot.density_at_world({20,60,0});
    check(probe>0,"Unknown generation version fixture must compare nonzero density");
    settings.stage=.25;settings.wind.back().displacement={70,0,-20};current=freeze_candidate(generate_cloud_state(initial,settings));
    const auto after_candidate=generation_job_count();
    check(wind_snapshot.metadata_json()==saved&&wind_snapshot.density_at_world({20,60,0})==probe&&
          wind_snapshot.scene().frozen->selection_value==.8&&current.frozen->selection_value==.25,"New candidate/stage changed the captured export");
    auto unavailable=wind_snapshot.scene();unavailable.frozen->generation_version=987;
    unavailable.frozen->provenance->settings.algorithm_version=987;refresh_frozen_scene(unavailable);
    unavailable=parse_scene_json(scene_json(unavailable));const DensityExportSnapshot unknown(unavailable,{3,1});
    const auto json=nlohmann::json::parse(unknown.metadata_json());
    check(unknown.density_at_world({20,60,0})==probe&&json["generation"]["version"]==987&&json["generation"]["regeneration_available"]==false&&
          json["generation"]["settings"]["algorithm_version"]==987,"Unavailable generation implementation changed evaluated export or provenance");
    unavailable.frozen->provenance.reset();refresh_frozen_scene(unavailable);const DensityExportSnapshot history_free(unavailable,{3,1});
    const auto no_history=nlohmann::json::parse(history_free.metadata_json());
    check(history_free.density_at_world({20,60,0})==probe&&no_history["generation"]["settings"].is_null()&&
          no_history["generation"]["input_hash"]==std::to_string(unavailable.frozen->generation_input_hash),"Missing provenance fabricated a recipe or lost input hash");
    check(generation_job_count()==after_candidate,"Unknown-version export secretly regenerated");
    std::cout<<"density_export_provenance wind_changes_rho=true later_candidate_isolated=true unknown_generation_preserved=true history_optional=true PASS\n";
}
void multi_field_anvil(){
    auto initial=new_anvil_scene(new_cumulonimbus_scene());initial.anvil->cloud.settings.mode=TopLobeMode::children;
    initial.anvil->settings.enabled=true;refresh_anvil_scene(initial);auto settings=default_generation_settings(initial);
    settings.stage=.8;settings.wind.back().displacement={40,0,12};const auto scene=freeze_candidate(generate_cloud_state(initial,settings));
    check(scene.frozen->fields.size()==2&&scene.frozen->anvil.has_value(),"Export fixture must retain two fields and an active anvil");
    const auto jobs=generation_job_count();const DensityExportSnapshot snapshot(scene,{4,1});
    const SceneDensityEvaluator reference(scene);const DensityField incomplete_proxy(scene.cloud);std::size_t proxy_differs=0;
    const auto& layout=snapshot.layout();
    for(std::uint32_t z=1;z<layout.extent[2];z+=3)for(std::uint32_t y=1;y<layout.extent[1];y+=3)for(std::uint32_t x=1;x<layout.extent[0];x+=3){
        const Vec3 world=export_index_to_world(layout,{double(x),double(y),double(z)}),local=world_to_local(scene.frozen->transform,world);
        const float expected=static_cast<float>(reference.at(local));
        check(snapshot.sample({std::int32_t(x),std::int32_t(y),std::int32_t(z)})==expected,"Exporter lost a field or the evaluated anvil");
        if(std::abs(expected-incomplete_proxy.at(local))>1e-5)++proxy_differs;
    }
    check(proxy_differs>10,"Multi-field/anvil fixture cannot distinguish the incomplete proxy from final density");
    FinishModifier cut;cut.id=1;cut.target_kind=FinishTargetKind::field;cut.target_id=scene.frozen->fields[0].development_id;
    cut.mask={{20,80,0},{300,300,300},20};auto scoped=scene_with_finish_command(scene,{FinishCommandKind::add,0,cut});
    auto protect=cut;protect.id=2;protect.kind=FinishModifierKind::protect_detail;protect.target_id=scene.frozen->fields[1].development_id;
    scoped=scene_with_finish_command(scoped,{FinishCommandKind::add,0,protect});const DensityExportSnapshot scoped_snapshot(scoped,{4,1});
    const auto& top=scene.frozen->fields[1];auto clean=frozen_effective_recipe(top);
    clean.noise.warp_amplitude=clean.noise.medium_strength=clean.noise.micro_erosion=0;
    const Vec3 point=clean.cells.front().center+top.translation;
    const double expected=DensityField(clean).at(point-top.translation);
    check(expected>0&&std::abs(scoped_snapshot.density_at_world(local_to_world(scene.frozen->transform,point))-expected)<1e-12,
          "A field-targeted cut erased the independent protected top field");
    check(generation_job_count()==jobs,"Multi-field/anvil export or finishing started generation");
    std::cout<<"density_export_grouped two_fields_and_anvil=true proxy_difference_points="<<proxy_differs<<" scoped_cut_retains_other_field=true zero_generation_jobs=true PASS\n";
}
void optical_scale(){
    Scene initial;initial.cloud.base.enabled=false;initial.cloud.cells={{2,{0,10,0},{10,10,10},1}};initial.cloud.density=2;
    const auto fixed=static_frozen(initial);const DensityExportSnapshot a(fixed);
    auto scaled=fixed;scaled.frozen->transform.scale={2,2,2};refresh_frozen_scene(scaled);const DensityExportSnapshot b(scaled);
    double integral_a=0,integral_b=0;constexpr unsigned steps=32;
    for(unsigned i=0;i<steps;++i){const double x=-2+(i+.5)*4/steps;
        integral_a+=a.density_at_world({x,10,0})*4/steps;
        integral_b+=b.density_at_world({2*x,20,0})*8/steps;
    }
    const double extinction=fixed.frozen->optics.extinction_scale,tau_a=extinction*integral_a,tau_b=extinction*integral_b;
    check(integral_a==8&&integral_b==16&&tau_b==2*tau_a&&(extinction/2)*integral_b==tau_a,"World scale incorrectly normalized rho or preserved optical depth implicitly");
    check(nlohmann::json::parse(b.metadata_json())["optics_recommendation"]["extinction_scale_per_m"]==extinction,"Export silently compensated extinction for object scale");
    auto anisotropic=fixed;anisotropic.frozen->transform.scale={2,3,.5};refresh_frozen_scene(anisotropic);
    const DensityExportSnapshot c(anisotropic);const std::array<double,3> expected_ratios{2,3,.5};
    for(unsigned axis=0;axis<3;++axis){double before=0,after=0;
        for(unsigned i=0;i<steps;++i){const double offset=-2+(i+.5)*4/steps;Vec3 point{0,10,0};
            if(axis==0)point.x+=offset;else if(axis==1)point.y+=offset;else point.z+=offset;
            before+=a.density_at_world(point)*4/steps;
            after+=c.density_at_world(local_to_world(anisotropic.frozen->transform,point))*4*expected_ratios[axis]/steps;
        }
        check(after*extinction==before*extinction*expected_ratios[axis],"Nonuniform world scale used an incorrect universal optical-depth compensation");
    }
    std::cout<<"density_export_optics rho_dimensionless=true uniform_scale_doubles_tau=true nonuniform_tau_ratios=2,3,0.5 inverse_extinction_compensates=true PASS\n";
}
}
int main(){try{coordinate_contract();frozen_transform_and_repeatability();finishing_and_interpolation();wind_unknown_version_and_candidate_isolation();multi_field_anvil();optical_scale();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
