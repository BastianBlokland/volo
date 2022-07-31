#include "core_math.h"
#include "geo_box.h"
#include "geo_box_rotated.h"

static GeoVector geo_rotate_around(const GeoVector point, const GeoQuat rot, const GeoVector v) {
  return geo_vector_add(point, geo_quat_rotate(rot, geo_vector_sub(v, point)));
}

static GeoRay geo_box_rotated_local_ray(const GeoBoxRotated* box, const GeoRay* worldRay) {
  const GeoVector boxCenter      = geo_box_center(&box->box);
  const GeoQuat   boxInvRotation = geo_quat_inverse(box->rotation);
  return (GeoRay){
      .point = geo_rotate_around(boxCenter, boxInvRotation, worldRay->point),
      .dir   = geo_quat_rotate(boxInvRotation, worldRay->dir),
  };
}

f32 geo_box_rotated_intersect_ray(
    const GeoBoxRotated* box, const GeoRay* ray, GeoVector* outNormal) {
  /**
   * Transform the ray to the local space of the box and perform the intersection on the non-rotated
   * box.
   */

  const GeoRay localRay = geo_box_rotated_local_ray(box, ray);
  const f32    rayHitT  = geo_box_intersect_ray(&box->box, &localRay, outNormal);
  if (rayHitT >= 0.0f) {
    /**
     * Transform the surface normal back to world-space.
     */
    *outNormal = geo_quat_rotate(box->rotation, *outNormal);
  }

  return rayHitT;
}
