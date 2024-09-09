#include "asset_manager.h"
#include "core_diag.h"
#include "core_float.h"
#include "ecs_world.h"
#include "rend_object.h"
#include "rend_register.h"
#include "scene_bounds.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_tag.h"
#include "scene_transform.h"
#include "scene_visibility.h"

#define rend_instance_max_obj_create_per_task 4

static const String g_rendInstanceDebugSkinning = string_static("graphics/debug/skinning.graphic");

typedef struct {
  ALIGNAS(16)
  GeoVector posAndScale; // xyz: position, w: scale.
  GeoQuat   rot;
  u32       tags;
  u32       color; // u8 r, u8 g, u8 b, u8 a
  f32       emissive;
  u32       padding[1];
} RendInstanceData;

ASSERT(sizeof(RendInstanceData) == 48, "Size needs to match the size defined in glsl");
ASSERT(alignof(RendInstanceData) == 16, "Alignment needs to match the glsl alignment");

typedef struct {
  ALIGNAS(16)
  f32 comps[12];
} RendMat3x4;

ASSERT(sizeof(RendMat3x4) == 48, "RendMat3x4 has to be 384 bits");
ASSERT(alignof(RendMat3x4) == 16, "RendMat3x4 has to be aligned to 128 bits");

typedef struct {
  ALIGNAS(16)
  GeoVector  posAndScale; // xyz: position, w: scale.
  GeoQuat    rot;
  u32        tags;
  u32        color; // u8 r, u8 g, u8 b, u8 a
  f32        emissive;
  u32        padding[1];
  RendMat3x4 jointDelta[scene_skeleton_joints_max];
} RendInstanceSkinnedData;

ASSERT(sizeof(RendInstanceSkinnedData) == 3648, "Size needs to match the size defined in glsl");
ASSERT(alignof(RendInstanceSkinnedData) == 16, "Alignment needs to match the glsl alignment");

ecs_comp_define(RendInstanceEnvComp) { EcsEntityId debugSkinningGraphic; };

ecs_view_define(FillGlobalView) {
  ecs_access_read(RendInstanceEnvComp);
  ecs_access_read(SceneVisibilityEnvComp);
}

/**
 * Convert the given 4x4 matrix to a 4x3 matrix (dropping the last row) and then transpose to a 3x4.
 * Reason for transposing is that it avoids needing padding between the columns.
 */
static RendMat3x4 rend_transpose_to_3x4(const GeoMatrix* m) {
  RendMat3x4 res;
  for (u32 i = 0; i != 3; ++i) {
    res.comps[i * 4 + 0] = m->comps[0 * 4 + i];
    res.comps[i * 4 + 1] = m->comps[1 * 4 + i];
    res.comps[i * 4 + 2] = m->comps[2 * 4 + i];
    res.comps[i * 4 + 3] = m->comps[3 * 4 + i];
  }
  return res;
}

static u32 rend_color_pack(const GeoColor color) {
  const u32 r = (u8)(color.r * 255.999f);
  const u32 g = (u8)(color.g * 255.999f);
  const u32 b = (u8)(color.b * 255.999f);
  const u32 a = (u8)(color.a * 255.999f);
  return r | (g << 8) | (b << 16) | (a << 24);
}

static SceneTags rend_tags(const SceneTagComp* tagComp, const SceneRenderableComp* renderable) {
  SceneTags tags = tagComp ? tagComp->tags : SceneTags_Default;
  if (renderable->color.a < 1.0f) {
    tags |= SceneTags_Transparent;
  }
  return tags;
}

static void rend_obj_init(
    EcsWorld*                  w,
    const RendInstanceEnvComp* env,
    const SceneRenderableComp* renderable,
    const RendObjectFlags      flags) {
  RendObjectComp* obj = rend_object_create(w, renderable->graphic, flags);
  rend_object_set_resource(obj, RendObjectResource_Graphic, renderable->graphic);
  rend_object_set_resource(obj, RendObjectResource_DebugSkinningGraphic, env->debugSkinningGraphic);
}

ecs_view_define(InitEnvView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_without(RendInstanceEnvComp);
}

ecs_system_define(RendInstanceIntEnvSys) {
  EcsView* initView = ecs_world_view_t(world, InitEnvView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    AssetManagerComp* assets = ecs_view_write_t(itr, AssetManagerComp);
    ecs_world_add_t(
        world,
        ecs_view_entity(itr),
        RendInstanceEnvComp,
        .debugSkinningGraphic = asset_lookup(world, assets, g_rendInstanceDebugSkinning));
  }
}

ecs_view_define(RenderableView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneBoundsComp);
  ecs_access_with(SceneSkeletonLoadedComp); // Wait until we know the entity is not skinned.
  ecs_access_without(SceneSkeletonComp);

  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneVisibilityComp);
}

ecs_view_define(ObjView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the render objects we create.

  ecs_access_write(RendObjectComp);
}

ecs_system_define(RendInstanceFillObjSys) {
  EcsView*     globalView = ecs_world_view_t(world, FillGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }
  const RendInstanceEnvComp*    instanceEnv = ecs_view_read_t(globalItr, RendInstanceEnvComp);
  const SceneVisibilityEnvComp* visEnv      = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);

  EcsView* renderables = ecs_world_view_t(world, RenderableView);
  EcsView* ObjView     = ecs_world_view_t(world, ObjView);

  u32 createdObjects = 0;

  EcsIterator* objItr = ecs_view_itr(ObjView);
  for (EcsIterator* itr = ecs_view_itr(renderables); ecs_view_walk(itr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    if (renderable->color.a <= f32_epsilon) {
      continue;
    }
    const SceneVisibilityComp* visComp = ecs_view_read_t(itr, SceneVisibilityComp);
    if (visComp && !scene_visible_for_render(visEnv, visComp)) {
      continue;
    }

    const SceneTagComp*       tagComp       = ecs_view_read_t(itr, SceneTagComp);
    const SceneTransformComp* transformComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scaleComp     = ecs_view_read_t(itr, SceneScaleComp);
    const SceneBoundsComp*    boundsComp    = ecs_view_read_t(itr, SceneBoundsComp);

    if (UNLIKELY(!ecs_world_has_t(world, renderable->graphic, RendObjectComp))) {
      if (++createdObjects <= rend_instance_max_obj_create_per_task) { // Limit new objs per frame.
        rend_obj_init(world, instanceEnv, renderable, RendObjectFlags_StandardGeometry);
      }
      continue;
    }

    ecs_view_jump(objItr, renderable->graphic);
    RendObjectComp* obj = ecs_view_write_t(objItr, RendObjectComp);

    const SceneTags tags     = rend_tags(tagComp, renderable);
    const GeoVector position = transformComp ? transformComp->position : geo_vector(0);
    const GeoQuat   rotation = transformComp ? transformComp->rotation : geo_quat_ident;
    const f32       scale    = scaleComp ? scaleComp->scale : 1.0f;
    const GeoBox    aabb     = scene_bounds_world(boundsComp, transformComp, scaleComp);

    RendInstanceData* data = rend_object_add_instance_t(obj, RendInstanceData, tags, aabb);
    data->posAndScale      = geo_vector(position.x, position.y, position.z, scale);
    data->rot              = rotation;
    data->tags             = (u32)tags;
    data->color            = rend_color_pack(renderable->color);
    data->emissive         = renderable->emissive;
  }
}

ecs_view_define(RenderableSkinnedView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneBoundsComp);
  ecs_access_read(SceneSkeletonComp);
  ecs_access_with(SceneSkeletonLoadedComp);

  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneVisibilityComp);
}

ecs_view_define(ObjSkinnedView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the draw's we create.

  ecs_access_write(RendObjectComp);
  ecs_access_maybe_read(SceneSkeletonTemplComp);
}

ecs_system_define(RendInstanceSkinnedFillObjSys) {
  EcsView*     globalView = ecs_world_view_t(world, FillGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }
  const RendInstanceEnvComp*    instanceEnv = ecs_view_read_t(globalItr, RendInstanceEnvComp);
  const SceneVisibilityEnvComp* visEnv      = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);

  EcsView* renderables = ecs_world_view_t(world, RenderableSkinnedView);
  EcsView* objView     = ecs_world_view_t(world, ObjSkinnedView);

  u32 createdObjects = 0;

  EcsIterator* objItr = ecs_view_itr(objView);
  for (EcsIterator* itr = ecs_view_itr(renderables); ecs_view_walk(itr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    if (renderable->color.a <= f32_epsilon) {
      continue;
    }
    const SceneVisibilityComp* visComp = ecs_view_read_t(itr, SceneVisibilityComp);
    if (visComp && !scene_visible_for_render(visEnv, visComp)) {
      continue;
    }

    const SceneTagComp*       tagComp       = ecs_view_read_t(itr, SceneTagComp);
    const SceneTransformComp* transformComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scaleComp     = ecs_view_read_t(itr, SceneScaleComp);
    const SceneBoundsComp*    boundsComp    = ecs_view_read_t(itr, SceneBoundsComp);
    const SceneSkeletonComp*  skeletonComp  = ecs_view_read_t(itr, SceneSkeletonComp);

    if (UNLIKELY(!ecs_world_has_t(world, renderable->graphic, RendObjectComp))) {
      if (++createdObjects <= rend_instance_max_obj_create_per_task) { // Limit new objs per frame.
        const RendObjectFlags flags = RendObjectFlags_StandardGeometry | RendObjectFlags_Skinned;
        rend_obj_init(world, instanceEnv, renderable, flags);
      }
      continue;
    }

    ecs_view_jump(objItr, renderable->graphic);
    RendObjectComp* obj = ecs_view_write_t(objItr, RendObjectComp);

    const SceneTags tags     = rend_tags(tagComp, renderable);
    const GeoVector position = transformComp ? transformComp->position : geo_vector(0);
    const GeoQuat   rotation = transformComp ? transformComp->rotation : geo_quat_ident;
    const f32       scale    = scaleComp ? scaleComp->scale : 1.0f;
    const GeoBox    aabb     = scene_bounds_world(boundsComp, transformComp, scaleComp);

    const SceneSkeletonTemplComp* templ = ecs_view_read_t(objItr, SceneSkeletonTemplComp);
    if (UNLIKELY(!templ)) {
      continue; // Template no longer available; possible when hot-loading the graphic.
    }

    GeoMatrix jointDeltas[scene_skeleton_joints_max];
    scene_skeleton_delta(skeletonComp, templ, jointDeltas);

    RendInstanceSkinnedData* data =
        rend_object_add_instance_t(obj, RendInstanceSkinnedData, tags, aabb);
    data->posAndScale = geo_vector(position.x, position.y, position.z, scale);
    data->rot         = rotation;
    data->tags        = (u32)tags;
    data->color       = rend_color_pack(renderable->color);
    data->emissive    = renderable->emissive;
    for (u32 i = 0; i != skeletonComp->jointCount; ++i) {
      data->jointDelta[i] = rend_transpose_to_3x4(&jointDeltas[i]);
    }
  }
}

ecs_module_init(rend_instance_module) {
  ecs_register_comp(RendInstanceEnvComp);

  ecs_register_view(FillGlobalView);

  ecs_register_system(RendInstanceIntEnvSys, ecs_register_view(InitEnvView));

  ecs_register_system(
      RendInstanceFillObjSys,
      ecs_view_id(FillGlobalView),
      ecs_register_view(RenderableView),
      ecs_register_view(ObjView));

  ecs_register_system(
      RendInstanceSkinnedFillObjSys,
      ecs_view_id(FillGlobalView),
      ecs_register_view(RenderableSkinnedView),
      ecs_register_view(ObjSkinnedView));

  ecs_order(RendInstanceFillObjSys, RendOrder_ObjectUpdate);
  ecs_order(RendInstanceSkinnedFillObjSys, RendOrder_ObjectUpdate);
}
