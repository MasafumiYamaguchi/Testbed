#include <openvdb/openvdb.h>
#include <openvdb/io/File.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace fs=std::filesystem;
namespace {
constexpr int side=32;
constexpr double voxel=0.75;
const openvdb::Vec3d origin(11.25,-3.5,5.75);
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
float reference(int x,int y,int z){return x<0||y<0||z<0||x>=side||y>=side||z>=side?0.f:float(1+x+2*y+4*z)/256.f;}
struct OwnedDirectory {
    fs::path path;
    explicit OwnedDirectory(const fs::path& parent){
        for(unsigned i=0;i<32;++i){path=parent/("white-vdb-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));if(fs::create_directory(path))return;}
        throw std::runtime_error("Cannot reserve isolated temporary directory");
    }
    ~OwnedDirectory(){std::error_code ec;fs::remove_all(path,ec);}
};
openvdb::FloatGrid::Ptr make_grid(){
    auto grid=openvdb::FloatGrid::create(0.f);grid->setName("density");grid->setGridClass(openvdb::GRID_FOG_VOLUME);
    auto transform=openvdb::math::Transform::createLinearTransform(voxel);transform->postTranslate(origin);grid->setTransform(transform);
    auto values=grid->getAccessor();for(int z=0;z<side;++z)for(int y=0;y<side;++y)for(int x=0;x<side;++x)values.setValue(openvdb::Coord(x,y,z),reference(x,y,z));
    return grid;
}
void verify(const fs::path& path){
    openvdb::io::File file(path.string());file.open(false);auto grids=file.getGrids();file.close();
    check(grids->size()==1,"Expected exactly one grid");auto grid=openvdb::gridPtrCast<openvdb::FloatGrid>((*grids)[0]);
    check(bool(grid),"Expected FloatGrid");check(grid->getName()=="density","Wrong grid name");check(grid->getGridClass()==openvdb::GRID_FOG_VOLUME,"Wrong grid class");check(grid->background()==0,"Background must be zero");
    auto accessor=grid->getConstAccessor();double value_error=0,world_error=0;
    for(int z=-1;z<=side;++z)for(int y=-1;y<=side;++y)for(int x=-1;x<=side;++x){
        const float value=accessor.getValue(openvdb::Coord(x,y,z));check(std::isfinite(value)&&value>=0,"Invalid density");value_error=std::max(value_error,std::abs(double(value)-reference(x,y,z)));
        const auto world=grid->indexToWorld(openvdb::Vec3d(x,y,z));const auto expected=origin+openvdb::Vec3d(x*voxel,y*voxel,z*voxel);world_error=std::max(world_error,(world-expected).length());
    }
    check(value_error==0&&world_error<1e-12,"Round-trip value/world mismatch");check(grid->activeVoxelCount()==side*side*side,"Unexpected active topology");
    std::cout<<"grid=density class=fog type=float background=0 active="<<grid->activeVoxelCount()<<" samples="<<34*34*34<<" value_max_error="<<value_error<<" world_max_error_m="<<world_error<<" voxel_size_m="<<voxel<<" origin_m="<<origin<<" PASS\n";
}
void write_new(const fs::path& destination,bool fail_before_publish=false){
    check(!fs::exists(destination),"Destination exists; refusing to overwrite");
    const auto parent=destination.parent_path().empty()?fs::path("."):destination.parent_path();OwnedDirectory staging(parent);
    const auto temporary=staging.path/"pending.vdb";openvdb::io::File file(temporary.string());file.write({make_grid()});file.close();verify(temporary);
    if(fail_before_publish)throw std::runtime_error("Injected failure before publication");
    // Same-filesystem hard-link creation atomically refuses an existing name.
    // The complete, closed file is published; staging cleanup removes its link.
    fs::create_hard_link(temporary,destination);
}
std::string bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void self_test(const fs::path& output){
    fs::create_directories(output);const auto run=output/("run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(run),"Cannot reserve evidence run");
    const auto destination=run/"density.vdb";write_new(destination);verify(destination);const auto original=bytes(destination);
    bool refused=false;try{write_new(destination);}catch(const std::exception&){refused=true;}check(refused&&bytes(destination)==original,"Collision modified the existing file");
    refused=false;try{write_new(run/"interrupted.vdb",true);}catch(const std::exception&){refused=true;}check(refused&&!fs::exists(run/"interrupted.vdb"),"Partial result was published");
    const auto blocker=run/"not-a-directory";{std::ofstream stream(blocker);stream<<"preserve this file";}
    refused=false;try{write_new(blocker/"density.vdb");}catch(const std::exception&){refused=true;}check(refused&&bytes(blocker)=="preserve this file","Invalid destination path damaged existing data");
    for(const auto& entry:fs::directory_iterator(run))check(!entry.path().filename().string().starts_with("white-vdb-"),"Temporary directory leaked");
    std::cout<<"collision_preserved=true interrupted_publish_absent=true invalid_path_preserved=true temp_cleanup=true PASS\nartifact="<<destination.string()<<'\n';
}
}
int main(int argc,char** argv){
    try{openvdb::initialize();std::cout<<"OpenVDB="<<OPENVDB_LIBRARY_VERSION_STRING<<" ABI="<<OPENVDB_LIBRARY_ABI_VERSION_STRING<<'\n';
        if(argc==3&&std::string(argv[1])=="--self-test")self_test(argv[2]);
        else if(argc==3&&std::string(argv[1])=="--write"){write_new(argv[2]);verify(argv[2]);}
        else {std::cerr<<"Usage: white_vdb_spike --self-test OUTPUT_DIR | --write NEW_FILE.vdb\n";return 2;}
        openvdb::uninitialize();return 0;
    }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
