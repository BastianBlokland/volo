#include "core_annotation.h"
#include "rend_settings.h"

#include "view_internal.h"

static bool rend_frustum_intersect(const GeoPlane frustum[4], const GeoBox* aabb) {
  for (usize i = 0; i != 4; ++i) {
    const GeoVector max = {
        .x = frustum[i].normal.x > 0 ? aabb->max.x : aabb->min.x,
        .y = frustum[i].normal.y > 0 ? aabb->max.y : aabb->min.y,
        .z = frustum[i].normal.z > 0 ? aabb->max.z : aabb->min.z,
    };
    if (-geo_vector_dot(frustum[i].normal, max) > frustum[i].distance) {
      return false;
    }
  }
  return true;
}

RendView
rend_view_create(const EcsEntityId camera, const GeoMatrix* viewProj, const SceneTagFilter filter) {
  RendView result = {
      .camera = camera,
      .filter = filter,
  };
  geo_matrix_frustum4(viewProj, result.frustum);
  return result;
}

bool rend_view_visible(
    const RendView*         view,
    const SceneTags         objTags,
    const GeoBox*           objAabb,
    const RendSettingsComp* settings) {

  if (!scene_tag_filter(view->filter, objTags)) {
    return false;
  }
  if (UNLIKELY(!(settings->flags & RendFlags_FrustumCulling))) {
    return true;
  }
  if (UNLIKELY(geo_box_is_inverted3(objAabb))) {
    /**
     * Objects with inverted bounds should be always drawn.
     * TODO: Investigate if we can get rid of this check and handle it in the intersect.
     */
    return true;
  }
  return rend_frustum_intersect(view->frustum, objAabb);
}
