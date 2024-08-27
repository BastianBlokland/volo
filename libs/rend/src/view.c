#include "core_annotation.h"
#include "core_math.h"
#include "rend_settings.h"

#include "view_internal.h"

static bool rend_view_tag_filter(const SceneTagFilter filter, const SceneTags tags) {
  return ((tags & filter.required) == filter.required) && ((tags & filter.illegal) == 0);
}

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

u16 rend_view_sort_dist(const RendView* view, const GeoBox* objAabb) {
  /**
   * The maximum view distance here is a trade-off between supporting object sorting over longer
   * distances (as objects beyond this distance won't have a stable sorting anymore) and having more
   * precision for close objects.
   */
  static const f32 g_maxDistSqrInv = 1.0f / (200.0f * 200.0f);

  const GeoVector objCenter = geo_box_center(objAabb);
  const GeoVector toObj     = geo_vector_sub(objCenter, view->origin);
  const f32       distSqr   = geo_vector_mag_sqr(toObj);
  const f32       frac      = distSqr * g_maxDistSqrInv;
  return (u16)((f32)u16_max * math_min(frac, 1.0f));
}

bool rend_view_visible(
    const RendView*         view,
    const SceneTags         objTags,
    const GeoBox*           objAabb,
    const RendSettingsComp* settings) {

  if (!rend_view_tag_filter(view->filter, objTags)) {
    return false;
  }
  if (UNLIKELY(!(settings->flags & RendFlags_FrustumCulling))) {
    return true;
  }
  return geo_box_overlap_frustum4_approx(objAabb, view->frustum);
}
