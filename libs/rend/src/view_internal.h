#pragma once
#include "geo_box.h"
#include "geo_matrix.h"
#include "rend_settings.h"
#include "scene_tag.h"

typedef struct sRendView {
  EcsEntityId    camera;
  GeoVector      origin;
  SceneTagFilter filter;
  GeoPlane       frustum[4]; // Left, Right, Top, Bottom.
} RendView;

RendView
rend_view_create(EcsEntityId camera, GeoVector origin, const GeoMatrix* viewProj, SceneTagFilter);

/**
 * Get the square distance from view origin to the objects center.
 */
f32 rend_view_dist_sqr(const RendView*, const GeoBox* objAabb);

/**
 * Compute a sorting distance from the view origin to the object center.
 * NOTE: Is not linear but can be used for sorting.
 */
u16 rend_view_sort_dist(const RendView*, const GeoBox* objAabb);

/**
 * Check if an object is visible in the view.
 */
bool rend_view_visible(
    const RendView*, SceneTags objTags, const GeoBox* objAabb, const RendSettingsComp*);
