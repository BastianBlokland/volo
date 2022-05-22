#include "core_annotation.h"
#include "rend_settings.h"

#include "view_internal.h"

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
  return geo_box_intersect_frustum4(objAabb, view->frustum);
}
