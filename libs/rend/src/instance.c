#include "core_diag.h"
#include "core_float.h"
#include "ecs_world.h"
#include "rend_draw.h"
#include "rend_register.h"
#include "scene_bounds.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_tag.h"
#include "scene_transform.h"
#include "scene_visibility.h"

#define rend_instance_max_draw_create_per_task 4

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

ecs_comp_define(RendInstanceDrawComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneVisibilityEnvComp); }

ecs_view_define(RenderableView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneBoundsComp);
  ecs_access_with(SceneSkeletonLoadedComp); // Wait until the skeleton is available (if applicable).

  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneSkeletonComp);
  ecs_access_maybe_read(SceneVisibilityComp);
}

ecs_view_define(DrawView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the draw's we create.

  ecs_access_write(RendDrawComp);
  ecs_access_maybe_read(SceneSkeletonTemplComp);
}

ecs_system_define(RendInstanceFillDrawsSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not yet available.
  }
  const SceneVisibilityEnvComp* visEnv = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);

  EcsView* renderables = ecs_world_view_t(world, RenderableView);
  EcsView* drawView    = ecs_world_view_t(world, DrawView);

  u32 createdDraws = 0;

  EcsIterator* drawItr = ecs_view_itr(drawView);
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
    const bool                isSkinned     = skeletonComp != null;

    SceneTags tags = tagComp ? tagComp->tags : SceneTags_Default;
    if (renderable->color.a < 1.0f) {
      tags |= SceneTags_Transparent;
    }

    if (UNLIKELY(!ecs_world_has_t(world, renderable->graphic, RendDrawComp))) {
      if (++createdDraws > rend_instance_max_draw_create_per_task) {
        continue; // Limit the amount of new draws to create per frame.
      }
      const RendDrawFlags flags = isSkinned ? RendDrawFlags_StandardGeometry | RendDrawFlags_Skinned
                                            : RendDrawFlags_StandardGeometry;
      RendDrawComp*       draw  = rend_draw_create(world, renderable->graphic, flags);
      rend_draw_set_resource(draw, RendDrawResource_Graphic, renderable->graphic);
      continue;
    }

    ecs_view_jump(drawItr, renderable->graphic);
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);

    const GeoVector position = transformComp ? transformComp->position : geo_vector(0);
    const GeoQuat   rotation = transformComp ? transformComp->rotation : geo_quat_ident;
    const f32       scale    = scaleComp ? scaleComp->scale : 1.0f;
    const GeoBox    aabb     = scene_bounds_world(boundsComp, transformComp, scaleComp);

    if (isSkinned) {
      const SceneSkeletonTemplComp* templ = ecs_view_read_t(drawItr, SceneSkeletonTemplComp);
      if (LIKELY(templ)) {
        GeoMatrix jointDeltas[scene_skeleton_joints_max];
        scene_skeleton_delta(skeletonComp, templ, jointDeltas);

        RendInstanceSkinnedData* data =
            rend_draw_add_instance_t(draw, RendInstanceSkinnedData, tags, aabb);
        data->posAndScale = geo_vector(position.x, position.y, position.z, scale);
        data->rot         = rotation;
        data->tags        = (u32)tags;
        data->color       = rend_color_pack(renderable->color);
        data->emissive    = renderable->emissive;
        for (u32 i = 0; i != skeletonComp->jointCount; ++i) {
          data->jointDelta[i] = rend_transpose_to_3x4(&jointDeltas[i]);
        }
      }
    } else /* !isSkinned */ {
      RendInstanceData* data = rend_draw_add_instance_t(draw, RendInstanceData, tags, aabb);
      data->posAndScale      = geo_vector(position.x, position.y, position.z, scale);
      data->rot              = rotation;
      data->tags             = (u32)tags;
      data->color            = rend_color_pack(renderable->color);
      data->emissive         = renderable->emissive;
    }
  }
}

ecs_module_init(rend_instance_module) {
  ecs_register_view(GlobalView);
  ecs_register_view(RenderableView);
  ecs_register_view(DrawView);

  ecs_register_system(
      RendInstanceFillDrawsSys,
      ecs_view_id(GlobalView),
      ecs_view_id(RenderableView),
      ecs_view_id(DrawView));

  ecs_order(RendInstanceFillDrawsSys, RendOrder_DrawCollect);
}
