#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

// Internal forward declarations:
typedef struct sRvkCanvas RvkCanvas;

typedef enum {
  RendPainterType_2D,
  RendPainterType_3D,
} RendPainterType;

ecs_comp_extern_public(RendPainterComp) {
  RendPainterType type;
  RvkCanvas*      canvas;
};

void rend_painter_teardown(EcsWorld* world, EcsEntityId entity);
