#include "white/revision_queue.hpp"
#include "white/persistence.hpp"
#include "white/developed_scene.hpp"
#include "white/top_lobe_scene.hpp"
#include "white/anvil_scene.hpp"
#include <algorithm>
namespace white {
namespace {std::uint64_t hash_byte(std::uint64_t h,unsigned char b){return (h^b)*1099511628211ull;}}
std::uint64_t density_input_hash(const Scene& scene){
    if(scene.frozen){require_valid(scene);return frozen_density_hash(*scene.frozen);}
    Scene canonical;canonical.schema_version=scene.schema_version;canonical.algorithm_version=scene.algorithm_version;canonical.cloud=scene.cloud;canonical.cloud.optics={};
    if(scene_has_active_anvil(scene)){
        canonical.anvil=*scene.anvil;auto& source=canonical.anvil->cloud.trunk;
        source=DevelopedEvaluationPlan(source).cloud();source.optics={};for(auto& cell:source.cells)cell.shape.source.modifiers.optics={};
        canonical.cloud=anvil_proxy_recipe(*canonical.anvil);
    }else if(scene_has_active_top_lobes(scene)) {
        canonical.top_lobes=*editable_top_lobe_source(scene);
        canonical.top_lobes->trunk=DevelopedEvaluationPlan(canonical.top_lobes->trunk).cloud();
        canonical.top_lobes->trunk.optics={};for(auto& cell:canonical.top_lobes->trunk.cells)cell.shape.source.modifiers.optics={};
        canonical.cloud=top_lobe_proxy_recipe(*canonical.top_lobes);
    } else if(scene_has_multiple_developments(scene)) {
        canonical.developed=DevelopedEvaluationPlan(*editable_developed_source(scene)).cloud();
        canonical.developed->optics={};
        for(auto& cell:canonical.developed->cells)cell.shape.source.modifiers.optics={};
        canonical.cloud=developed_proxy_recipe(*canonical.developed);
    }
    std::sort(canonical.cloud.cells.begin(),canonical.cloud.cells.end(),[](auto& a,auto& b){return a.id<b.id;});
    std::sort(canonical.cloud.cuts.begin(),canonical.cloud.cuts.end(),[](auto& a,auto& b){return a.id<b.id;});
    auto text=scene_json(canonical);std::uint64_t hash=14695981039346656037ull;for(unsigned char c:text)hash=hash_byte(hash,c);return hash;
}
std::uint64_t density_job_hash(const Scene& scene,std::array<std::uint32_t,3> extent){auto h=density_input_hash(scene);for(auto n:extent)for(int b=0;b<4;++b)h=hash_byte(h,static_cast<unsigned char>(n>>(b*8)));return h;}
void RevisionQueue::request(DensityJob job){
    latest_hash_=job.input_hash;
    // Retain the latest request even if the active job has the same hash: it
    // may already have decided to cancel before this request arrived. Success
    // clears the redundant pending snapshot; cancellation leaves it retryable.
    if(pending_)++coalesced;
    pending_=std::move(job);
}
std::optional<DensityJob> RevisionQueue::start(){if(running_||!pending_)return {};running_=std::move(pending_);pending_.reset();return running_;}
bool RevisionQueue::finish(std::uint64_t revision,std::uint64_t hash,bool successful){
    if(!running_||running_->revision!=revision||running_->input_hash!=hash)return false;
    running_.reset();if(successful&&hash==latest_hash_){pending_.reset();++published;return true;}
    ++discarded;return false;
}
Invalidation invalidation(Dirty dirty){
    const bool density=has(dirty,Dirty::density),light=density||has(dirty,Dirty::optics)||has(dirty,Dirty::sun);
    const bool hdr=light||has(dirty,Dirty::camera);return {density,light,hdr,hdr||has(dirty,Dirty::display)};
}
}
