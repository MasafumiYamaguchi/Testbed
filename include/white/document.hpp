#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace white {
using Id = std::uint64_t;
struct Vec3 {
    double x=0,y=0,z=0;
    bool operator==(const Vec3&) const = default;
};
Vec3 operator+(Vec3 a,Vec3 b);
Vec3 operator-(Vec3 a,Vec3 b);
Vec3 operator*(Vec3 a,double b);
double dot(Vec3 a,Vec3 b);
struct Quaternion {
    double x=0,y=0,z=0,w=1;
    bool operator==(const Quaternion&) const = default;
};
struct Transform {
    Vec3 translation{};
    Quaternion rotation{};
    Vec3 scale{1,1,1};
    bool operator==(const Transform&) const = default;
};
Vec3 local_to_world(const Transform&,Vec3);
Vec3 world_to_local(const Transform&,Vec3);
struct Bounds {
    Vec3 min{-100,-100,-100},max{100,100,100};
    bool operator==(const Bounds&) const = default;
};
struct GridLayout {
    Bounds local_bounds;
    std::array<std::uint32_t,3> extent{128,128,128};
};
Vec3 index_to_local(const GridLayout&,Vec3 index);
Vec3 local_to_index(const GridLayout&,Vec3 point);
struct Cell {
    Id id=0;
    Vec3 center{0,20,0},radii{20,30,20};
    std::uint64_t structure_seed=0;
    bool operator==(const Cell&) const = default;
};
struct Cut {
    Id id=0;
    Vec3 center{},radii{5,5,5};
    double transition=1;
    bool operator==(const Cut&) const = default;
};
struct BasePlane {
    bool enabled=true;
    double height=0,transition=2;
    bool operator==(const BasePlane&) const = default;
};
struct Optics {
    double extinction_scale=0.02; // 1/metre
    double albedo=0.9;
    bool operator==(const Optics&) const = default;
};
struct NoiseSettings {
    Vec3 origin{}; // saved cloud-local metres; never normalized by current bounds
    double medium_frequency=0.12,medium_strength=0;
    double micro_frequency=0.6,micro_erosion=0; // erosion is local metres
    double warp_frequency=0.035,warp_amplitude=0; // bounded displacement magnitude
    bool operator==(const NoiseSettings&) const = default;
};
struct CloudRecipe {
    Id id=1;
    Transform transform{};
    std::vector<Cell> cells{{2}};
    std::vector<Cut> cuts;
    Bounds envelope{};
    BasePlane base{};
    double density=1,blend_width=2,overlap=0;
    std::uint64_t structure_seed=42,detail_seed=17;
    Optics optics{};
    NoiseSettings noise{};
    bool operator==(const CloudRecipe&) const = default;
};
struct Camera {
    Vec3 position{120,70,120},target{0,20,0},up{0,1,0};
    double vertical_fov_degrees=45,near_plane=0.1,far_plane=10000;
    bool operator==(const Camera&) const = default;
};
struct Sun {
    Vec3 direction_to_light{0,1,0};
    Vec3 irradiance{1,1,1};
    bool operator==(const Sun&) const = default;
};
struct Scene {
    std::uint32_t schema_version=2,algorithm_version=2;
    CloudRecipe cloud{};
    Camera camera{};
    Sun sun{};
    double exposure_ev=0;
    bool operator==(const Scene&) const = default;
};
enum class Dirty : std::uint32_t {none=0,density=1,optics=2,sun=4,camera=8,display=16};
Dirty operator|(Dirty,Dirty);
bool has(Dirty,Dirty);
Dirty classify_change(const Scene&,const Scene&);
std::vector<std::string> validate(const Scene&);
void require_valid(const Scene&);
Id next_id(const Scene&);
std::uint64_t cell_random_key(const CloudRecipe&,const Cell&);

// The revision belongs to the editing session, not a GPU resource or saved
// scene. Replacement validates first and publishes once with a newer revision.
class Document {
public:
    explicit Document(Scene scene = {});
    const Scene& scene() const {return scene_;}
    std::uint64_t revision() const {return revision_;}
    Dirty last_change() const {return last_change_;}
    std::chrono::steady_clock::time_point changed_at()const{return changed_at_;}
    bool replace(Scene scene);
private:
    Scene scene_;
    std::uint64_t revision_=1;
    Dirty last_change_=Dirty::none;
    std::chrono::steady_clock::time_point changed_at_=std::chrono::steady_clock::now();
};
}
