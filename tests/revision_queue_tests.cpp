#include "white/revision_queue.hpp"
#include <iostream>
using namespace white;
int main(){int failures=0;auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;};
    RevisionQueue q;Scene scene;auto job=[&](std::uint64_t rev){auto s=scene;s.cloud.cells[0].center.x=double(rev);return DensityJob{rev,density_job_hash(s,{128,128,128}),s,{128,128,128}};};
    q.request(job(1));auto slow=q.start();for(int i=2;i<=1000;++i)q.request(job(i));
    check(q.running()&&q.pending()&&q.coalesced==998,"slow running job plus one latest pending request remains bounded");
    check(!q.finish(1000,job(1000).input_hash)&&q.running(),"out-of-order completion cannot publish or erase active job");
    check(!q.finish(slow->revision,slow->input_hash)&&q.discarded==1,"obsolete result discarded after completion");
    auto newest=q.start();check(newest&&newest->revision==1000,"only newest unstarted revision executes");
    check(q.finish(newest->revision,newest->input_hash)&&!q.pending()&&!q.running(),"current result publishes once");
    auto same=*newest;same.revision=1001;same.scene.camera.position.x+=1;q.request(same);auto recreated=q.start();check(bool(recreated),"completed queue result is not assumed to remain resident");
    // Completion can be rejected by the main thread after the Document changes.
    // A -> B -> A must schedule A again, even if the worker once completed A.
    check(q.finish(recreated->revision,recreated->input_hash),"recreated current result completes");
    q.request(job(1002));q.request(same);auto returned=q.start();
    check(returned&&returned->input_hash==same.input_hash,"A B A after rejected completion requests A again");
    q.request(same);check(q.pending(),"same-hash request survives an active cancellation decision");
    check(q.finish(returned->revision,returned->input_hash)&&!q.pending(),"successful return-to-A clears redundant pending request");
    q.request(job(1003));auto cancelling=q.start();q.request(job(1004));q.request(job(1003));
    check(!q.finish(cancelling->revision,cancelling->input_hash,false),"already-cancelled A is not published after return to A");
    auto retry=q.start();check(retry&&retry->input_hash==cancelling->input_hash,"return-to-A retained for retry after cancellation");
    check(q.finish(retry->revision,retry->input_hash),"retry publishes matching latest input");
    auto a=density_input_hash(scene);auto display=scene;display.exposure_ev=2;display.camera.position.x+=1;display.sun.irradiance.x=3;display.cloud.optics.albedo=0.5;
    check(a==density_input_hash(display),"camera/sun/optics/exposure do not enter density hash");
    display.cloud.detail_seed++;check(a!=density_input_hash(display),"density source enters cache identity");
    auto flags=invalidation(Dirty::density);check(flags.density&&flags.lighting&&flags.hdr&&flags.display,"local density edit invalidates global lighting");
    flags=invalidation(Dirty::display);check(!flags.density&&!flags.lighting&&!flags.hdr&&flags.display,"exposure preserves density and HDR");
    q.request(job(1002));auto failed=q.start();check(!q.finish(failed->revision,failed->input_hash,false),"failed job never publishes");
    return failures?1:0;
}
