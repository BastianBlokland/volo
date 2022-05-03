#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "debug_register.h"
#include "debug_shape.h"
#include "ecs_world.h"
#include "rend_draw.h"

typedef enum {
  DebugShapeType_BoxFill,
  DebugShapeType_BoxWire,
  DebugShapeType_SphereFill,
  DebugShapeType_SphereWire,

  DebugShapeType_Count,
} DebugShapeType;

typedef struct {
  GeoBox   box;
  GeoQuat  rot;
  GeoColor color;
} DebugShapeBox;

typedef struct {
  GeoVector position;
  f32       radius;
  GeoColor  color;
} DebugShapeSphere;

typedef struct {
  DebugShapeType type;
  union {
    DebugShapeBox    data_box;
    DebugShapeSphere data_sphere;
  };
} DebugShape;

static const String g_debugGraphics[DebugShapeType_Count] = {
    [DebugShapeType_BoxFill]    = string_static("graphics/debug/shape_box_fill.gra"),
    [DebugShapeType_BoxWire]    = string_static("graphics/debug/shape_box_wire.gra"),
    [DebugShapeType_SphereFill] = string_static("graphics/debug/shape_sphere_fill.gra"),
    [DebugShapeType_SphereWire] = string_static("graphics/debug/shape_sphere_wire.gra"),
};

ecs_comp_define(DebugShapeRendererComp) { EcsEntityId drawEntities[DebugShapeType_Count]; };

ecs_comp_define(DebugShapeCanvasComp) {
  DynArray shapes; // DebugShape[]
};

static void ecs_destruct_canvas(void* data) {
  DebugShapeCanvasComp* comp = data;
  dynarray_destroy(&comp->shapes);
}

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(ShapeRendererView) { ecs_access_write(DebugShapeRendererComp); }
ecs_view_define(CanvasView) { ecs_access_write(DebugShapeCanvasComp); }
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

static AssetManagerComp* ui_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static DebugShapeRendererComp* debug_shape_renderer(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, ShapeRendererView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, DebugShapeRendererComp) : null;
}

static EcsEntityId
debug_shape_draw_create(EcsWorld* world, AssetManagerComp* assets, const DebugShapeType shape) {
  const EcsEntityId entity        = ecs_world_entity_create(world);
  RendDrawComp*     draw          = rend_draw_create(world, entity, RendDrawFlags_None);
  const EcsEntityId graphicEntity = asset_lookup(world, assets, g_debugGraphics[shape]);
  rend_draw_set_graphic(draw, graphicEntity);
  return entity;
}

void debug_shape_renderer_create(EcsWorld* world, AssetManagerComp* assets) {
  DebugShapeRendererComp* renderer =
      ecs_world_add_t(world, ecs_world_global(world), DebugShapeRendererComp);

  for (DebugShapeType shape = 0; shape != DebugShapeType_Count; ++shape) {
    renderer->drawEntities[shape] = debug_shape_draw_create(world, assets, shape);
  }
}

ecs_system_define(DebugShapeRenderSys) {
  AssetManagerComp* assets = ui_asset_manager(world);
  if (!assets) {
    return; // Asset manager hasn't been initialized yet.
  }

  DebugShapeRendererComp* renderer = debug_shape_renderer(world);
  if (!renderer) {
    debug_shape_renderer_create(world, assets);
    debug_shape_canvas_create(world, ecs_world_global(world)); // Global canvas for convenience.
    return;
  }

  EcsView*     drawView = ecs_world_view_t(world, DrawView);
  EcsIterator* drawItr  = ecs_view_itr(drawView);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, CanvasView)); ecs_view_walk(itr);) {
    DebugShapeCanvasComp* canvas = ecs_view_write_t(itr, DebugShapeCanvasComp);
    dynarray_for_t(&canvas->shapes, DebugShape, shape) {
      ecs_view_jump(drawItr, renderer->drawEntities[shape->type]);
      RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

      typedef struct {
        ALIGNAS(16)
        GeoVector pos;
        GeoQuat   rot;
        GeoVector scale;
        GeoColor  color;
      } DrawData;
      ASSERT(sizeof(DrawData) == 64, "Size needs to match the size defined in glsl");

      switch (shape->type) {
      case DebugShapeType_BoxFill:
      case DebugShapeType_BoxWire: {
        const DrawData data = {
            .pos   = geo_box_center(&shape->data_box.box),
            .rot   = shape->data_box.rot,
            .scale = geo_box_size(&shape->data_box.box),
            .color = shape->data_box.color,
        };
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, shape->data_box.box);
        continue;
      }
      case DebugShapeType_SphereFill:
      case DebugShapeType_SphereWire: {
        const GeoVector pos    = shape->data_sphere.position;
        const f32       radius = shape->data_sphere.radius;
        const GeoBox    bounds = {
            .min = geo_vector(pos.x - radius, pos.y - radius, pos.z - radius),
            .max = geo_vector(pos.x + radius, pos.y + radius, pos.z + radius),
        };
        const DrawData data = {
            .pos   = pos,
            .rot   = geo_quat_ident,
            .scale = geo_vector(radius, radius, radius),
            .color = shape->data_sphere.color,
        };
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_Count:
        break;
      }
      diag_crash();
    }
    dynarray_clear(&canvas->shapes);
  }
}

ecs_module_init(debug_shape_module) {
  ecs_register_comp(DebugShapeRendererComp);
  ecs_register_comp(DebugShapeCanvasComp, .destructor = ecs_destruct_canvas);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(ShapeRendererView);
  ecs_register_view(CanvasView);
  ecs_register_view(DrawView);

  ecs_register_system(
      DebugShapeRenderSys,
      ecs_view_id(GlobalAssetsView),
      ecs_view_id(ShapeRendererView),
      ecs_view_id(CanvasView),
      ecs_view_id(DrawView));

  ecs_order(DebugShapeRenderSys, DebugOrder_ShapeRender);
}

DebugShapeCanvasComp* debug_shape_canvas_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world,
      entity,
      DebugShapeCanvasComp,
      .shapes = dynarray_create_t(g_alloc_heap, DebugShape, 64));
}

void debug_shape_box_fill(
    DebugShapeCanvasComp* comp, const GeoBox box, const GeoQuat rot, const GeoColor color) {
  *dynarray_push_t(&comp->shapes, DebugShape) = (DebugShape){
      .type     = DebugShapeType_BoxFill,
      .data_box = {.box = box, .rot = rot, .color = color},
  };
}

void debug_shape_box_wire(
    DebugShapeCanvasComp* comp, const GeoBox box, const GeoQuat rot, const GeoColor color) {
  *dynarray_push_t(&comp->shapes, DebugShape) = (DebugShape){
      .type     = DebugShapeType_BoxWire,
      .data_box = {.box = box, .rot = rot, .color = color},
  };
}

void debug_shape_sphere_fill(
    DebugShapeCanvasComp* comp, const GeoVector pos, const f32 radius, const GeoColor color) {
  *dynarray_push_t(&comp->shapes, DebugShape) = (DebugShape){
      .type        = DebugShapeType_SphereFill,
      .data_sphere = {.position = pos, .radius = radius, .color = color},
  };
}

void debug_shape_sphere_wire(
    DebugShapeCanvasComp* comp, const GeoVector pos, const f32 radius, const GeoColor color) {
  *dynarray_push_t(&comp->shapes, DebugShape) = (DebugShape){
      .type        = DebugShapeType_SphereWire,
      .data_sphere = {.position = pos, .radius = radius, .color = color},
  };
}
