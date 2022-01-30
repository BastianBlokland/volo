#pragma once
#include "geo_box.h"
#include "geo_matrix.h"
#include "scene_tag.h"

typedef struct {
  SceneTagFilter filter;
  GeoPlane       frustum[4]; // Left, Right, Top, Bottom.
} RendView;

RendView rend_view_create(const GeoMatrix* viewProj, SceneTagFilter);

/**
 * Check if an object is visible in the view.
 */
bool rend_view_visible(const RendView*, SceneTags objTags, const GeoBox* objAabb);
