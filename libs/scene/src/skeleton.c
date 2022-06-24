#include "asset_graphic.h"
#include "asset_manager.h"
#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_time.h"

#define scene_skeleton_max_loads 16

ecs_comp_define_public(SceneSkeletonComp);

typedef enum {
  SkeletonTemplateState_Start,
  SkeletonTemplateState_LoadGraphic,
  SkeletonTemplateState_LoadMesh,
  SkeletonTemplateState_Finished,
} SkeletonTemplateState;

typedef struct {
  u32        frameCount;
  const f32* times;
  union {
    void*            values_raw;
    const GeoVector* values_vec;
    const GeoQuat*   values_quat;
  };
} SceneSkeletonChannel;

typedef struct {
  StringHash           nameHash;
  SceneSkeletonChannel joints[asset_mesh_joints_max][AssetMeshAnimTarget_Count];
} SceneSkeletonAnim;

/**
 * NOTE: On the graphic asset.
 */
ecs_comp_define(SceneSkeletonTemplateComp) {
  SkeletonTemplateState state;
  EcsEntityId           mesh;
  SceneSkeletonJoint*   joints;
  SceneSkeletonAnim*    anims;
  const GeoMatrix*      invBindMats;
  u32                   jointCount;
  u32                   animCount;
  u32                   jointRootIndex;
  Mem                   animData;
};

ecs_comp_define(SceneSkeletonTemplateLoadedComp);

static void ecs_destruct_skeleton_comp(void* data) {
  SceneSkeletonComp* sk = data;
  if (sk->jointCount) {
    alloc_free_array_t(g_alloc_heap, sk->jointTransforms, sk->jointCount);
  }
}

static void ecs_combine_skeleton_template(void* dataA, void* dataB) {
  MAYBE_UNUSED SceneSkeletonTemplateComp* tlA = dataA;
  MAYBE_UNUSED SceneSkeletonTemplateComp* tlB = dataB;

  diag_assert_msg(
      tlA->state == SkeletonTemplateState_Start && tlB->state == SkeletonTemplateState_Start,
      "Skeleton templates can only be combined in the starting phase");
}

static void ecs_destruct_skeleton_template_comp(void* data) {
  SceneSkeletonTemplateComp* comp = data;
  if (comp->jointCount) {
    alloc_free_array_t(g_alloc_heap, comp->joints, comp->jointCount);
  }
  if (comp->animCount) {
    alloc_free_array_t(g_alloc_heap, comp->anims, comp->animCount);
  }
  if (comp->animData.size) {
    alloc_free(g_alloc_heap, comp->animData);
  }
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(TemplateLoadView) {
  ecs_access_write(SceneSkeletonTemplateComp);
  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_without(SceneSkeletonTemplateLoadedComp);
}

ecs_view_define(SkeletonInitView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_without(SceneSkeletonComp);
}

ecs_view_define(MeshView) {
  ecs_access_with(AssetMeshComp);
  ecs_access_read(AssetMeshSkeletonComp);
}

ecs_view_define(SkeletonTemplateView) { ecs_access_read(SceneSkeletonTemplateComp); }

static void scene_skeleton_init_empty(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(world, entity, SceneSkeletonComp);
}

static void scene_skeleton_init_from_template(
    EcsWorld* world, const EcsEntityId entity, const SceneSkeletonTemplateComp* tl) {

  if (!tl->jointCount) {
    scene_skeleton_init_empty(world, entity);
    return;
  }

  GeoMatrix* jointTransforms = alloc_array_t(g_alloc_heap, GeoMatrix, tl->jointCount);
  for (u32 i = 0; i != tl->jointCount; ++i) {
    jointTransforms[i] = geo_matrix_inverse(&tl->invBindMats[i]);
  }
  ecs_world_add_t(
      world,
      entity,
      SceneSkeletonComp,
      .jointCount      = tl->jointCount,
      .jointTransforms = jointTransforms);
}

ecs_system_define(SceneSkeletonInitSys) {
  EcsView*     initView    = ecs_world_view_t(world, SkeletonInitView);
  EcsIterator* templateItr = ecs_view_itr(ecs_world_view_t(world, SkeletonTemplateView));

  u32 startedLoads = 0;

  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    const EcsEntityId          graphic    = renderable->graphic;
    if (!graphic) {
      scene_skeleton_init_empty(world, entity);
      continue;
    }

    if (ecs_view_maybe_jump(templateItr, graphic)) {
      const SceneSkeletonTemplateComp* tl = ecs_view_read_t(templateItr, SceneSkeletonTemplateComp);
      if (tl->state == SkeletonTemplateState_Finished) {
        scene_skeleton_init_from_template(world, entity, tl);
      }
      continue;
    }

    if (++startedLoads > scene_skeleton_max_loads) {
      continue; // Limit the amount of loads to start in a single frame.
    }
    ecs_world_add_t(world, graphic, SceneSkeletonTemplateComp);
  }
}

static bool scene_asset_is_loaded(EcsWorld* world, const EcsEntityId asset) {
  return ecs_world_has_t(world, asset, AssetLoadedComp) ||
         ecs_world_has_t(world, asset, AssetFailedComp);
}

static void
scene_asset_template_init(SceneSkeletonTemplateComp* tl, const AssetMeshSkeletonComp* asset) {
  tl->jointRootIndex = asset->rootJointIndex;
  tl->animData       = alloc_dup(g_alloc_heap, asset->animData, 1);

  tl->joints     = alloc_array_t(g_alloc_heap, SceneSkeletonJoint, asset->jointCount);
  tl->jointCount = asset->jointCount;
  for (u32 jointIndex = 0; jointIndex != asset->jointCount; ++jointIndex) {
    tl->joints[jointIndex] = (SceneSkeletonJoint){
        .childIndices = (const u32*)mem_at_u8(tl->animData, asset->joints[jointIndex].childData),
        .childCount   = asset->joints[jointIndex].childCount,
        .nameHash     = asset->joints[jointIndex].nameHash,
    };
  }

  tl->anims     = alloc_array_t(g_alloc_heap, SceneSkeletonAnim, asset->animCount);
  tl->animCount = asset->animCount;
  for (u32 animIndex = 0; animIndex != asset->animCount; ++animIndex) {
    const AssetMeshAnim* assetAnim = &asset->anims[animIndex];
    tl->anims[animIndex].nameHash  = assetAnim->nameHash;
    for (u32 jointIndex = 0; jointIndex != asset->jointCount; ++jointIndex) {
      for (AssetMeshAnimTarget target = 0; target != AssetMeshAnimTarget_Count; ++target) {
        const AssetMeshAnimChannel* assetChannel = &assetAnim->joints[jointIndex][target];

        tl->anims[animIndex].joints[jointIndex][target] = (SceneSkeletonChannel){
            .frameCount = assetChannel->frameCount,
            .times      = (const f32*)mem_at_u8(tl->animData, assetChannel->timeData),
            .values_raw = mem_at_u8(tl->animData, assetChannel->valueData),
        };
      }
    }
  }

  tl->invBindMats = (const GeoMatrix*)mem_at_u8(tl->animData, asset->invBindMats);
}

static void scene_skeleton_template_load_done(EcsWorld* world, EcsIterator* itr) {
  const EcsEntityId          entity = ecs_view_entity(itr);
  SceneSkeletonTemplateComp* tl     = ecs_view_write_t(itr, SceneSkeletonTemplateComp);

  asset_release(world, entity);
  if (tl->mesh) {
    asset_release(world, tl->mesh);
  }
  tl->state = SkeletonTemplateState_Finished;
  ecs_world_add_empty_t(world, entity, SceneSkeletonTemplateLoadedComp);
}

ecs_system_define(SceneSkeletonTemplateLoadSys) {
  EcsView*     loadView = ecs_world_view_t(world, TemplateLoadView);
  EcsIterator* meshItr  = ecs_view_itr(ecs_world_view_t(world, MeshView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId          entity  = ecs_view_entity(itr);
    SceneSkeletonTemplateComp* tl      = ecs_view_write_t(itr, SceneSkeletonTemplateComp);
    const AssetGraphicComp*    graphic = ecs_view_read_t(itr, AssetGraphicComp);
    switch (tl->state) {
    case SkeletonTemplateState_Start: {
      asset_acquire(world, entity);
      ++tl->state;
      // Fallthrough.
    }
    case SkeletonTemplateState_LoadGraphic: {
      if (!scene_asset_is_loaded(world, entity)) {
        break; // Graphic has not loaded yet; wait.
      }
      if (!graphic) {
        scene_skeleton_template_load_done(world, itr);
        break; // Graphic failed to load, or was of unexpected type.
      }
      if (!graphic->mesh) {
        scene_skeleton_template_load_done(world, itr);
        break; // Graphic did not have a mesh.
      }
      tl->mesh = graphic->mesh;
      asset_acquire(world, graphic->mesh);
      ++tl->state;
      // Fallthrough.
    }
    case SkeletonTemplateState_LoadMesh: {
      if (!scene_asset_is_loaded(world, tl->mesh)) {
        break; // Mesh has not loaded yet; wait.
      }
      if (ecs_view_maybe_jump(meshItr, tl->mesh)) {
        scene_asset_template_init(tl, ecs_view_read_t(meshItr, AssetMeshSkeletonComp));
      }
      scene_skeleton_template_load_done(world, itr);
      break;
    }
    case SkeletonTemplateState_Finished:
      diag_crash();
    }
  }
}

ecs_view_define(UpdateView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_write(SceneSkeletonComp);
}

static u32 scene_animation_from_frame(const SceneSkeletonChannel* ch, const f32 t) {
  for (u32 i = 1; i != ch->frameCount; ++i) {
    if (ch->times[i] > t) {
      return i - 1;
    }
  }
  return 0;
}

static u32 scene_animation_to_frame(const SceneSkeletonChannel* ch, const f32 t) {
  for (u32 i = 0; i != ch->frameCount; ++i) {
    if (ch->times[i] > t) {
      return i;
    }
  }
  return ch->frameCount - 1;
}

static GeoVector scene_animation_sample_vec3(const SceneSkeletonChannel* ch, const f32 t) {
  const u32 fromFrame = scene_animation_from_frame(ch, t);
  const u32 toFrame   = scene_animation_to_frame(ch, t);
  const f32 fromT     = ch->times[fromFrame];
  const f32 toT       = ch->times[toFrame];
  const f32 frac      = math_unlerp(fromT, toT, t);

  return geo_vector_lerp(ch->values_vec[fromFrame], ch->values_vec[toFrame], frac);
}

static GeoQuat scene_animation_sample_quat(const SceneSkeletonChannel* ch, const f32 t) {
  const u32 fromFrame = scene_animation_from_frame(ch, t);
  const u32 toFrame   = scene_animation_to_frame(ch, t);
  const f32 fromT     = ch->times[fromFrame];
  const f32 toT       = ch->times[toFrame];
  const f32 frac      = math_unlerp(fromT, toT, t);

  return geo_quat_slerp(ch->values_quat[fromFrame], ch->values_quat[toFrame], frac);
}

static void scene_animation_sample(
    const SceneSkeletonTemplateComp* tl, const u32 joint, const f32 t, GeoMatrix* out) {
  const SceneSkeletonAnim* anim = &tl->anims[2];

  const SceneSkeletonChannel* chT = &anim->joints[joint][AssetMeshAnimTarget_Translation];
  const SceneSkeletonChannel* chR = &anim->joints[joint][AssetMeshAnimTarget_Rotation];
  const SceneSkeletonChannel* chS = &anim->joints[joint][AssetMeshAnimTarget_Scale];

  const GeoMatrix tMat = geo_matrix_translate(scene_animation_sample_vec3(chT, t));
  out[joint]           = geo_matrix_mul(&out[joint], &tMat);

  GeoMatrix qMat = geo_matrix_from_quat(scene_animation_sample_quat(chR, t));
  out[joint]     = geo_matrix_mul(&out[joint], &qMat);

  const GeoMatrix sMat = geo_matrix_scale(scene_animation_sample_vec3(chS, t));
  out[joint]           = geo_matrix_mul(&out[joint], &sMat);

  for (u32 childNum = 0; childNum != tl->joints[joint].childCount; ++childNum) {
    const u32 childIndex = tl->joints[joint].childIndices[childNum];
    out[childIndex]      = out[joint];
    scene_animation_sample(tl, childIndex, t, out);
  }
}

ecs_system_define(SceneSkeletonUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = time->delta / (f32)time_second;

  EcsView*     updateView  = ecs_world_view_t(world, UpdateView);
  EcsIterator* templateItr = ecs_view_itr(ecs_world_view_t(world, SkeletonTemplateView));

  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    SceneSkeletonComp*         sk         = ecs_view_write_t(itr, SceneSkeletonComp);

    if (!sk->jointCount) {
      continue;
    }

    sk->playHead += deltaSeconds;
    sk->playHead = math_mod_f32(sk->playHead, 1.15f);

    ecs_view_jump(templateItr, renderable->graphic);
    const SceneSkeletonTemplateComp* tl = ecs_view_read_t(templateItr, SceneSkeletonTemplateComp);

    sk->jointTransforms[tl->jointRootIndex] = geo_matrix_ident();
    scene_animation_sample(tl, tl->jointRootIndex, sk->playHead, sk->jointTransforms);
  }
}

ecs_module_init(scene_skeleton_module) {
  ecs_register_comp(SceneSkeletonComp, .destructor = ecs_destruct_skeleton_comp);
  ecs_register_comp(
      SceneSkeletonTemplateComp,
      .combinator = ecs_combine_skeleton_template,
      .destructor = ecs_destruct_skeleton_template_comp);
  ecs_register_comp_empty(SceneSkeletonTemplateLoadedComp);

  ecs_register_view(GlobalView);
  ecs_register_view(TemplateLoadView);
  ecs_register_view(SkeletonInitView);
  ecs_register_view(MeshView);
  ecs_register_view(SkeletonTemplateView);
  ecs_register_view(UpdateView);

  ecs_register_system(
      SceneSkeletonInitSys, ecs_view_id(SkeletonInitView), ecs_view_id(SkeletonTemplateView));

  ecs_register_system(
      SceneSkeletonTemplateLoadSys, ecs_view_id(TemplateLoadView), ecs_view_id(MeshView));

  ecs_register_system(
      SceneSkeletonUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(UpdateView),
      ecs_view_id(SkeletonTemplateView));
}

u32 scene_skeleton_root_index(const SceneSkeletonTemplateComp* tl) { return tl->jointRootIndex; }

const SceneSkeletonJoint*
scene_skeleton_joint(const SceneSkeletonTemplateComp* tl, const u32 jointIndex) {
  diag_assert(jointIndex < tl->jointCount);
  return &tl->joints[jointIndex];
}

void scene_skeleton_joint_delta(
    const SceneSkeletonComp* sk, const SceneSkeletonTemplateComp* tl, GeoMatrix* out) {
  diag_assert(sk->jointCount == tl->jointCount);
  for (u32 i = 0; i != sk->jointCount; ++i) {
    out[i] = geo_matrix_mul(&sk->jointTransforms[i], &tl->invBindMats[i]);
  }
}
