#pragma once
#include "dev.h"
#include "geo_color.h"
#include "geo_vector.h"

ecs_comp_extern(DebugTextComp);

/**
 * Add a new debug-text component to the given entity.
 */
DebugTextComp* debug_text_create(EcsWorld*, EcsEntityId entity);

typedef struct {
  GeoColor color;
  u16      fontSize;
} DebugTextOpts;

// clang-format off

#define debug_text(_COMP_, _POS_, _STR_, ...) debug_text_with_opts((_COMP_), (_POS_), (_STR_),     \
      &((DebugTextOpts){                                                                           \
          .color      = geo_color_white,                                                           \
          .fontSize   = 14,                                                                        \
          __VA_ARGS__}))

// clang-format on

void debug_text_with_opts(DebugTextComp*, GeoVector pos, String, const DebugTextOpts*);
