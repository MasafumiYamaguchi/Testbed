#pragma once
#include "white/document.hpp"
#include <array>
#include <optional>
namespace white {
using Matrix4=std::array<float,16>;
struct Ray {Vec3 origin,direction;};
Ray camera_ray(const Camera&,double normalized_x,double normalized_y,double aspect);
Matrix4 camera_view(const Camera&);
Matrix4 camera_projection(const Camera&,double aspect);
Matrix4 primitive_matrix(const Transform&,Vec3 center,Vec3 radii);
void primitive_from_matrix(const Transform&,const Matrix4&,Vec3& center,Vec3& radii);
std::optional<Id> pick_primitive(const CloudRecipe&,Ray,bool cuts);
}
