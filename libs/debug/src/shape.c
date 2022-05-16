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
  DebugShapeType_BoxOverlay,
  DebugShapeType_SphereFill,
  DebugShapeType_SphereWire,
  DebugShapeType_SphereOverlay,
  DebugShapeType_CylinderFill,
  DebugShapeType_CylinderWire,
  DebugShapeType_CylinderOverlay,
  DebugShapeType_ConeFill,
  DebugShapeType_ConeWire,
  DebugShapeType_ConeOverlay,

  DebugShapeType_Count,
} DebugShapeType;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  GeoVector size;
  GeoColor  color;
} DebugShapeBox;

typedef struct {
  GeoVector pos;
  f32       radius;
  GeoColor  color;
} DebugShapeSphere;

typedef struct {
  GeoVector bottom, top;
  f32       radius;
  GeoColor  color;
} DebugShapeCylOrCone;

typedef struct {
  DebugShapeType type;
  union {
    DebugShapeBox       data_box;
    DebugShapeSphere    data_sphere;
    DebugShapeCylOrCone data_cylOrCone;
  };
} DebugShape;

static const String g_debugGraphics[DebugShapeType_Count] = {
    [DebugShapeType_BoxFill]         = string_static("graphics/debug/shape_box_fill.gra"),
    [DebugShapeType_BoxWire]         = string_static("graphics/debug/shape_box_wire.gra"),
    [DebugShapeType_BoxOverlay]      = string_static("graphics/debug/shape_box_overlay.gra"),
    [DebugShapeType_SphereFill]      = string_static("graphics/debug/shape_sphere_fill.gra"),
    [DebugShapeType_SphereWire]      = string_static("graphics/debug/shape_sphere_wire.gra"),
    [DebugShapeType_SphereOverlay]   = string_static("graphics/debug/shape_sphere_overlay.gra"),
    [DebugShapeType_CylinderFill]    = string_static("graphics/debug/shape_cylinder_fill.gra"),
    [DebugShapeType_CylinderWire]    = string_static("graphics/debug/shape_cylinder_wire.gra"),
    [DebugShapeType_CylinderOverlay] = string_static("graphics/debug/shape_cylinder_overlay.gra"),
    [DebugShapeType_ConeFill]        = string_static("graphics/debug/shape_cone_fill.gra"),
    [DebugShapeType_ConeWire]        = string_static("graphics/debug/shape_cone_wire.gra"),
    [DebugShapeType_ConeOverlay]     = string_static("graphics/debug/shape_cone_overlay.gra"),
};

ecs_comp_define(DebugShapeRendererComp) { EcsEntityId drawEntities[DebugShapeType_Count]; };

ecs_comp_define(DebugShapeComp) {
  DynArray entries; // DebugShape[]
};

static void ecs_destruct_shape(void* data) {
  DebugShapeComp* comp = data;
  dynarray_destroy(&comp->entries);
}

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(ShapeRendererView) { ecs_access_write(DebugShapeRendererComp); }
ecs_view_define(ShapeView) { ecs_access_write(DebugShapeComp); }
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
    debug_shape_create(world, ecs_world_global(world)); // Global shape component for convenience.
    return;
  }

  EcsView*     drawView = ecs_world_view_t(world, DrawView);
  EcsIterator* drawItr  = ecs_view_itr(drawView);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ShapeView)); ecs_view_walk(itr);) {
    DebugShapeComp* shape = ecs_view_write_t(itr, DebugShapeComp);
    dynarray_for_t(&shape->entries, DebugShape, entry) {
      ecs_view_jump(drawItr, renderer->drawEntities[entry->type]);
      RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

      typedef struct {
        ALIGNAS(16)
        GeoVector pos;
        GeoQuat   rot;
        GeoVector scale;
        GeoColor  color;
      } DrawMeshData;
      ASSERT(sizeof(DrawMeshData) == 64, "Size needs to match the size defined in glsl");

      switch (entry->type) {
      case DebugShapeType_BoxFill:
      case DebugShapeType_BoxWire:
      case DebugShapeType_BoxOverlay: {
        const GeoBox       bounds = geo_box_inverted3(); // TODO: Compute bounds.
        const DrawMeshData data   = {
            .pos   = entry->data_box.pos,
            .rot   = entry->data_box.rot,
            .scale = entry->data_box.size,
            .color = entry->data_box.color,
        };
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_SphereFill:
      case DebugShapeType_SphereWire:
      case DebugShapeType_SphereOverlay: {
        const GeoVector pos    = entry->data_sphere.pos;
        const f32       radius = entry->data_sphere.radius;
        const GeoBox    bounds = {
            .min = geo_vector(pos.x - radius, pos.y - radius, pos.z - radius),
            .max = geo_vector(pos.x + radius, pos.y + radius, pos.z + radius),
        };
        const DrawMeshData data = {
            .pos   = pos,
            .rot   = geo_quat_ident,
            .scale = geo_vector(radius, radius, radius),
            .color = entry->data_sphere.color,
        };
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_CylinderFill:
      case DebugShapeType_CylinderWire:
      case DebugShapeType_CylinderOverlay:
      case DebugShapeType_ConeFill:
      case DebugShapeType_ConeWire:
      case DebugShapeType_ConeOverlay: {
        const GeoBox    bounds = geo_box_inverted3(); // TODO: Compute bounds.
        const GeoVector bottom = entry->data_cylOrCone.bottom;
        const GeoVector toTop  = geo_vector_sub(entry->data_cylOrCone.top, bottom);
        const f32       dist   = geo_vector_mag(toTop);
        if (UNLIKELY(dist < f32_epsilon)) {
          continue;
        }
        const DrawMeshData data = {
            .pos   = bottom,
            .rot   = geo_quat_look(geo_vector_div(toTop, dist), geo_up),
            .scale = {entry->data_cylOrCone.radius, entry->data_cylOrCone.radius, dist},
            .color = entry->data_cylOrCone.color,
        };
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_Count:
        break;
      }
      diag_crash();
    }
    dynarray_clear(&shape->entries);
  }
}

ecs_module_init(debug_shape_module) {
  ecs_register_comp(DebugShapeRendererComp);
  ecs_register_comp(DebugShapeComp, .destructor = ecs_destruct_shape);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(ShapeRendererView);
  ecs_register_view(ShapeView);
  ecs_register_view(DrawView);

  ecs_register_system(
      DebugShapeRenderSys,
      ecs_view_id(GlobalAssetsView),
      ecs_view_id(ShapeRendererView),
      ecs_view_id(ShapeView),
      ecs_view_id(DrawView));

  ecs_order(DebugShapeRenderSys, DebugOrder_ShapeRender);
}

DebugShapeComp* debug_shape_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, DebugShapeComp, .entries = dynarray_create_t(g_alloc_heap, DebugShape, 64));
}

void debug_box_fill(
    DebugShapeComp* comp,
    const GeoVector pos,
    const GeoQuat   rot,
    const GeoVector size,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type     = DebugShapeType_BoxFill,
      .data_box = {.pos = pos, .rot = rot, .size = size, .color = color},
  };
}

void debug_box_wire(
    DebugShapeComp* comp,
    const GeoVector pos,
    const GeoQuat   rot,
    const GeoVector size,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type     = DebugShapeType_BoxWire,
      .data_box = {.pos = pos, .rot = rot, .size = size, .color = color},
  };
}

void debug_box_overlay(
    DebugShapeComp* comp,
    const GeoVector pos,
    const GeoQuat   rot,
    const GeoVector size,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type     = DebugShapeType_BoxOverlay,
      .data_box = {.pos = pos, .rot = rot, .size = size, .color = color},
  };
}

void debug_sphere_fill(
    DebugShapeComp* comp, const GeoVector pos, const f32 radius, const GeoColor color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type        = DebugShapeType_SphereFill,
      .data_sphere = {.pos = pos, .radius = radius, .color = color},
  };
}

void debug_sphere_wire(
    DebugShapeComp* comp, const GeoVector pos, const f32 radius, const GeoColor color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type        = DebugShapeType_SphereWire,
      .data_sphere = {.pos = pos, .radius = radius, .color = color},
  };
}

void debug_sphere_overlay(
    DebugShapeComp* comp, const GeoVector pos, const f32 radius, const GeoColor color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type        = DebugShapeType_SphereOverlay,
      .data_sphere = {.pos = pos, .radius = radius, .color = color},
  };
}

void debug_cylinder_fill(
    DebugShapeComp* comp,
    const GeoVector bottom,
    const GeoVector top,
    const f32       radius,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type           = DebugShapeType_CylinderFill,
      .data_cylOrCone = {.bottom = bottom, .top = top, .radius = radius, .color = color},
  };
}

void debug_cylinder_wire(
    DebugShapeComp* comp,
    const GeoVector bottom,
    const GeoVector top,
    const f32       radius,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type           = DebugShapeType_CylinderWire,
      .data_cylOrCone = {.bottom = bottom, .top = top, .radius = radius, .color = color},
  };
}

void debug_cylinder_overlay(
    DebugShapeComp* comp,
    const GeoVector bottom,
    const GeoVector top,
    const f32       radius,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type           = DebugShapeType_CylinderOverlay,
      .data_cylOrCone = {.bottom = bottom, .top = top, .radius = radius, .color = color},
  };
}

void debug_cone_fill(
    DebugShapeComp* comp,
    const GeoVector bottom,
    const GeoVector top,
    const f32       radius,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type           = DebugShapeType_ConeFill,
      .data_cylOrCone = {.bottom = bottom, .top = top, .radius = radius, .color = color},
  };
}

void debug_cone_wire(
    DebugShapeComp* comp,
    const GeoVector bottom,
    const GeoVector top,
    const f32       radius,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type           = DebugShapeType_ConeWire,
      .data_cylOrCone = {.bottom = bottom, .top = top, .radius = radius, .color = color},
  };
}

void debug_cone_overlay(
    DebugShapeComp* comp,
    const GeoVector bottom,
    const GeoVector top,
    const f32       radius,
    const GeoColor  color) {
  *dynarray_push_t(&comp->entries, DebugShape) = (DebugShape){
      .type           = DebugShapeType_ConeOverlay,
      .data_cylOrCone = {.bottom = bottom, .top = top, .radius = radius, .color = color},
  };
}

void debug_arrow_overlay(
    DebugShapeComp* comp,
    const GeoVector begin,
    const GeoVector end,
    const f32       radius,
    const GeoColor  color) {
  static const f32 g_tipLengthMult  = 2.0f;
  static const f32 g_baseRadiusMult = 0.25f;

  const GeoVector toEnd = geo_vector_sub(end, begin);
  const f32       dist  = geo_vector_mag(toEnd);
  const GeoVector dir   = dist > f32_epsilon ? geo_vector_div(toEnd, dist) : geo_forward;

  const f32       tipLength = radius * g_tipLengthMult;
  const GeoVector tipStart  = geo_vector_sub(end, geo_vector_mul(dir, tipLength));
  debug_cone_overlay(comp, tipStart, end, radius, color);

  const f32 baseLength = dist - tipLength;
  if (baseLength > f32_epsilon) {
    debug_cylinder_overlay(comp, begin, tipStart, radius * g_baseRadiusMult, color);
  }
}

void debug_orientation_overlay(
    DebugShapeComp* comp, const GeoVector pos, const GeoQuat rot, const f32 size) {
  static const f32 g_startOffsetMult = 0.05f;
  static const f32 g_radiusMult      = 0.1f;

  const GeoVector right   = geo_quat_rotate(rot, geo_right);
  const GeoVector up      = geo_quat_rotate(rot, geo_up);
  const GeoVector forward = geo_quat_rotate(rot, geo_forward);
  const f32       radius  = size * g_radiusMult;

  const GeoVector startRight = geo_vector_add(pos, geo_vector_mul(right, g_startOffsetMult));
  debug_arrow_overlay(comp, startRight, geo_vector_add(pos, right), radius, geo_color_red);

  const GeoVector startUp = geo_vector_add(pos, geo_vector_mul(up, g_startOffsetMult));
  debug_arrow_overlay(comp, startUp, geo_vector_add(pos, up), radius, geo_color_green);

  const GeoVector startForward = geo_vector_add(pos, geo_vector_mul(forward, g_startOffsetMult));
  debug_arrow_overlay(comp, startForward, geo_vector_add(pos, forward), radius, geo_color_blue);
}
