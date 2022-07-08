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
  DebugShapeType_Box,
  DebugShapeType_BoxFill    = DebugShapeType_Box + DebugShape_Fill,
  DebugShapeType_BoxWire    = DebugShapeType_Box + DebugShape_Wire,
  DebugShapeType_BoxOverlay = DebugShapeType_Box + DebugShape_Overlay,

  DebugShapeType_Quad,
  DebugShapeType_QuadFill    = DebugShapeType_Quad + DebugShape_Fill,
  DebugShapeType_QuadWire    = DebugShapeType_Quad + DebugShape_Wire,
  DebugShapeType_QuadOverlay = DebugShapeType_Quad + DebugShape_Overlay,

  DebugShapeType_Sphere,
  DebugShapeType_SphereFill    = DebugShapeType_Sphere + DebugShape_Fill,
  DebugShapeType_SphereWire    = DebugShapeType_Sphere + DebugShape_Wire,
  DebugShapeType_SphereOverlay = DebugShapeType_Sphere + DebugShape_Overlay,

  DebugShapeType_HemisphereUncapped,
  DebugShapeType_HemisphereUncappedFill    = DebugShapeType_HemisphereUncapped + DebugShape_Fill,
  DebugShapeType_HemisphereUncappedWire    = DebugShapeType_HemisphereUncapped + DebugShape_Wire,
  DebugShapeType_HemisphereUncappedOverlay = DebugShapeType_HemisphereUncapped + DebugShape_Overlay,

  DebugShapeType_Cylinder,
  DebugShapeType_CylinderFill    = DebugShapeType_Cylinder + DebugShape_Fill,
  DebugShapeType_CylinderWire    = DebugShapeType_Cylinder + DebugShape_Wire,
  DebugShapeType_CylinderOverlay = DebugShapeType_Cylinder + DebugShape_Overlay,

  DebugShapeType_CylinderUncapped,
  DebugShapeType_CylinderUncappedFill    = DebugShapeType_CylinderUncapped + DebugShape_Fill,
  DebugShapeType_CylinderUncappedWire    = DebugShapeType_CylinderUncapped + DebugShape_Wire,
  DebugShapeType_CylinderUncappedOverlay = DebugShapeType_CylinderUncapped + DebugShape_Overlay,

  DebugShapeType_Cone,
  DebugShapeType_ConeFill    = DebugShapeType_Cone + DebugShape_Fill,
  DebugShapeType_ConeWire    = DebugShapeType_Cone + DebugShape_Wire,
  DebugShapeType_ConeOverlay = DebugShapeType_Cone + DebugShape_Overlay,

  DebugShapeType_Line,
  DebugShapeType_LineOverlay = DebugShapeType_Line + DebugShape_Overlay,

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
  GeoQuat   rot;
  f32       sizeX, sizeY;
  GeoColor  color;
} DebugShapeQuad;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  f32       radius;
  GeoColor  color;
} DebugShapeSphere;

typedef struct {
  GeoVector bottom, top;
  f32       radius;
  GeoColor  color;
} DebugShapeCylinder;

typedef struct {
  GeoVector bottom, top;
  f32       radius;
  GeoColor  color;
} DebugShapeCone;

typedef struct {
  GeoVector start, end;
  GeoColor  color;
} DebugShapeLine;

typedef struct {
  DebugShapeType type;
  union {
    DebugShapeBox      data_box;
    DebugShapeQuad     data_quad;
    DebugShapeSphere   data_sphere;
    DebugShapeCylinder data_cylinder;
    DebugShapeCone     data_cone;
    DebugShapeLine     data_line;
  };
} DebugShape;

// clang-format off
#define shape_graphic(_SHAPE_, _GRAPHIC_) [DebugShapeType_##_SHAPE_] = string_static(_GRAPHIC_)

static const String g_debugGraphics[DebugShapeType_Count] = {
    shape_graphic(BoxFill,                    "graphics/debug/shape_box_fill.gra"),
    shape_graphic(BoxWire,                    "graphics/debug/shape_box_wire.gra"),
    shape_graphic(BoxOverlay,                 "graphics/debug/shape_box_overlay.gra"),
    shape_graphic(QuadFill,                   "graphics/debug/shape_quad_fill.gra"),
    shape_graphic(QuadWire,                   "graphics/debug/shape_quad_wire.gra"),
    shape_graphic(QuadOverlay,                "graphics/debug/shape_quad_overlay.gra"),
    shape_graphic(SphereFill,                 "graphics/debug/shape_sphere_fill.gra"),
    shape_graphic(SphereWire,                 "graphics/debug/shape_sphere_wire.gra"),
    shape_graphic(SphereOverlay,              "graphics/debug/shape_sphere_overlay.gra"),
    shape_graphic(HemisphereUncappedFill,     "graphics/debug/shape_hemisphere_uncapped_fill.gra"),
    shape_graphic(HemisphereUncappedWire,     "graphics/debug/shape_hemisphere_uncapped_wire.gra"),
    shape_graphic(HemisphereUncappedOverlay,  "graphics/debug/shape_hemisphere_uncapped_overlay.gra"),
    shape_graphic(CylinderFill,               "graphics/debug/shape_cylinder_fill.gra"),
    shape_graphic(CylinderWire,               "graphics/debug/shape_cylinder_wire.gra"),
    shape_graphic(CylinderOverlay,            "graphics/debug/shape_cylinder_overlay.gra"),
    shape_graphic(CylinderUncappedFill,       "graphics/debug/shape_cylinder_uncapped_fill.gra"),
    shape_graphic(CylinderUncappedWire,       "graphics/debug/shape_cylinder_uncapped_wire.gra"),
    shape_graphic(CylinderUncappedOverlay,    "graphics/debug/shape_cylinder_uncapped_overlay.gra"),
    shape_graphic(ConeFill,                   "graphics/debug/shape_cone_fill.gra"),
    shape_graphic(ConeWire,                   "graphics/debug/shape_cone_wire.gra"),
    shape_graphic(ConeOverlay,                "graphics/debug/shape_cone_overlay.gra"),
    shape_graphic(LineOverlay,                "graphics/debug/shape_line_overlay.gra"),
};

#undef shape_graphic
// clang-format on

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
  if (string_is_empty(g_debugGraphics[shape])) {
    return 0;
  }
  const EcsEntityId entity        = ecs_world_entity_create(world);
  RendDrawComp*     draw          = rend_draw_create(world, entity, RendDrawFlags_None);
  const EcsEntityId graphicEntity = asset_lookup(world, assets, g_debugGraphics[shape]);
  rend_draw_set_graphic(draw, graphicEntity);
  return entity;
}

static void debug_shape_renderer_create(EcsWorld* world, AssetManagerComp* assets) {
  DebugShapeRendererComp* renderer =
      ecs_world_add_t(world, ecs_world_global(world), DebugShapeRendererComp);

  for (DebugShapeType shape = 0; shape != DebugShapeType_Count; ++shape) {
    renderer->drawEntities[shape] = debug_shape_draw_create(world, assets, shape);
  }
}

static GeoBox debug_box_bounds(const GeoVector pos, const GeoQuat rot, const GeoVector size) {
  const GeoBox b = (GeoBox){.min = geo_vector_mul(size, -0.5f), .max = geo_vector_mul(size, 0.5f)};
  return geo_box_transform3(&b, pos, rot, 1);
}

INLINE_HINT static void debug_shape_add(DebugShapeComp* comp, const DebugShape shape) {
  *((DebugShape*)dynarray_push(&comp->entries, 1).ptr) = shape;
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

      typedef struct {
        ALIGNAS(16)
        GeoVector positions[2];
        GeoColor  color;
      } DrawLineData;
      ASSERT(sizeof(DrawLineData) == 48, "Size needs to match the size defined in glsl");

      switch (entry->type) {
      case DebugShapeType_BoxFill:
      case DebugShapeType_BoxWire:
      case DebugShapeType_BoxOverlay: {
        const DrawMeshData data = {
            .pos   = entry->data_box.pos,
            .rot   = entry->data_box.rot,
            .scale = entry->data_box.size,
            .color = entry->data_box.color,
        };
        const GeoBox bounds =
            debug_box_bounds(entry->data_box.pos, entry->data_box.rot, entry->data_box.size);
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_QuadFill:
      case DebugShapeType_QuadWire:
      case DebugShapeType_QuadOverlay: {
        const DrawMeshData data = {
            .pos   = entry->data_quad.pos,
            .rot   = entry->data_quad.rot,
            .scale = geo_vector(entry->data_quad.sizeX, entry->data_quad.sizeY, 1),
            .color = entry->data_quad.color,
        };
        const GeoBox bounds = debug_box_bounds(
            entry->data_quad.pos,
            entry->data_quad.rot,
            geo_vector(entry->data_quad.sizeX, entry->data_quad.sizeY));
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_SphereFill:
      case DebugShapeType_SphereWire:
      case DebugShapeType_SphereOverlay:
      case DebugShapeType_HemisphereUncappedFill:
      case DebugShapeType_HemisphereUncappedWire:
      case DebugShapeType_HemisphereUncappedOverlay: {
        const GeoVector pos    = entry->data_sphere.pos;
        const f32       radius = entry->data_sphere.radius;
        if (UNLIKELY(radius < f32_epsilon)) {
          continue;
        }
        const DrawMeshData data = {
            .pos   = pos,
            .rot   = entry->data_sphere.rot,
            .scale = geo_vector(radius, radius, radius),
            .color = entry->data_sphere.color,
        };
        const GeoBox bounds = geo_box_from_sphere(pos, radius);
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_CylinderFill:
      case DebugShapeType_CylinderWire:
      case DebugShapeType_CylinderOverlay:
      case DebugShapeType_CylinderUncappedFill:
      case DebugShapeType_CylinderUncappedWire:
      case DebugShapeType_CylinderUncappedOverlay: {
        const GeoVector bottom = entry->data_cylinder.bottom;
        const GeoVector top    = entry->data_cylinder.top;
        const GeoVector toTop  = geo_vector_sub(top, bottom);
        const f32       dist   = geo_vector_mag(toTop);
        if (UNLIKELY(dist < f32_epsilon)) {
          continue;
        }
        const DrawMeshData data = {
            .pos   = bottom,
            .rot   = geo_quat_look(geo_vector_div(toTop, dist), geo_up),
            .scale = {entry->data_cylinder.radius, entry->data_cylinder.radius, dist},
            .color = entry->data_cylinder.color,
        };
        const GeoBox bounds = geo_box_from_cylinder(bottom, top, entry->data_cylinder.radius);
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_ConeFill:
      case DebugShapeType_ConeWire:
      case DebugShapeType_ConeOverlay: {
        const GeoVector bottom = entry->data_cone.bottom;
        const GeoVector top    = entry->data_cone.top;
        const GeoVector toTop  = geo_vector_sub(top, bottom);
        const f32       dist   = geo_vector_mag(toTop);
        if (UNLIKELY(dist < f32_epsilon)) {
          continue;
        }
        const DrawMeshData data = {
            .pos   = bottom,
            .rot   = geo_quat_look(geo_vector_div(toTop, dist), geo_up),
            .scale = {entry->data_cone.radius, entry->data_cone.radius, dist},
            .color = entry->data_cone.color,
        };
        const GeoBox bounds = geo_box_from_cone(bottom, top, entry->data_cone.radius);
        rend_draw_add_instance(draw, mem_var(data), SceneTags_Debug, bounds);
        continue;
      }
      case DebugShapeType_Line:
      case DebugShapeType_LineOverlay: {
        const DrawLineData data = {
            .positions[0] = entry->data_line.start,
            .positions[1] = entry->data_line.end,
            .color        = entry->data_line.color,
        };
        const GeoBox bounds = geo_box_from_line(entry->data_line.start, entry->data_line.end);
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

void debug_box(
    DebugShapeComp*      comp,
    const GeoVector      pos,
    const GeoQuat        rot,
    const GeoVector      size,
    const GeoColor       color,
    const DebugShapeMode mode) {
  debug_shape_add(
      comp,
      (DebugShape){
          .type     = DebugShapeType_Box + mode,
          .data_box = {.pos = pos, .rot = rot, .size = size, .color = color},
      });
}

void debug_quad(
    DebugShapeComp*      comp,
    const GeoVector      pos,
    const GeoQuat        rot,
    const f32            sizeX,
    const f32            sizeY,
    const GeoColor       color,
    const DebugShapeMode mode) {
  debug_shape_add(
      comp,
      (DebugShape){
          .type      = DebugShapeType_Quad + mode,
          .data_quad = {.pos = pos, .rot = rot, .sizeX = sizeX, .sizeY = sizeY, .color = color},
      });
}

void debug_sphere(
    DebugShapeComp*      comp,
    const GeoVector      pos,
    const f32            radius,
    const GeoColor       color,
    const DebugShapeMode mode) {
  debug_shape_add(
      comp,
      (DebugShape){
          .type        = DebugShapeType_Sphere + mode,
          .data_sphere = {.pos = pos, .radius = radius, .color = color},
      });
}

void debug_cylinder(
    DebugShapeComp*      comp,
    const GeoVector      bottom,
    const GeoVector      top,
    const f32            radius,
    const GeoColor       color,
    const DebugShapeMode mode) {
  debug_shape_add(
      comp,
      (DebugShape){
          .type          = DebugShapeType_Cylinder + mode,
          .data_cylinder = {.bottom = bottom, .top = top, .radius = radius, .color = color},
      });
}

void debug_capsule(
    DebugShapeComp*      comp,
    const GeoVector      bottom,
    const GeoVector      top,
    const f32            radius,
    const GeoColor       color,
    const DebugShapeMode mode) {
  GeoVector toTop = geo_vector_sub(top, bottom);
  if (geo_vector_mag_sqr(toTop) < 1e-6f) {
    toTop = geo_up;
  }
  const GeoVector toBottom = geo_vector_mul(toTop, -1.0f);

  debug_shape_add(
      comp,
      (DebugShape){
          .type          = DebugShapeType_CylinderUncapped + mode,
          .data_cylinder = {.bottom = bottom, .top = top, .radius = radius, .color = color},
      });

  debug_shape_add(
      comp,
      (DebugShape){
          .type = DebugShapeType_HemisphereUncapped + mode,
          .data_sphere =
              {.pos    = top,
               .rot    = geo_quat_look(toTop, geo_forward),
               .radius = radius,
               .color  = color},
      });

  debug_shape_add(
      comp,
      (DebugShape){
          .type = DebugShapeType_HemisphereUncapped + mode,
          .data_sphere =
              {.pos    = bottom,
               .rot    = geo_quat_look(toBottom, geo_forward),
               .radius = radius,
               .color  = color},
      });
}

void debug_cone(
    DebugShapeComp*      comp,
    const GeoVector      bottom,
    const GeoVector      top,
    const f32            radius,
    const GeoColor       color,
    const DebugShapeMode mode) {
  debug_shape_add(
      comp,
      (DebugShape){
          .type      = DebugShapeType_Cone + mode,
          .data_cone = {.bottom = bottom, .top = top, .radius = radius, .color = color},
      });
}

void debug_line(
    DebugShapeComp* comp, const GeoVector start, const GeoVector end, const GeoColor color) {
  debug_shape_add(
      comp,
      (DebugShape){
          .type      = DebugShapeType_Line + DebugShape_Overlay,
          .data_line = {.start = start, .end = end, .color = color},
      });
}

void debug_arrow(
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
  debug_cone(comp, tipStart, end, radius, color, DebugShape_Overlay);

  const f32 baseLength = dist - tipLength;
  if (baseLength > f32_epsilon) {
    debug_cylinder(comp, begin, tipStart, radius * g_baseRadiusMult, color, DebugShape_Overlay);
  }
}

void debug_orientation(
    DebugShapeComp* comp, const GeoVector pos, const GeoQuat rot, const f32 size) {
  static const f32 g_startOffsetMult = 0.05f;
  static const f32 g_radiusMult      = 0.1f;

  const GeoVector right   = geo_quat_rotate(rot, geo_right);
  const GeoVector up      = geo_quat_rotate(rot, geo_up);
  const GeoVector forward = geo_quat_rotate(rot, geo_forward);
  const f32       radius  = size * g_radiusMult;

  const GeoVector startRight = geo_vector_add(pos, geo_vector_mul(right, g_startOffsetMult));
  const GeoVector endRight   = geo_vector_add(pos, geo_vector_mul(right, size));
  debug_arrow(comp, startRight, endRight, radius, geo_color_red);

  const GeoVector startUp = geo_vector_add(pos, geo_vector_mul(up, g_startOffsetMult));
  const GeoVector endUp   = geo_vector_add(pos, geo_vector_mul(up, size));
  debug_arrow(comp, startUp, endUp, radius, geo_color_green);

  const GeoVector startForward = geo_vector_add(pos, geo_vector_mul(forward, g_startOffsetMult));
  const GeoVector endForward   = geo_vector_add(pos, geo_vector_mul(forward, size));
  debug_arrow(comp, startForward, endForward, radius, geo_color_blue);
}

void debug_plane(
    DebugShapeComp* comp, const GeoVector pos, const GeoQuat rot, const GeoColor color) {
  const f32 quadSize = 1.0f;
  debug_quad(comp, pos, rot, quadSize, quadSize, color, DebugShape_Overlay);

  const f32       arrowLength = 1.0f;
  const f32       arrowRadius = 0.1f;
  const GeoVector arrowNorm   = geo_quat_rotate(rot, geo_forward);
  const GeoVector arrowEnd    = geo_vector_add(pos, geo_vector_mul(arrowNorm, arrowLength));
  debug_arrow(comp, pos, arrowEnd, arrowRadius, color);
}

void debug_frustum(DebugShapeComp* comp, const GeoMatrix* viewProj, const GeoColor color) {
  const GeoMatrix invViewProj = geo_matrix_inverse(viewProj);
  const f32       nearNdc     = 1.0f;
  const f32       farNdc      = 0.0001f; // NOTE: Using reverse-z with infinite far-plane.

  GeoVector points[] = {
      geo_vector(-1, -1, nearNdc, 1),
      geo_vector(1, -1, nearNdc, 1),
      geo_vector(1, 1, nearNdc, 1),
      geo_vector(-1, 1, nearNdc, 1),
      geo_vector(-1, -1, farNdc, 1),
      geo_vector(1, -1, farNdc, 1),
      geo_vector(1, 1, farNdc, 1),
      geo_vector(-1, 1, farNdc, 1),
  };
  array_for_t(points, GeoVector, v) {
    *v = geo_vector_perspective_div(geo_matrix_transform(&invViewProj, *v));
  }

  // Near plane.
  debug_line(comp, points[0], points[1], color);
  debug_line(comp, points[1], points[2], color);
  debug_line(comp, points[2], points[3], color);
  debug_line(comp, points[3], points[0], color);

  // Far plane.
  debug_line(comp, points[4], points[5], color);
  debug_line(comp, points[5], points[6], color);
  debug_line(comp, points[6], points[7], color);
  debug_line(comp, points[7], points[4], color);

  // Connecting lines.
  debug_line(comp, points[0], points[4], color);
  debug_line(comp, points[1], points[5], color);
  debug_line(comp, points[2], points[6], color);
  debug_line(comp, points[3], points[7], color);
}
