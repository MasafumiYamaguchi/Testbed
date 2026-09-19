#include "white/reference.hpp"
#include "white/density.hpp"
#include "white/persistence.hpp"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
std::uint64_t integer(const std::string& s){std::uint64_t v=0;const auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),v);if(e!=std::errc{}||p!=s.data()+s.size())throw std::invalid_argument("Invalid unsigned number: "+s);return v;}
unsigned small_integer(const std::string& s){const auto value=integer(s);if(value>std::numeric_limits<unsigned>::max())throw std::invalid_argument("Number too large");return unsigned(value);}
}
int main(int argc,char** argv){try{
    white::ReferenceSettings settings;unsigned grid=32,checkpoint=0;std::filesystem::path output,recipe;std::string fixture="cloud";
    for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--help"){std::cout<<"white_reference --output NEW_DIRECTORY [--recipe SCENE | --fixture cloud|empty|absorption|thin|thick|internal|albedo-one] [--mode single|multiple] [--width 32 --height 18 --grid 32 --samples 64 --seed 42 --bounce-limit 64 --event-limit 100000 --checkpoint-every 32 --fixed-pixels]\n";return 0;}
        if(arg=="--fixed-pixels"){settings.pixel_jitter=false;continue;}
        if(i+1>=argc)throw std::invalid_argument("Missing option value: "+arg);
        const std::string value=argv[++i];
        if(arg=="--output")output=value;else if(arg=="--recipe")recipe=value;else if(arg=="--fixture")fixture=value;
        else if(arg=="--width")settings.width=small_integer(value);else if(arg=="--height")settings.height=small_integer(value);else if(arg=="--samples")settings.samples=small_integer(value);else if(arg=="--seed")settings.seed=integer(value);else if(arg=="--grid")grid=small_integer(value);
        else if(arg=="--bounce-limit")settings.bounce_limit=small_integer(value);else if(arg=="--event-limit")settings.event_limit=integer(value);else if(arg=="--checkpoint-every")checkpoint=small_integer(value);
        else if(arg=="--mode"){if(value=="single")settings.mode=white::ReferenceMode::single_scattering;else if(value=="multiple")settings.mode=white::ReferenceMode::multiple_scattering;else throw std::invalid_argument("Unknown reference mode");}
        else throw std::invalid_argument("Unknown option: "+arg);
    }
    if(output.empty()||std::filesystem::exists(output))throw std::invalid_argument("Reference output must be a new directory");
    if(grid<2||grid>128)throw std::invalid_argument("Grid must be 2..128");
    if(checkpoint>settings.samples)throw std::invalid_argument("Checkpoint interval exceeds samples");
    settings.roulette_start=std::min(settings.roulette_start,settings.bounce_limit);
    auto scene=recipe.empty()?white::fixture_scene(2):white::read_scene(recipe);
    if(recipe.empty()){
        if(fixture=="empty")scene.cloud.density=0;
        else if(fixture=="absorption")scene.cloud.optics.albedo=0;
        else if(fixture=="thin")scene.cloud.optics.extinction_scale=.005;
        else if(fixture=="thick"){scene.cloud.optics.extinction_scale=.1;scene.cloud.optics.albedo=.98;}
        else if(fixture=="internal"){scene.camera.position={0,20,-2};scene.camera.target={0,20,10};}
        else if(fixture=="albedo-one")scene.cloud.optics.albedo=1;
        else if(fixture!="cloud")throw std::invalid_argument("Unknown reference fixture");
    }
    const auto frozen=white::bake_reference_snapshot(scene,grid);white::ReferenceRenderer renderer(frozen,settings);
    std::cout<<"samples_per_pixel,seed,mean_radiance,mean_pixel_estimator_variance,complete_paths,partial_paths,collision_events,tracking_events\n";
    const unsigned chunk=checkpoint?checkpoint:std::min(settings.samples,32u);
    while(renderer.samples_completed()<settings.samples){renderer.advance(std::min(chunk,settings.samples-renderer.samples_completed()));const auto& t=renderer.statistics();
        std::cout<<renderer.samples_completed()<<','<<settings.seed<<','<<renderer.mean_radiance()<<','<<renderer.mean_estimator_variance()<<','<<t.complete_paths<<','<<t.attempted_paths-t.complete_paths<<','<<t.collision_events<<','<<t.tracking_events<<'\n';
        if(checkpoint||renderer.samples_completed()==settings.samples){white::HdrMetadata meta;meta.scene=scene;meta.frame=renderer.samples_completed();meta.samples_per_pixel=renderer.samples_completed();meta.cached=true;meta.density_extent=frozen.grid().extent;meta.provenance_json=white::reference_report_json(renderer);
            auto destination=output;if(renderer.samples_completed()!=settings.samples)destination+=std::string("-spp-")+std::to_string(renderer.samples_completed());
            white::export_hdr(renderer.image(),meta,destination);
        }
    }
    if(!renderer.statistics().complete()){std::cerr<<"PARTIAL reference: inspect metadata safety/numerical counters; output must not be used as complete reference\n";return 2;}
    return 0;
}catch(const std::exception& e){std::cerr<<"Reference failed: "<<e.what()<<'\n';return 1;}}
