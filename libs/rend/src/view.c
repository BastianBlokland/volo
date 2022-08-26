#include "core_annotation.h"
#include "rend_settings.h"

#include "view_internal.h"

RendView rend_view_create(
    const EcsEntityId    camera,
    const GeoVector      origin,
    const GeoMatrix*     viewProj,
    const SceneTagFilter filter) {
  RendView result = {
      .camera = camera,
      .filter = filter,
      .origin = origin,
  };
  geo_matrix_frustum4(viewProj, result.frustum);
  return result;
}

f32 rend_view_dist_sqr(const RendView* view, const GeoBox* objAabb) {
  const GeoVector objCenter = geo_box_center(objAabb);
  const GeoVector toObj     = geo_vector_sub(objCenter, view->origin);
  return geo_vector_mag_sqr(toObj);
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
  return geo_box_intersect_frustum4_approx(objAabb, view->frustum);
}
