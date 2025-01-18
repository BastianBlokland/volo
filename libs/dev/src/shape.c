#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "dev_register.h"
#include "dev_shape.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "geo_box_rotated.h"
#include "geo_capsule.h"
#include "geo_color.h"
#include "geo_matrix.h"
#include "geo_quat.h"
#include "geo_sphere.h"
#include "rend_object.h"
#include "scene_tag.h"

typedef enum {
  DevShapeType_Box,
  DevShapeType_BoxFill    = DevShapeType_Box + DevShape_Fill,
  DevShapeType_BoxWire    = DevShapeType_Box + DevShape_Wire,
  DevShapeType_BoxOverlay = DevShapeType_Box + DevShape_Overlay,

  DevShapeType_Quad,
  DevShapeType_QuadFill    = DevShapeType_Quad + DevShape_Fill,
  DevShapeType_QuadWire    = DevShapeType_Quad + DevShape_Wire,
  DevShapeType_QuadOverlay = DevShapeType_Quad + DevShape_Overlay,

  DevShapeType_Sphere,
  DevShapeType_SphereFill    = DevShapeType_Sphere + DevShape_Fill,
  DevShapeType_SphereWire    = DevShapeType_Sphere + DevShape_Wire,
  DevShapeType_SphereOverlay = DevShapeType_Sphere + DevShape_Overlay,

  DevShapeType_HemisphereUncapped,
  DevShapeType_HemisphereUncappedFill    = DevShapeType_HemisphereUncapped + DevShape_Fill,
  DevShapeType_HemisphereUncappedWire    = DevShapeType_HemisphereUncapped + DevShape_Wire,
  DevShapeType_HemisphereUncappedOverlay = DevShapeType_HemisphereUncapped + DevShape_Overlay,

  DevShapeType_Cylinder,
  DevShapeType_CylinderFill    = DevShapeType_Cylinder + DevShape_Fill,
  DevShapeType_CylinderWire    = DevShapeType_Cylinder + DevShape_Wire,
  DevShapeType_CylinderOverlay = DevShapeType_Cylinder + DevShape_Overlay,

  DevShapeType_CylinderUncapped,
  DevShapeType_CylinderUncappedFill    = DevShapeType_CylinderUncapped + DevShape_Fill,
  DevShapeType_CylinderUncappedWire    = DevShapeType_CylinderUncapped + DevShape_Wire,
  DevShapeType_CylinderUncappedOverlay = DevShapeType_CylinderUncapped + DevShape_Overlay,

  DevShapeType_Cone,
  DevShapeType_ConeFill    = DevShapeType_Cone + DevShape_Fill,
  DevShapeType_ConeWire    = DevShapeType_Cone + DevShape_Wire,
  DevShapeType_ConeOverlay = DevShapeType_Cone + DevShape_Overlay,

  DevShapeType_Line,
  DevShapeType_LineOverlay = DevShapeType_Line + DevShape_Overlay,

  DevShapeType_Count,
} DevShapeType;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  GeoVector size;
  GeoColor  color;
} DevShapeBox;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  f32       sizeX, sizeY;
  GeoColor  color;
} DevShapeQuad;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  f32       radius;
  GeoColor  color;
} DevShapeSphere;

typedef struct {
  GeoVector bottom, top;
  f32       radius;
  GeoColor  color;
} DevShapeCylinder;

typedef struct {
  GeoVector bottom, top;
  f32       radius;
  GeoColor  color;
} DevShapeCone;

typedef struct {
  GeoVector start, end;
  GeoColor  color;
} DevShapeLine;

typedef struct {
  DevShapeType type;
  union {
    DevShapeBox      data_box;
    DevShapeQuad     data_quad;
    DevShapeSphere   data_sphere;
    DevShapeCylinder data_cylinder;
    DevShapeCone     data_cone;
    DevShapeLine     data_line;
  };
} DevShape;

// clang-format off
#define shape_graphic(_SHAPE_, _GRAPHIC_) [DevShapeType_##_SHAPE_] = string_static(_GRAPHIC_)

static const String g_devGraphics[DevShapeType_Count] = {
    shape_graphic(BoxFill,                    "graphics/debug/shape_box_fill.graphic"),
    shape_graphic(BoxWire,                    "graphics/debug/shape_box_wire.graphic"),
    shape_graphic(BoxOverlay,                 "graphics/debug/shape_box_overlay.graphic"),
    shape_graphic(QuadFill,                   "graphics/debug/shape_quad_fill.graphic"),
    shape_graphic(QuadWire,                   "graphics/debug/shape_quad_wire.graphic"),
    shape_graphic(QuadOverlay,                "graphics/debug/shape_quad_overlay.graphic"),
    shape_graphic(SphereFill,                 "graphics/debug/shape_sphere_fill.graphic"),
    shape_graphic(SphereWire,                 "graphics/debug/shape_sphere_wire.graphic"),
    shape_graphic(SphereOverlay,              "graphics/debug/shape_sphere_overlay.graphic"),
    shape_graphic(HemisphereUncappedFill,     "graphics/debug/shape_hemisphere_uncapped_fill.graphic"),
    shape_graphic(HemisphereUncappedWire,     "graphics/debug/shape_hemisphere_uncapped_wire.graphic"),
    shape_graphic(HemisphereUncappedOverlay,  "graphics/debug/shape_hemisphere_uncapped_overlay.graphic"),
    shape_graphic(CylinderFill,               "graphics/debug/shape_cylinder_fill.graphic"),
    shape_graphic(CylinderWire,               "graphics/debug/shape_cylinder_wire.graphic"),
    shape_graphic(CylinderOverlay,            "graphics/debug/shape_cylinder_overlay.graphic"),
    shape_graphic(CylinderUncappedFill,       "graphics/debug/shape_cylinder_uncapped_fill.graphic"),
    shape_graphic(CylinderUncappedWire,       "graphics/debug/shape_cylinder_uncapped_wire.graphic"),
    shape_graphic(CylinderUncappedOverlay,    "graphics/debug/shape_cylinder_uncapped_overlay.graphic"),
    shape_graphic(ConeFill,                   "graphics/debug/shape_cone_fill.graphic"),
    shape_graphic(ConeWire,                   "graphics/debug/shape_cone_wire.graphic"),
    shape_graphic(ConeOverlay,                "graphics/debug/shape_cone_overlay.graphic"),
    shape_graphic(LineOverlay,                "graphics/debug/shape_line_overlay.graphic"),
};

#undef shape_graphic
// clang-format on

ecs_comp_define(DevShapeRendererComp) { EcsEntityId rendObjEntities[DevShapeType_Count]; };

ecs_comp_define(DevShapeComp) {
  DynArray entries; // DevShape[]
};

static void ecs_destruct_shape(void* data) {
  DevShapeComp* comp = data;
  dynarray_destroy(&comp->entries);
}

ecs_view_define(AssetManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(ShapeRendererView) { ecs_access_write(DevShapeRendererComp); }
ecs_view_define(ShapeView) { ecs_access_write(DevShapeComp); }

ecs_view_define(RendObjView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the render objects we create.
  ecs_access_write(RendObjectComp);
}

static AssetManagerComp* dev_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, AssetManagerView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static DevShapeRendererComp* dev_shape_renderer(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, ShapeRendererView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, DevShapeRendererComp) : null;
}

static EcsEntityId
dev_shape_rend_obj_create(EcsWorld* world, AssetManagerComp* assets, const DevShapeType shape) {
  if (string_is_empty(g_devGraphics[shape])) {
    return 0;
  }
  const EcsEntityId entity = ecs_world_entity_create(world);
  /**
   * TODO: At the moment all shapes are drawn back-to-front, but this is only needed for overlay
   * types. For the depth testing types (fill and wire) this causes unnecessary overdraw and should
   * either be sorted front-to-back or not at all.
   * NOTE: Only instances of the same shape are sorted, order between different shapes is undefined.
   */
  const RendObjectFlags objFlags      = RendObjectFlags_SortBackToFront;
  RendObjectComp*       obj           = rend_object_create(world, entity, objFlags);
  const EcsEntityId     graphicEntity = asset_lookup(world, assets, g_devGraphics[shape]);
  rend_object_set_resource(obj, RendObjectRes_Graphic, graphicEntity);
  return entity;
}

static void dev_shape_renderer_create(EcsWorld* world, AssetManagerComp* assets) {
  DevShapeRendererComp* renderer =
      ecs_world_add_t(world, ecs_world_global(world), DevShapeRendererComp);

  for (DevShapeType shape = 0; shape != DevShapeType_Count; ++shape) {
    renderer->rendObjEntities[shape] = dev_shape_rend_obj_create(world, assets, shape);
  }
}

INLINE_HINT static void dev_shape_add(DevShapeComp* comp, const DevShape shape) {
  *((DevShape*)dynarray_push(&comp->entries, 1).ptr) = shape;
}

ecs_system_define(DevShapeInitSys) {
  DevShapeRendererComp* renderer = dev_shape_renderer(world);
  if (LIKELY(renderer)) {
    return; // Already initialized.
  }

  AssetManagerComp* assets = dev_asset_manager(world);
  if (assets) {
    dev_shape_renderer_create(world, assets);
    dev_shape_create(world, ecs_world_global(world)); // Global shape component for convenience.
  }
}

ecs_system_define(DevShapeRenderSys) {
  DevShapeRendererComp* renderer = dev_shape_renderer(world);
  if (UNLIKELY(!renderer)) {
    return; // Renderer not yet initialized.
  }

  EcsView*     rendObjView = ecs_world_view_t(world, RendObjView);
  EcsIterator* rendObjItr  = ecs_view_itr(rendObjView);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ShapeView)); ecs_view_walk(itr);) {
    DevShapeComp* shape = ecs_view_write_t(itr, DevShapeComp);
    dynarray_for_t(&shape->entries, DevShape, entry) {
      ecs_view_jump(rendObjItr, renderer->rendObjEntities[entry->type]);
      RendObjectComp* rendObj = ecs_view_write_t(rendObjItr, RendObjectComp);

      typedef struct {
        ALIGNAS(16)
        GeoVector pos;
        GeoQuat   rot;
        GeoVector scale;
        GeoColor  color;
      } DrawMeshData;
      ASSERT(sizeof(DrawMeshData) == 64, "Size needs to match the size defined in glsl");
      ASSERT(alignof(DrawMeshData) == 16, "Alignment needs to match the glsl alignment");

      typedef struct {
        ALIGNAS(16)
        GeoVector positions[2];
        GeoColor  color;
      } DrawLineData;
      ASSERT(sizeof(DrawLineData) == 48, "Size needs to match the size defined in glsl");
      ASSERT(alignof(DrawLineData) == 16, "Alignment needs to match the glsl alignment");

      switch (entry->type) {
      case DevShapeType_BoxFill:
      case DevShapeType_BoxWire:
      case DevShapeType_BoxOverlay: {
        const GeoBox boundsLocal = (GeoBox){
            .min = geo_vector_mul(entry->data_box.size, -0.5f),
            .max = geo_vector_mul(entry->data_box.size, 0.5f),
        };
        const SceneTags tags = SceneTags_Debug;
        const GeoBox    bounds =
            geo_box_transform3(&boundsLocal, entry->data_box.pos, entry->data_box.rot, 1);
        *rend_object_add_instance_t(rendObj, DrawMeshData, tags, bounds) = (DrawMeshData){
            .pos   = entry->data_box.pos,
            .rot   = entry->data_box.rot,
            .scale = entry->data_box.size,
            .color = entry->data_box.color,
        };
        continue;
      }
      case DevShapeType_QuadFill:
      case DevShapeType_QuadWire:
      case DevShapeType_QuadOverlay: {
        const SceneTags tags   = SceneTags_Debug;
        const GeoBox    bounds = geo_box_from_quad(
            entry->data_quad.pos,
            entry->data_quad.sizeX,
            entry->data_quad.sizeY,
            entry->data_quad.rot);
        *rend_object_add_instance_t(rendObj, DrawMeshData, tags, bounds) = (DrawMeshData){
            .pos   = entry->data_quad.pos,
            .rot   = entry->data_quad.rot,
            .scale = geo_vector(entry->data_quad.sizeX, entry->data_quad.sizeY, 1),
            .color = entry->data_quad.color,
        };
        continue;
      }
      case DevShapeType_SphereFill:
      case DevShapeType_SphereWire:
      case DevShapeType_SphereOverlay:
      case DevShapeType_HemisphereUncappedFill:
      case DevShapeType_HemisphereUncappedWire:
      case DevShapeType_HemisphereUncappedOverlay: {
        const GeoVector pos    = entry->data_sphere.pos;
        const f32       radius = entry->data_sphere.radius;
        if (UNLIKELY(radius < f32_epsilon)) {
          continue;
        }
        const SceneTags tags   = SceneTags_Debug;
        const GeoBox    bounds = geo_box_from_sphere(pos, radius);
        *rend_object_add_instance_t(rendObj, DrawMeshData, tags, bounds) = (DrawMeshData){
            .pos   = pos,
            .rot   = entry->data_sphere.rot,
            .scale = geo_vector(radius, radius, radius),
            .color = entry->data_sphere.color,
        };
        continue;
      }
      case DevShapeType_CylinderFill:
      case DevShapeType_CylinderWire:
      case DevShapeType_CylinderOverlay:
      case DevShapeType_CylinderUncappedFill:
      case DevShapeType_CylinderUncappedWire:
      case DevShapeType_CylinderUncappedOverlay: {
        const GeoVector bottom = entry->data_cylinder.bottom;
        const GeoVector top    = entry->data_cylinder.top;
        const GeoVector toTop  = geo_vector_sub(top, bottom);
        const f32       dist   = geo_vector_mag(toTop);
        if (UNLIKELY(dist < f32_epsilon)) {
          continue;
        }
        const SceneTags tags   = SceneTags_Debug;
        const GeoBox    bounds = geo_box_from_cylinder(bottom, top, entry->data_cylinder.radius);
        *rend_object_add_instance_t(rendObj, DrawMeshData, tags, bounds) = (DrawMeshData){
            .pos   = bottom,
            .rot   = geo_quat_look(geo_vector_div(toTop, dist), geo_up),
            .scale = {entry->data_cylinder.radius, entry->data_cylinder.radius, dist},
            .color = entry->data_cylinder.color,
        };
        continue;
      }
      case DevShapeType_ConeFill:
      case DevShapeType_ConeWire:
      case DevShapeType_ConeOverlay: {
        const GeoVector bottom = entry->data_cone.bottom;
        const GeoVector top    = entry->data_cone.top;
        const GeoVector toTop  = geo_vector_sub(top, bottom);
        const f32       dist   = geo_vector_mag(toTop);
        if (UNLIKELY(dist < f32_epsilon)) {
          continue;
        }
        const SceneTags tags   = SceneTags_Debug;
        const GeoBox    bounds = geo_box_from_cone(bottom, top, entry->data_cone.radius);
        *rend_object_add_instance_t(rendObj, DrawMeshData, tags, bounds) = (DrawMeshData){
            .pos   = bottom,
            .rot   = geo_quat_look(geo_vector_div(toTop, dist), geo_up),
            .scale = {entry->data_cone.radius, entry->data_cone.radius, dist},
            .color = entry->data_cone.color,
        };
        continue;
      }
      case DevShapeType_Line:
      case DevShapeType_LineOverlay: {
        const SceneTags tags   = SceneTags_Debug;
        const GeoBox    bounds = geo_box_from_line(entry->data_line.start, entry->data_line.end);
        *rend_object_add_instance_t(rendObj, DrawLineData, tags, bounds) = (DrawLineData){
            .positions[0] = entry->data_line.start,
            .positions[1] = entry->data_line.end,
            .color        = entry->data_line.color,
        };
        continue;
      }
      case DevShapeType_Count:
        break;
      }
      diag_crash();
    }
    dynarray_clear(&shape->entries);
  }
}

ecs_module_init(dev_shape_module) {
  ecs_register_comp(DevShapeRendererComp);
  ecs_register_comp(DevShapeComp, .destructor = ecs_destruct_shape);

  ecs_register_view(AssetManagerView);
  ecs_register_view(ShapeRendererView);
  ecs_register_view(ShapeView);
  ecs_register_view(RendObjView);

  ecs_register_system(
      DevShapeInitSys, ecs_view_id(AssetManagerView), ecs_view_id(ShapeRendererView));

  ecs_register_system(
      DevShapeRenderSys,
      ecs_view_id(ShapeRendererView),
      ecs_view_id(ShapeView),
      ecs_view_id(RendObjView));

  ecs_order(DevShapeRenderSys, DevOrder_ShapeRender);
}

DevShapeComp* dev_shape_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, DevShapeComp, .entries = dynarray_create_t(g_allocHeap, DevShape, 64));
}

void dev_box(
    DevShapeComp*      comp,
    const GeoVector    pos,
    const GeoQuat      rot,
    const GeoVector    size,
    const GeoColor     color,
    const DevShapeMode mode) {
  dev_shape_add(
      comp,
      (DevShape){
          .type     = DevShapeType_Box + mode,
          .data_box = {.pos = pos, .rot = rot, .size = size, .color = color},
      });
}

void dev_quad(
    DevShapeComp*      comp,
    const GeoVector    pos,
    const GeoQuat      rot,
    const f32          sizeX,
    const f32          sizeY,
    const GeoColor     color,
    const DevShapeMode mode) {
  dev_shape_add(
      comp,
      (DevShape){
          .type      = DevShapeType_Quad + mode,
          .data_quad = {.pos = pos, .rot = rot, .sizeX = sizeX, .sizeY = sizeY, .color = color},
      });
}

void dev_sphere(
    DevShapeComp*      comp,
    const GeoVector    pos,
    const f32          radius,
    const GeoColor     color,
    const DevShapeMode mode) {
  dev_shape_add(
      comp,
      (DevShape){
          .type        = DevShapeType_Sphere + mode,
          .data_sphere = {.pos = pos, .radius = radius, .color = color},
      });
}

void dev_cylinder(
    DevShapeComp*      comp,
    const GeoVector    bottom,
    const GeoVector    top,
    const f32          radius,
    const GeoColor     color,
    const DevShapeMode mode) {
  dev_shape_add(
      comp,
      (DevShape){
          .type          = DevShapeType_Cylinder + mode,
          .data_cylinder = {.bottom = bottom, .top = top, .radius = radius, .color = color},
      });
}

void dev_capsule(
    DevShapeComp*      comp,
    const GeoVector    bottom,
    const GeoVector    top,
    const f32          radius,
    const GeoColor     color,
    const DevShapeMode mode) {
  GeoVector toTop = geo_vector_sub(top, bottom);
  if (geo_vector_mag_sqr(toTop) < 1e-6f) {
    toTop = geo_up;
  }
  const GeoVector toBottom = geo_vector_mul(toTop, -1.0f);

  dev_shape_add(
      comp,
      (DevShape){
          .type          = DevShapeType_CylinderUncapped + mode,
          .data_cylinder = {.bottom = bottom, .top = top, .radius = radius, .color = color},
      });

  dev_shape_add(
      comp,
      (DevShape){
          .type = DevShapeType_HemisphereUncapped + mode,
          .data_sphere =
              {.pos    = top,
               .rot    = geo_quat_look(toTop, geo_forward),
               .radius = radius,
               .color  = color},
      });

  dev_shape_add(
      comp,
      (DevShape){
          .type = DevShapeType_HemisphereUncapped + mode,
          .data_sphere =
              {.pos    = bottom,
               .rot    = geo_quat_look(toBottom, geo_forward),
               .radius = radius,
               .color  = color},
      });
}

void dev_cone(
    DevShapeComp*      comp,
    const GeoVector    bottom,
    const GeoVector    top,
    const f32          radius,
    const GeoColor     color,
    const DevShapeMode mode) {
  dev_shape_add(
      comp,
      (DevShape){
          .type      = DevShapeType_Cone + mode,
          .data_cone = {.bottom = bottom, .top = top, .radius = radius, .color = color},
      });
}

void dev_line(
    DevShapeComp* comp, const GeoVector start, const GeoVector end, const GeoColor color) {
  dev_shape_add(
      comp,
      (DevShape){
          .type      = DevShapeType_Line + DevShape_Overlay,
          .data_line = {.start = start, .end = end, .color = color},
      });
}

void dev_circle(
    DevShapeComp*   comp,
    const GeoVector pos,
    const GeoQuat   rot,
    const f32       radius,
    const GeoColor  color) {
  enum { Segments = 16 };
  const f32 step = math_pi_f32 * 2.0f / Segments;
  GeoVector points[Segments];
  for (u32 i = 0; i != Segments; ++i) {
    const f32       angle = i * step;
    const GeoVector point = geo_vector(math_sin_f32(angle) * radius, math_cos_f32(angle) * radius);
    points[i]             = geo_vector_add(pos, geo_quat_rotate(rot, point));
  }
  for (u32 i = 0; i != Segments; ++i) {
    dev_line(comp, points[i], points[(i + 1) % Segments], color);
  }
}

void dev_arrow(
    DevShapeComp*   comp,
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
  dev_cone(comp, tipStart, end, radius, color, DevShape_Overlay);

  const f32 baseLength = dist - tipLength;
  if (baseLength > f32_epsilon) {
    dev_cylinder(comp, begin, tipStart, radius * g_baseRadiusMult, color, DevShape_Overlay);
  }
}

void dev_orientation(DevShapeComp* comp, const GeoVector pos, const GeoQuat rot, const f32 size) {
  static const f32 g_startOffsetMult = 0.05f;
  static const f32 g_radiusMult      = 0.1f;

  const GeoVector right   = geo_quat_rotate(rot, geo_right);
  const GeoVector up      = geo_quat_rotate(rot, geo_up);
  const GeoVector forward = geo_quat_rotate(rot, geo_forward);
  const f32       radius  = size * g_radiusMult;

  const GeoVector startRight = geo_vector_add(pos, geo_vector_mul(right, g_startOffsetMult));
  const GeoVector endRight   = geo_vector_add(pos, geo_vector_mul(right, size));
  dev_arrow(comp, startRight, endRight, radius, geo_color_red);

  const GeoVector startUp = geo_vector_add(pos, geo_vector_mul(up, g_startOffsetMult));
  const GeoVector endUp   = geo_vector_add(pos, geo_vector_mul(up, size));
  dev_arrow(comp, startUp, endUp, radius, geo_color_green);

  const GeoVector startForward = geo_vector_add(pos, geo_vector_mul(forward, g_startOffsetMult));
  const GeoVector endForward   = geo_vector_add(pos, geo_vector_mul(forward, size));
  dev_arrow(comp, startForward, endForward, radius, geo_color_blue);
}

void dev_plane(DevShapeComp* comp, const GeoVector pos, const GeoQuat rot, const GeoColor color) {
  const f32 quadSize = 1.0f;
  dev_quad(comp, pos, rot, quadSize, quadSize, color, DevShape_Overlay);

  const f32       arrowLength = 1.0f;
  const f32       arrowRadius = 0.1f;
  const GeoVector arrowNorm   = geo_quat_rotate(rot, geo_forward);
  const GeoVector arrowEnd    = geo_vector_add(pos, geo_vector_mul(arrowNorm, arrowLength));
  dev_arrow(comp, pos, arrowEnd, arrowRadius, color);
}

void dev_frustum_points(
    DevShapeComp* comp, const GeoVector points[PARAM_ARRAY_SIZE(8)], const GeoColor color) {
  // Near plane.
  dev_line(comp, points[0], points[1], color);
  dev_line(comp, points[1], points[2], color);
  dev_line(comp, points[2], points[3], color);
  dev_line(comp, points[3], points[0], color);

  // Far plane.
  dev_line(comp, points[4], points[5], color);
  dev_line(comp, points[5], points[6], color);
  dev_line(comp, points[6], points[7], color);
  dev_line(comp, points[7], points[4], color);

  // Connecting lines.
  dev_line(comp, points[0], points[4], color);
  dev_line(comp, points[1], points[5], color);
  dev_line(comp, points[2], points[6], color);
  dev_line(comp, points[3], points[7], color);
}

void dev_frustum_matrix(DevShapeComp* comp, const GeoMatrix* viewProj, const GeoColor color) {
  const GeoMatrix invViewProj = geo_matrix_inverse(viewProj);
  const f32       nearNdc     = 1.0f;
  const f32       farNdc      = 1e-8f; // NOTE: Using reverse-z with infinite far-plane.

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

  dev_frustum_points(comp, points, color);
}

void dev_world_box(DevShapeComp* shape, const GeoBox* b, const GeoColor color) {
  const GeoColor  colorDimmed = geo_color_mul_comps(color, geo_color(0.75f, 0.75f, 0.75f, 0.4f));
  const GeoVector center      = geo_box_center(b);
  const GeoVector size        = geo_box_size(b);

  dev_box(shape, center, geo_quat_ident, size, colorDimmed, DevShape_Fill);
  dev_box(shape, center, geo_quat_ident, size, color, DevShape_Wire);
}

void dev_world_box_rotated(DevShapeComp* shape, const GeoBoxRotated* b, const GeoColor color) {
  const GeoColor  colorDimmed = geo_color_mul_comps(color, geo_color(0.75f, 0.75f, 0.75f, 0.4f));
  const GeoVector center      = geo_box_center(&b->box);
  const GeoVector size        = geo_box_size(&b->box);
  const GeoQuat   rotation    = b->rotation;

  dev_box(shape, center, rotation, size, colorDimmed, DevShape_Fill);
  dev_box(shape, center, rotation, size, color, DevShape_Wire);
}

void dev_world_sphere(DevShapeComp* shape, const GeoSphere* s, const GeoColor color) {
  const GeoColor colorDimmed = geo_color_mul_comps(color, geo_color(0.75f, 0.75f, 0.75f, 0.4f));

  dev_sphere(shape, s->point, s->radius, colorDimmed, DevShape_Fill);
  dev_sphere(shape, s->point, s->radius, color, DevShape_Wire);
}

void dev_world_capsule(DevShapeComp* shape, const GeoCapsule* c, const GeoColor color) {
  const GeoColor colorDimmed = geo_color_mul_comps(color, geo_color(0.75f, 0.75f, 0.75f, 0.4f));

  dev_capsule(shape, c->line.a, c->line.b, c->radius, colorDimmed, DevShape_Fill);
  dev_capsule(shape, c->line.a, c->line.b, c->radius, color, DevShape_Wire);
}
