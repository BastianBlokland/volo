#pragma once
#include "geo_box.h"
#include "geo_matrix.h"
#include "rend_settings.h"
#include "scene_tag.h"

typedef struct {
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
 * Check if an object is visible in the view.
 */
bool rend_view_visible(
    const RendView*, SceneTags objTags, const GeoBox* objAabb, const RendSettingsComp*);
