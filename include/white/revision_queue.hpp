#pragma once
#include "white/document.hpp"
#include <optional>
namespace white {
struct DensityJob {
    std::uint64_t revision=0,input_hash=0;
    Scene scene;
    std::array<std::uint32_t,3> extent{};
    std::uint64_t previous_bytes=0,other_gpu_bytes=0;
};
std::uint64_t density_input_hash(const Scene&);
std::uint64_t density_job_hash(const Scene&,std::array<std::uint32_t,3>);
// Externally synchronized. At most one pending and one running immutable job.
class RevisionQueue {
public:
    void request(DensityJob);
    std::optional<DensityJob> start();
    bool finish(std::uint64_t revision,std::uint64_t hash,bool successful=true);
    bool current(std::uint64_t hash)const{return latest_hash_==hash;}
    bool pending()const{return pending_.has_value();}
    bool running()const{return running_.has_value();}
    std::uint64_t coalesced=0,discarded=0,published=0;
private:
    std::optional<DensityJob> pending_,running_;
    std::optional<std::uint64_t> published_hash_;
    std::uint64_t latest_hash_=0;
};
struct Invalidation {bool density=false,lighting=false,hdr=false,display=false;};
Invalidation invalidation(Dirty);
}
