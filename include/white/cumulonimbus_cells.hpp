#pragma once
#include "white/cumulonimbus.hpp"

namespace white {
// Version-1 source edits. Generated slots remain part of the prefab; this
// adapter does not turn its five ellipsoids into independent developed curves.
enum class CumulonimbusCellKind {generated,manual};
struct CumulonimbusCellCapabilities {
    CumulonimbusCellKind kind;
    bool can_remove;
    std::size_t remaining_manual_slots;
};
CumulonimbusCellCapabilities cumulonimbus_cell_capabilities(const CumulonimbusGroup&,Id);
// Includes cloud, generated cells, manual cells and cuts; never reuses a live ID.
Id next_cumulonimbus_cell_id(const CumulonimbusGroup&);
// A deleted/missing selection clears instead of silently selecting another cell.
std::optional<Id> cumulonimbus_cell_selection(const CumulonimbusGroup&,std::optional<Id>);

// Retain copies the numeric local seed. The duplicate has a new stable ID,
// hence a distinct cell_random_key under density algorithm 2. This does not
// promise identical random geometry. Regenerate requires an explicit new seed.
enum class CumulonimbusDuplicateSeed {retain,regenerate};
struct CumulonimbusAddCell {Cell cell;};
struct CumulonimbusDuplicateCell {
    Id source_id,new_id;
    CumulonimbusDuplicateSeed seed_policy;
    std::uint64_t replacement_seed=0;
    CumulonimbusDuplicateCell(Id source,Id copy,CumulonimbusDuplicateSeed policy,std::uint64_t seed=0)
        :source_id(source),new_id(copy),seed_policy(policy),replacement_seed(seed){}
};
struct CumulonimbusRemoveCell {Id id;};
// Absolute local geometry permits the same command from numbers or a gizmo.
struct CumulonimbusMoveCell {Id id;Vec3 local_center;};
struct CumulonimbusScaleCell {Id id;Vec3 local_radii;};
struct CumulonimbusReseedCell {Id id;std::uint64_t seed;};
using CumulonimbusCellCommand=std::variant<CumulonimbusAddCell,CumulonimbusDuplicateCell,
    CumulonimbusRemoveCell,CumulonimbusMoveCell,CumulonimbusScaleCell,CumulonimbusReseedCell>;

// Pure validated replacement; the caller's existing document/session owns Undo.
// Canonicalizes adjustment/manual-cell storage by ID, preserving generated role
// order, global parameters, cuts, optics, noise and other source modifiers.
// Invalid commands throw before publication and leave the input unchanged.
CumulonimbusGroup command_cumulonimbus_cell(const CumulonimbusGroup&,const CumulonimbusCellCommand&);
// Scene adapter preserves a saved centerline/profile and applies local geometry
// commands to the displayed deformed cell. Duplicate captures that geometry as
// a manual cell; the object's altitude density profile still applies to it.
Scene scene_with_cumulonimbus_cell_command(Scene,const CumulonimbusCellCommand&);
}
