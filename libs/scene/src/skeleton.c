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
ecs_comp_define_public(SceneAnimationComp);

typedef enum {
  SkeletonTemplState_Start,
  SkeletonTemplState_LoadGraphic,
  SkeletonTemplState_LoadMesh,
  SkeletonTemplState_Finished,
} SkeletonTemplState;

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
  GeoVector t;
  GeoQuat   r;
  GeoVector s;
} SceneJointPose;

typedef struct {
  StringHash           nameHash;
  f32                  duration;
  SceneSkeletonChannel joints[asset_mesh_joints_max][AssetMeshAnimTarget_Count];
} SceneSkeletonAnim;

/**
 * NOTE: On the graphic asset.
 */
ecs_comp_define(SceneSkeletonTemplComp) {
  SkeletonTemplState    state;
  EcsEntityId           mesh;
  SceneSkeletonJoint*   joints;          // [jointCount].
  SceneSkeletonAnim*    anims;           // [animCount].
  const GeoMatrix*      bindPoseInvMats; // [jointCount].
  const SceneJointPose* defaultPose;     // [jointCount].
  u32                   jointCount;
  u32                   animCount;
  u32                   jointRootIndex;
  Mem                   animData;
};
ecs_comp_define(SceneSkeletonTemplLoadedComp);

static void ecs_destruct_skeleton_comp(void* data) {
  SceneSkeletonComp* sk = data;
  if (sk->jointCount) {
    alloc_free_array_t(g_alloc_heap, sk->jointTransforms, sk->jointCount);
  }
}

static void ecs_destruct_animation_comp(void* data) {
  SceneAnimationComp* anim = data;
  alloc_free_array_t(g_alloc_heap, anim->layers, anim->layerCount);
}

static void ecs_combine_skeleton_templ(void* dataA, void* dataB) {
  MAYBE_UNUSED SceneSkeletonTemplComp* tlA = dataA;
  MAYBE_UNUSED SceneSkeletonTemplComp* tlB = dataB;

  diag_assert_msg(
      tlA->state == SkeletonTemplState_Start && tlB->state == SkeletonTemplState_Start,
      "Skeleton templates can only be combined in the starting phase");
}

static void ecs_destruct_skeleton_templ_comp(void* data) {
  SceneSkeletonTemplComp* comp = data;
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

ecs_view_define(TemplLoadView) {
  ecs_access_write(SceneSkeletonTemplComp);
  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_without(SceneSkeletonTemplLoadedComp);
}

ecs_view_define(SkeletonInitView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_without(SceneSkeletonComp);
}

ecs_view_define(MeshView) {
  ecs_access_with(AssetMeshComp);
  ecs_access_read(AssetMeshSkeletonComp);
}

ecs_view_define(SkeletonTemplView) { ecs_access_read(SceneSkeletonTemplComp); }

static void scene_skeleton_init_empty(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(world, entity, SceneSkeletonComp);
}

static void scene_skeleton_init_from_templ(
    EcsWorld* world, const EcsEntityId entity, const SceneSkeletonTemplComp* tl) {
  if (!tl->jointCount) {
    scene_skeleton_init_empty(world, entity);
    return;
  }

  ecs_world_add_t(
      world,
      entity,
      SceneSkeletonComp,
      .jointCount      = tl->jointCount,
      .jointTransforms = alloc_array_t(g_alloc_heap, GeoMatrix, tl->jointCount));

  SceneAnimLayer* layers = alloc_array_t(g_alloc_heap, SceneAnimLayer, tl->animCount);
  for (u32 i = 0; i != tl->animCount; ++i) {
    layers[i] = (SceneAnimLayer){
        .animIndex = i,
        .nameHash  = tl->anims[i].nameHash,
        .duration  = tl->anims[i].duration,
        .speed     = 1.0f,
        .weight    = 1.0f,
    };
  }
  ecs_world_add_t(world, entity, SceneAnimationComp, .layers = layers, .layerCount = tl->animCount);
}

ecs_system_define(SceneSkeletonInitSys) {
  EcsView*     initView = ecs_world_view_t(world, SkeletonInitView);
  EcsIterator* templItr = ecs_view_itr(ecs_world_view_t(world, SkeletonTemplView));

  u32 startedLoads = 0;

  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    const EcsEntityId          graphic    = renderable->graphic;
    if (!graphic) {
      scene_skeleton_init_empty(world, entity);
      continue;
    }

    if (ecs_view_maybe_jump(templItr, graphic)) {
      const SceneSkeletonTemplComp* tl = ecs_view_read_t(templItr, SceneSkeletonTemplComp);
      if (tl->state == SkeletonTemplState_Finished) {
        scene_skeleton_init_from_templ(world, entity, tl);
      }
      continue;
    }

    if (++startedLoads > scene_skeleton_max_loads) {
      continue; // Limit the amount of loads to start in a single frame.
    }
    ecs_world_add_t(world, graphic, SceneSkeletonTemplComp);
  }
}

static bool scene_asset_is_loaded(EcsWorld* world, const EcsEntityId asset) {
  return ecs_world_has_t(world, asset, AssetLoadedComp) ||
         ecs_world_has_t(world, asset, AssetFailedComp);
}

static void scene_asset_templ_init(SceneSkeletonTemplComp* tl, const AssetMeshSkeletonComp* asset) {
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
    tl->anims[animIndex].duration  = assetAnim->duration;

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

  tl->bindPoseInvMats = (const GeoMatrix*)mem_at_u8(tl->animData, asset->bindPoseInvMats);
  tl->defaultPose     = (const SceneJointPose*)mem_at_u8(tl->animData, asset->defaultPose);
}

static void scene_skeleton_templ_load_done(EcsWorld* world, EcsIterator* itr) {
  const EcsEntityId       entity = ecs_view_entity(itr);
  SceneSkeletonTemplComp* tl     = ecs_view_write_t(itr, SceneSkeletonTemplComp);

  asset_release(world, entity);
  if (tl->mesh) {
    asset_release(world, tl->mesh);
  }
  tl->state = SkeletonTemplState_Finished;
  ecs_world_add_empty_t(world, entity, SceneSkeletonTemplLoadedComp);
}

ecs_system_define(SceneSkeletonTemplLoadSys) {
  EcsView*     loadView = ecs_world_view_t(world, TemplLoadView);
  EcsIterator* meshItr  = ecs_view_itr(ecs_world_view_t(world, MeshView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId       entity  = ecs_view_entity(itr);
    SceneSkeletonTemplComp* tl      = ecs_view_write_t(itr, SceneSkeletonTemplComp);
    const AssetGraphicComp* graphic = ecs_view_read_t(itr, AssetGraphicComp);
    switch (tl->state) {
    case SkeletonTemplState_Start: {
      asset_acquire(world, entity);
      ++tl->state;
      // Fallthrough.
    }
    case SkeletonTemplState_LoadGraphic: {
      if (!scene_asset_is_loaded(world, entity)) {
        break; // Graphic has not loaded yet; wait.
      }
      if (!graphic) {
        scene_skeleton_templ_load_done(world, itr);
        break; // Graphic failed to load, or was of unexpected type.
      }
      if (!graphic->mesh) {
        scene_skeleton_templ_load_done(world, itr);
        break; // Graphic did not have a mesh.
      }
      tl->mesh = graphic->mesh;
      asset_acquire(world, graphic->mesh);
      ++tl->state;
      // Fallthrough.
    }
    case SkeletonTemplState_LoadMesh: {
      if (!scene_asset_is_loaded(world, tl->mesh)) {
        break; // Mesh has not loaded yet; wait.
      }
      if (ecs_view_maybe_jump(meshItr, tl->mesh)) {
        scene_asset_templ_init(tl, ecs_view_read_t(meshItr, AssetMeshSkeletonComp));
      }
      scene_skeleton_templ_load_done(world, itr);
      break;
    }
    case SkeletonTemplState_Finished:
      diag_crash();
    }
  }
}

ecs_view_define(UpdateView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_write(SceneSkeletonComp);
  ecs_access_write(SceneAnimationComp);
}

static void anim_sample_default(const SceneSkeletonTemplComp* tl, SceneJointPose* out) {
  for (u32 j = 0; j != tl->jointCount; ++j) {
    out[j] = tl->defaultPose[j];
  }
}

static u32 anim_find_frame(const SceneSkeletonChannel* ch, const f32 t) {
  for (u32 i = 1; i != ch->frameCount; ++i) {
    if (ch->times[i] > t) {
      return i - 1;
    }
  }
  return 0;
}

static GeoVector anim_channel_get_vec3(const SceneSkeletonChannel* ch, const f32 t) {
  const u32 frame = anim_find_frame(ch, t);
  if (frame == ch->frameCount - 1) {
    return ch->values_vec[frame];
  }
  const f32 fromT = ch->times[frame];
  const f32 toT   = ch->times[frame + 1];
  const f32 frac  = math_unlerp(fromT, toT, t);
  return geo_vector_lerp(ch->values_vec[frame], ch->values_vec[frame + 1], frac);
}

static GeoQuat anim_channel_get_quat(const SceneSkeletonChannel* ch, const f32 t) {
  const u32 frame = anim_find_frame(ch, t);
  if (frame == ch->frameCount - 1) {
    return ch->values_quat[frame];
  }
  const f32 fromT = ch->times[frame];
  const f32 toT   = ch->times[frame + 1];
  const f32 frac  = math_unlerp(fromT, toT, t);
  return geo_quat_slerp(ch->values_quat[frame], ch->values_quat[frame + 1], frac);
}

static void anim_sample_layer(
    const SceneSkeletonTemplComp* tl, const SceneAnimLayer* layer, SceneJointPose* out) {
  const SceneSkeletonAnim* anim = &tl->anims[layer->animIndex];
  for (u32 j = 0; j != tl->jointCount; ++j) {
    const SceneSkeletonChannel* chT = &anim->joints[j][AssetMeshAnimTarget_Translation];
    if (chT->frameCount) {
      out[j].t = anim_channel_get_vec3(chT, layer->time);
    }

    const SceneSkeletonChannel* chR = &anim->joints[j][AssetMeshAnimTarget_Rotation];
    if (chR->frameCount) {
      out[j].r = anim_channel_get_quat(chR, layer->time);
    }

    const SceneSkeletonChannel* chS = &anim->joints[j][AssetMeshAnimTarget_Scale];
    if (chS->frameCount) {
      out[j].s = anim_channel_get_vec3(chS, layer->time);
    }
  }
}

static void anim_to_world(const SceneSkeletonTemplComp* tl, SceneJointPose* poses, GeoMatrix* out) {
  u32 stack[asset_mesh_joints_max] = {tl->jointRootIndex};
  u32 stackCount                   = 1;

  while (stackCount--) {
    const u32       joint   = stack[stackCount];
    const bool      isRoot  = joint == tl->jointRootIndex;
    const GeoMatrix poseMat = geo_matrix_trs(poses[joint].t, poses[joint].r, poses[joint].s);

    out[joint] = isRoot ? poseMat : geo_matrix_mul(&out[joint], &poseMat);

    for (u32 childNum = 0; childNum != tl->joints[joint].childCount; ++childNum) {
      const u32 childIndex = tl->joints[joint].childIndices[childNum];
      out[childIndex]      = out[joint];
      stack[stackCount++]  = childIndex;
    }
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

  EcsView*     updateView = ecs_world_view_t(world, UpdateView);
  EcsIterator* templItr   = ecs_view_itr(ecs_world_view_t(world, SkeletonTemplView));

  SceneJointPose poses[asset_mesh_joints_max];
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    SceneSkeletonComp*         sk         = ecs_view_write_t(itr, SceneSkeletonComp);
    SceneAnimationComp*        anim       = ecs_view_write_t(itr, SceneAnimationComp);

    ecs_view_jump(templItr, renderable->graphic);
    const SceneSkeletonTemplComp* tl = ecs_view_read_t(templItr, SceneSkeletonTemplComp);

    anim_sample_default(tl, poses);
    for (u32 i = 0; i != anim->layerCount; ++i) {
      SceneAnimLayer* layer = &anim->layers[i];
      layer->time += deltaSeconds * layer->speed;
      layer->time = math_mod_f32(layer->time, tl->anims[i].duration);
      if (layer->weight > 0.0f) {
        anim_sample_layer(tl, layer, poses);
      }
    }
    anim_to_world(tl, poses, sk->jointTransforms);
  }
}

ecs_module_init(scene_skeleton_module) {
  ecs_register_comp(SceneSkeletonComp, .destructor = ecs_destruct_skeleton_comp);
  ecs_register_comp(SceneAnimationComp, .destructor = ecs_destruct_animation_comp);
  ecs_register_comp(
      SceneSkeletonTemplComp,
      .combinator = ecs_combine_skeleton_templ,
      .destructor = ecs_destruct_skeleton_templ_comp);
  ecs_register_comp_empty(SceneSkeletonTemplLoadedComp);

  ecs_register_view(GlobalView);
  ecs_register_view(TemplLoadView);
  ecs_register_view(SkeletonInitView);
  ecs_register_view(MeshView);
  ecs_register_view(SkeletonTemplView);
  ecs_register_view(UpdateView);

  ecs_register_system(
      SceneSkeletonInitSys, ecs_view_id(SkeletonInitView), ecs_view_id(SkeletonTemplView));

  ecs_register_system(SceneSkeletonTemplLoadSys, ecs_view_id(TemplLoadView), ecs_view_id(MeshView));

  ecs_register_system(
      SceneSkeletonUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(UpdateView),
      ecs_view_id(SkeletonTemplView));
}

u32 scene_skeleton_root_index(const SceneSkeletonTemplComp* tl) { return tl->jointRootIndex; }

const SceneSkeletonJoint* scene_skeleton_joint(const SceneSkeletonTemplComp* tl, const u32 index) {
  diag_assert(index < tl->jointCount);
  return &tl->joints[index];
}

void scene_skeleton_delta(
    const SceneSkeletonComp* sk, const SceneSkeletonTemplComp* tl, GeoMatrix* out) {
  diag_assert(sk->jointCount == tl->jointCount);
  for (u32 i = 0; i != sk->jointCount; ++i) {
    out[i] = geo_matrix_mul(&sk->jointTransforms[i], &tl->bindPoseInvMats[i]);
  }
}
