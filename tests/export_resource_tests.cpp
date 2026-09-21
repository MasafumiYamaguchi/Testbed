#include "white/export_resources.hpp"
#include "white/build_info.hpp"
#include "white/generation.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace white;
namespace {
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char* message){try{fn();}catch(const std::exception&){return;}throw std::runtime_error(message);}
Scene fixture(){
    Scene scene;scene.cloud.envelope={{0,0,0},{5,7,9}};scene.cloud.cells={{2,{2,3,4},{1,2,3},1}};scene.cloud.base.enabled=false;
    GenerationSettings settings;settings.enabled=false;return freeze_candidate(generate_cloud_state(scene,settings));
}
ExportResourceBytes unrestricted(){const auto maximum=std::numeric_limits<std::uint64_t>::max();return {maximum,maximum,maximum,maximum,maximum,maximum};}
ExportResourceOptions staged(){ExportResourceOptions o;o.tile_extent={4,6,8};o.gpu_tile_slots=2;o.transfer_staging_slots=3;o.cpu_readback_slots=4;o.retained_snapshot_bytes=4194304;return o;}
void exact_accounting(){
    const DensityExportSnapshot snapshot(fixture(),{1,1});const auto jobs=generation_job_count();
    const ExportResourceBytes exact{2772,1536,36864,3072,4194304,4238548};
    const auto plan=plan_export_resources(snapshot,staged(),exact);require_export_resource_reservations(plan);
    check(plan.layout.extent==std::array<std::uint32_t,3>{7,9,11}&&plan.layout.sample_count==693,"Export extent lost padding or noncubic axes");
    check(plan.tile_extent==std::array<std::uint32_t,3>{4,6,8}&&plan.tile_grid==std::array<std::uint32_t,3>{2,2,2}&&plan.tile_count==8,"Tile edge/count rounding differs");
    check(plan.packed_tile_bytes==768&&plan.transfer_row_pitch_bytes==256&&plan.transfer_tile_bytes==12288,"Transfer row alignment incorrectly reused tightly packed bytes");
    check(plan.required.cpu_dense_grid==2772&&plan.required.gpu_tiles==1536&&plan.required.transfer_staging==36864&&
          plan.required.cpu_readback_queue==3072&&plan.required.retained_snapshot==4194304&&plan.required.accounted_total==4238548,
          "Separate payload categories lost a slot, duplicated a resource or used voxel bytes as VDB memory");
    check(plan.reservations_fit(),"Exact-fit caller limits rejected");
    const auto json=nlohmann::json::parse(export_resource_plan_json(plan));
    check(json["openvdb_tree_bytes"].is_null()&&json["vdb_output_file_bytes"].is_null()&&json["allocator_and_driver_overhead_bytes"].is_null()&&
          json["writer_ready"]==false&&json["actual_memory_admission_established"]==false,"Requested payloads were promoted to VDB/peak-memory admission");
    check(json["retained_snapshot"]["reservation_source"]=="caller_supplied"&&json["retained_snapshot"]["dynamic_storage_measured"]==false,
          "Snapshot reservation was mislabeled as measured residency");
    const std::array<std::uint64_t ExportResourceBytes::*,6> members{&ExportResourceBytes::cpu_dense_grid,&ExportResourceBytes::gpu_tiles,
        &ExportResourceBytes::transfer_staging,&ExportResourceBytes::cpu_readback_queue,&ExportResourceBytes::retained_snapshot,&ExportResourceBytes::accounted_total};
    for(const auto member:members){auto limits=exact;--(limits.*member);const auto too_small=plan_export_resources(snapshot,staged(),limits);
        check(!too_small.reservations_fit(),"One-byte category/aggregate budget overrun accepted");
        rejects([&]{require_export_resource_reservations(too_small);},"Over-budget plan did not reject resource reservation");
        check(nlohmann::json::parse(export_resource_plan_json(too_small))["exceeded_categories"].size()==1,"Budget diagnostic attributed wrong category");
    }
    check(generation_job_count()==jobs,"Resource preflight invoked growth");
    std::cout<<"export_resource_accounting noncubic=true aligned_staging=true independent_queues=true exact_fit=true six_one_byte_limits_rejected=true PASS\n";
}
void cpu_and_tile_models(){
    const DensityExportSnapshot snapshot(fixture());auto options=staged();options.gpu_tile_slots=options.transfer_staging_slots=options.cpu_readback_slots=0;
    const auto cpu=plan_export_resources(snapshot,options,{2772,0,0,0,4194304,4197076});require_export_resource_reservations(cpu);
    check(cpu.required.gpu_tiles==0&&cpu.required.transfer_staging==0&&cpu.required.cpu_readback_queue==0,"CPU model reserved nonexistent GPU/transfer resources");
    options=staged();options.tile_extent={100,100,100};const auto whole=plan_export_resources(snapshot,options,unrestricted());
    check(whole.tile_extent==snapshot.layout().extent&&whole.tile_count==1&&whole.packed_tile_bytes==2772&&
          whole.required.gpu_tiles==5544,"Effective edge tile or caller's complete pool reservation was silently changed");
    options=staged();options.transfer_row_alignment=16;const auto compact=plan_export_resources(snapshot,options,unrestricted());
    check(compact.transfer_tile_bytes==768&&compact.required.transfer_staging==2304&&compact.export_request_hash==whole.export_request_hash,
          "Transfer alignment changed output identity or failed to change transfer reservation");
    std::cout<<"export_resource_models cpu_zero_transfer=true clamped_tile_extent=true explicit_pool_slots=true alignment_not_density=true PASS\n";
}
void invalid_and_overflow(){
    const auto scene=fixture();const DensityExportSnapshot snapshot(scene);auto options=staged();
    options.tile_extent[1]=0;rejects([&]{plan_export_resources(snapshot,options,unrestricted());},"Zero tile dimension accepted");
    options=staged();options.transfer_row_alignment=3;rejects([&]{plan_export_resources(snapshot,options,unrestricted());},"Non-power-of-two alignment accepted");
    options=staged();options.transfer_staging_slots=0;rejects([&]{plan_export_resources(snapshot,options,unrestricted());},"Partial staged pipeline reserved no transfer slot");
    options=staged();options.retained_snapshot_bytes=sizeof(DensityExportSnapshot)-1;rejects([&]{plan_export_resources(snapshot,options,unrestricted());},"Reservation smaller than inline snapshot object accepted");
    const auto metadata_payload=snapshot.metadata_json().size()+1;
    const auto known_minimum=sizeof(DensityExportSnapshot)+(metadata_payload>sizeof(std::string)?metadata_payload-sizeof(std::string):0);
    check(known_minimum>sizeof(DensityExportSnapshot),"Known metadata lower-bound fixture is vacuous");
    options=staged();options.retained_snapshot_bytes=known_minimum-1;
    rejects([&]{plan_export_resources(snapshot,options,unrestricted());},"Reservation below demonstrably retained metadata payload accepted");
    options.retained_snapshot_bytes=known_minimum;const auto minimum_plan=plan_export_resources(snapshot,options,unrestricted());
    const auto minimum_json=nlohmann::json::parse(export_resource_plan_json(minimum_plan));
    check(minimum_plan.snapshot_known_lower_bound_bytes==known_minimum&&minimum_plan.required.retained_snapshot==known_minimum&&
          minimum_json["retained_snapshot"]["known_payload_lower_bound_bytes"]==std::to_string(known_minimum)&&
          minimum_json["retained_snapshot"]["dynamic_storage_measured"]==false,
          "Exact known lower bound was rejected or promoted to a full dynamic-memory measurement");
    options=staged();options.retained_snapshot_bytes=std::numeric_limits<std::uint64_t>::max();
    rejects([&]{plan_export_resources(snapshot,options,unrestricted());},"Aggregate reservation uint64 overflow wrapped");
    const DensityExportSnapshot enormous(scene,{1e-5,1});const auto jobs=generation_job_count();
    options=staged();options.tile_extent=enormous.layout().extent;options.gpu_tile_slots=std::numeric_limits<std::uint32_t>::max();
    rejects([&]{plan_export_resources(enormous,options,unrestricted());},"GPU slot multiplication overflow wrapped");
    options=staged();options.tile_extent={1,100000,100000};options.transfer_row_alignment=std::uint32_t{1}<<31;
    rejects([&]{plan_export_resources(enormous,options,unrestricted());},"Row-padded staging multiplication overflow wrapped");
    options=staged();options.gpu_tile_slots=options.transfer_staging_slots=options.cpu_readback_slots=0;
    const auto huge_plan=plan_export_resources(enormous,options,unrestricted());
    check(huge_plan.required.cpu_dense_grid>(std::uint64_t{1}<<53),"Large-count fixture does not exercise JSON integer precision");
    const auto json=nlohmann::json::parse(export_resource_plan_json(huge_plan));
    check(json["resources"]["cpu_dense_grid"]["requested_bytes"].get<std::string>()==std::to_string(huge_plan.required.cpu_dense_grid),
          "Resource byte count lost precision through JSON numbers");
    check(generation_job_count()==jobs,"Huge resource estimate allocated or regenerated a field");
    std::cout<<"export_resource_overflow dimensions_slots_pitch_total=true uint64_decimal_json=true huge_plan_without_grid_allocation=true PASS\n";
}
void identity(){
    auto scene=fixture();const auto jobs=generation_job_count();const DensityExportSnapshot coarse(scene,{1,1}),fine(scene,{.5,2});
    const auto a=plan_export_resources(coarse,staged(),unrestricted()),b=plan_export_resources(fine,staged(),unrestricted());
    check(a.snapshot_hash==b.snapshot_hash&&a.finishing_snapshot_hash==b.finishing_snapshot_hash&&a.export_request_hash!=b.export_request_hash,
          "Resolution/padding changed the frozen authority or failed to change output settings identity");
    auto options=staged();options.tile_extent={2,3,4};options.cpu_readback_slots=7;
    const auto changed_pipeline=plan_export_resources(coarse,options,unrestricted());
    check(changed_pipeline.export_request_hash==a.export_request_hash&&changed_pipeline.snapshot_hash==a.snapshot_hash,
          "Execution-resource policy changed sampled-density identity");
    scene.frozen->generation_version=987;scene.frozen->provenance->settings.algorithm_version=987;refresh_frozen_scene(scene);
    const DensityExportSnapshot unknown(scene);const auto unknown_plan=plan_export_resources(unknown,options,unrestricted());
    check(unknown_plan.snapshot_hash==unknown.snapshot_hash()&&generation_job_count()==jobs,"Unknown generation version was substituted during resource planning");
    std::cout<<"export_resource_identity resolution_changes_request_only=true execution_policy_preserves_request=true unknown_version_zero_jobs=true PASS\n";
}
}
int main(){try{exact_accounting();cpu_and_tile_models();invalid_and_overflow();identity();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
