#pragma once
#include "dev.h"
#include "geo_color.h"
#include "geo_vector.h"

ecs_comp_extern(DevTextComp);

/**
 * Add a new debug-text component to the given entity.
 */
DevTextComp* dev_text_create(EcsWorld*, EcsEntityId entity);

typedef struct {
  GeoColor color;
  u16      fontSize;
} DebugTextOpts;

// clang-format off

#define dev_text(_COMP_, _POS_, _STR_, ...) dev_text_with_opts((_COMP_), (_POS_), (_STR_),         \
      &((DebugTextOpts){                                                                           \
          .color      = geo_color_white,                                                           \
          .fontSize   = 14,                                                                        \
          __VA_ARGS__}))

// clang-format on

void dev_text_with_opts(DevTextComp*, GeoVector pos, String, const DebugTextOpts*);
