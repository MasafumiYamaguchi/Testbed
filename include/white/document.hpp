#pragma once
#include <array>
#include "white/preview_approx.hpp"
#include "white/density_profile.hpp"
#include <chrono>
#include <cstddef>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>
#include <variant>

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
    double g=0;
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
    AltitudeDensityProfile altitude_density{};
    bool operator==(const CloudRecipe&) const = default;
};
inline constexpr std::uint32_t cumulonimbus_contract_version=1;
inline constexpr std::size_t cumulonimbus_generated_cells=5;

struct CumulonimbusParameters {
    double width=80,height=120,cloud_base=0; // cloud-local metres
    Vec3 growth_direction{0,1,0}; // unit upward vector, y >= 0.2
    double density=1; // dimensionless density multiplier
    std::uint64_t structure_seed=42,detail_seed=17;
    bool operator==(const CumulonimbusParameters&)const=default;
};
// Offsets remain cloud-local metres when width/height change; scales multiply
// the generated radii. The edit is attached to an ID, never a vector index.
struct CumulonimbusCellAdjustment {
    Id cell_id=0;
    Vec3 center_offset{};
    Vec3 radius_scale{1,1,1};
    std::optional<std::uint64_t> structure_seed{};
    bool operator==(const CumulonimbusCellAdjustment&)const=default;
};
struct CumulonimbusModifiers {
    Transform transform{};
    Optics optics{};
    NoiseSettings noise{{},0.12,0.2,0.6,0.5,0.035,1};
    double blend_width=4,overlap=0.15,base_transition=2;
    bool base_enabled=true;
    std::vector<Cell> manual_cells;
    std::vector<Cut> cuts;
    bool operator==(const CumulonimbusModifiers&)const=default;
};
// This source model is authoritative. Graphs/recipes are derived snapshots;
// never store an independently editable generated graph alongside this model.
struct CumulonimbusGroup {
    std::uint32_t contract_version=cumulonimbus_contract_version;
    Id cloud_id=1;
    std::array<Id,cumulonimbus_generated_cells> cell_ids{2,3,4,5,6};
    CumulonimbusParameters parameters{};
    std::vector<CumulonimbusCellAdjustment> cell_adjustments;
    CumulonimbusModifiers modifiers{};
    bool operator==(const CumulonimbusGroup&)const=default;
};

inline constexpr std::uint32_t centerline_contract_version=1;
inline constexpr std::size_t max_centerline_points=6,max_centerline_profile_points=8;
inline constexpr double max_centerline_curvature=0.1; // conservative bound, inverse local metre

// Control and profile IDs each use a separate namespace from stable Cell IDs.
// t is normalized height above the fixed local cloud base, not arc length.
// offsets are absolute local metres; y must be zero to preserve monotone height.
struct CenterlinePoint {
    Id id=0;
    double t=0;
    Vec3 offset{};
    bool operator==(const CenterlinePoint&)const=default;
};
struct CenterlineProfilePoint {
    Id id=0;
    double t=0,radius_scale=1,density_scale=1;
    bool operator==(const CenterlineProfilePoint&)const=default;
};
struct CenterlineShape {
    std::uint32_t contract_version=centerline_contract_version;
    CumulonimbusGroup source{};
    std::vector<CenterlinePoint> points{{1,0,{}},{2,0.5,{}},{3,1,{}}};
    std::vector<CenterlineProfilePoint> profile{{1,0,1,1},{2,1,1,1}};
    bool operator==(const CenterlineShape&)const=default;
};
inline constexpr std::uint32_t developed_cloud_contract_version=1;
inline constexpr std::size_t max_developed_cells=2,max_developed_primitives=8;
// Each entry is a complete independently editable developed curve. Roles refer
// to the five stable generator slots, not to vector positions in another cell.
struct DevelopedCell {
    Id id=0;
    CenterlineShape shape{};
    Vec3 translation{}; // Object-local metres; carries the entire local field.
    std::vector<unsigned> roles{0,1,2}; // Explicit 3..5-lobe budget, includes 0/1/2.
    bool operator==(const DevelopedCell&)const=default;
};
struct DevelopedCloud {
    std::uint32_t contract_version=developed_cloud_contract_version;
    Id id=1;
    Transform transform{};
    Optics optics{}; // One participating medium; no optical coefficient sums.
    double fusion_width=4,overlap=0.15; // Inter-development metres and [0,1].
    std::vector<DevelopedCell> cells; // Empty is a valid transparent cloud.
    bool operator==(const DevelopedCloud&)const=default;
};
inline constexpr std::uint32_t top_lobe_contract_version=1;
inline constexpr unsigned max_top_lobe_depth=2,max_top_lobes=3;
enum class TopLobeMode {off,parent,children};
struct TopLobeSettings {
    TopLobeMode mode=TopLobeMode::off;
    unsigned depth_limit=2,child_limit=2;
    double top_start=.65,parent_radius=18,child_radius_ratio=.55;
    double hierarchy_density=1,mask_transition=6,fusion_width=4,density_scale=.75;
    Vec3 growth_direction{0,1,0};
    bool operator==(const TopLobeSettings&)const=default;
};
// One authority: a developed source plus a typed finite hierarchy generator.
// This is not a CenterlineShape pretending that one role is a developed curve.
struct TopLobeSource {
    std::uint32_t contract_version=top_lobe_contract_version;
    DevelopedCloud trunk;
    Id target_cell=0,field_id=0;
    std::array<Id,max_top_lobes> lobe_ids{}; // Reserved in every comparison mode.
    TopLobeSettings settings{};
    bool operator==(const TopLobeSource&)const=default;
};
inline constexpr std::uint32_t anvil_contract_version=1;
struct AnvilSettings {
    bool enabled=false,follow_wind=true;
    double start_height=84,thickness=24,width=100,extension=80;
    Vec3 direction{1,0,0}; // Unit horizontal vector in development-local space.
    double shear=0,edge_fade=1,density_scale=.8;
    bool operator==(const AnvilSettings&)const=default;
};
struct AnvilSource {
    std::uint32_t contract_version=anvil_contract_version;
    TopLobeSource cloud; // One authority, includes the target and fixed top IDs.
    AnvilSettings settings{};
    bool operator==(const AnvilSource&)const=default;
};
inline constexpr std::uint32_t generation_algorithm_version=1;
inline constexpr std::size_t max_wind_knots=6;
struct WindKnot {
    double altitude=0; // Normalized fixed reference altitude, not current bounds.
    Vec3 displacement{}; // Horizontal object-local metres at stage 1, not m/s.
    bool operator==(const WindKnot&)const=default;
};
struct DevelopmentGrowth {
    Id cell_id=0;
    double start_stage=0,amount=1;
    std::vector<Id> pinned_controls; // Preserve guide X/Z in the initial frame.
    bool operator==(const DevelopmentGrowth&)const=default;
};
struct GenerationSettings {
    std::uint32_t algorithm_version=generation_algorithm_version;
    bool enabled=true;
    double stage=1,initial_height_fraction=.4;
    double wind_base=0,wind_height=120;
    std::vector<WindKnot> wind{{0,{}},{1,{}}};
    Vec3 reference_translation{}; // Bulk motion, separately applied at stage 1.
    std::vector<DevelopmentGrowth> cells;
    bool operator==(const GenerationSettings&)const=default;
};
// A provenance recipe is an immutable input for an explicit future job. It is
// never a second editable authority for the fixed field.
using GenerationRecipe=std::variant<CloudRecipe,CumulonimbusGroup,CenterlineShape,
    DevelopedCloud,TopLobeSource,AnvilSource>;
struct GenerationProvenance {
    GenerationRecipe initial;
    GenerationSettings settings;
    bool operator==(const GenerationProvenance&)const=default;
};
inline constexpr std::uint32_t frozen_cloud_contract_version=1;
struct FrozenDetailLayers {
    bool base=true,macro=true,medium=true,micro=true;
    bool operator==(const FrozenDetailLayers&)const=default;
};
struct FrozenField {
    Id development_id=0;
    CloudRecipe recipe; // Evaluated primitives; no seed-driven structure generator.
    Vec3 translation{};
    FrozenDetailLayers layers;
    bool operator==(const FrozenField&)const=default;
};
struct FrozenCurve {
    Id development_id=0;
    double base=0,height=120;
    Vec3 growth_direction{0,1,0};
    std::vector<CenterlinePoint> points;
    std::vector<CenterlineProfilePoint> profile;
    bool operator==(const FrozenCurve&)const=default;
};
struct FrozenHierarchyNode {
    Id id=0,parent_id=0;
    unsigned depth=0;
    // Geometry lives only in the corresponding field Recipe Cell.
    bool operator==(const FrozenHierarchyNode&)const=default;
};
struct FrozenAnvil {
    Vec3 center{},direction{1,0,0};
    double along_radius=50,cross_radius=50,half_thickness=12;
    double shear=0,edge_fade=1,density_scale=.8,start_height=84;
    bool operator==(const FrozenAnvil&)const=default;
};
struct FrozenCloudState {
    std::uint32_t contract_version=frozen_cloud_contract_version;
    Id id=1;
    std::string selection_kind="development_stage",selection_unit="dimensionless";
    double selection_value=1;
    std::uint32_t generation_version=generation_algorithm_version;
    std::uint64_t generation_input_hash=0,content_hash=0,payload_hash=0;
    Transform transform{};
    Optics optics{};
    std::vector<FrozenField> fields;
    std::vector<FrozenCurve> curves;
    double fusion_width=0,overlap=0;
    bool top_enabled=false;
    double top_boundary=0;
    unsigned top_mode=0;
    std::vector<FrozenHierarchyNode> hierarchy;
    std::optional<FrozenAnvil> anvil;
    Bounds support{{-1,-1,-1},{1,1,1}};
    double rho_max=0;
    std::optional<GenerationProvenance> provenance;
    bool operator==(const FrozenCloudState&)const=default;
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
    std::uint32_t schema_version=10,algorithm_version=3;
    CloudRecipe cloud{}; // Derived render snapshot when cumulonimbus is present.
    std::optional<CumulonimbusGroup> cumulonimbus{};
    std::optional<CenterlineShape> centerline{}; // Exclusive source alternative.
    std::optional<TopLobeSource> top_lobes{};
    std::optional<AnvilSource> anvil{};
    std::optional<DevelopedCloud> developed{}; // Complete source; cloud is a validated metadata proxy for >1 group.
    std::optional<FrozenCloudState> frozen{}; // Exclusive, authoritative evaluated field.
    Camera camera{};
    Sun sun{};
    double exposure_ev=0;
    PreviewApproxSettings preview_approx{};
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
