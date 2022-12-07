#include "asset_graphic.h"
#include "asset_manager.h"
#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_time.h"
#include "scene_transform.h"

#define scene_skeleton_max_loads 16
#define scene_anim_duration_min 0.001f
#define scene_weight_min 0.001f
#define scene_weight_max 0.999f

ecs_comp_define_public(SceneSkeletonComp);
ecs_comp_define_public(SceneAnimationComp);

typedef enum {
  SkeletonTemplState_Start,
  SkeletonTemplState_LoadGraphic,
  SkeletonTemplState_LoadMesh,
  SkeletonTemplState_FinishedSuccess,
  SkeletonTemplState_FinishedFailure,
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
  StringHash           nameHash;
  f32                  duration;
  SceneSkeletonChannel joints[scene_skeleton_joints_max][AssetMeshAnimTarget_Count];
} SceneSkeletonAnim;

/**
 * NOTE: On the graphic asset.
 */
ecs_comp_define(SceneSkeletonTemplComp) {
  SkeletonTemplState    state;
  EcsEntityId           mesh;
  SceneSkeletonAnim*    anims;           // [animCount].
  const GeoMatrix*      bindPoseInvMats; // [jointCount].
  const SceneJointPose* defaultPose;     // [jointCount].
  const SceneJointPose* rootPose;        // [1].
  const u32*            parentIndices;   // [jointCount].
  const u32*            skinCounts;      // [jointCount]. Amount of verts skinned to each joint.
  const StringHash*     jointNames;      // [jointCount].
  GeoMatrix             rootTransform;
  u32                   jointCount;
  u32                   animCount;
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
      .jointCount        = tl->jointCount,
      .jointTransforms   = alloc_array_t(g_alloc_heap, GeoMatrix, tl->jointCount),
      .postTransJointIdx = sentinel_u32,
      .postTransMat      = geo_matrix_ident());

  SceneAnimLayer* layers = alloc_array_t(g_alloc_heap, SceneAnimLayer, tl->animCount);
  for (u32 i = 0; i != tl->animCount; ++i) {
    const bool isLowestLayer = i == tl->animCount - 1;
    const f32  startTime     = rng_sample_range(g_rng, 0, tl->anims->duration);
    layers[i]                = (SceneAnimLayer){
        .time     = startTime,
        .duration = tl->anims[i].duration,
        .speed    = 1.0f,
        .weight   = isLowestLayer ? 1.0f : 0.0f,
        .nameHash = tl->anims[i].nameHash,
        .flags    = SceneAnimFlags_Loop,
    };
    scene_skeleton_mask_set_rec(&layers[i].mask, tl, 0);
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
      if (tl->state == SkeletonTemplState_FinishedSuccess) {
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
  diag_assert(asset->jointCount <= scene_skeleton_joints_max);

  tl->jointCount = asset->jointCount;
  tl->animData   = alloc_dup(g_alloc_heap, asset->animData, 1);

  tl->anims     = alloc_array_t(g_alloc_heap, SceneSkeletonAnim, asset->animCount);
  tl->animCount = asset->animCount;
  for (u32 animIndex = 0; animIndex != asset->animCount; ++animIndex) {
    const AssetMeshAnim* assetAnim = &asset->anims[animIndex];
    tl->anims[animIndex].nameHash  = assetAnim->nameHash;
    tl->anims[animIndex].duration  = assetAnim->duration;

    for (u32 joint = 0; joint != asset->jointCount; ++joint) {
      for (AssetMeshAnimTarget target = 0; target != AssetMeshAnimTarget_Count; ++target) {
        const AssetMeshAnimChannel* assetChannel = &assetAnim->joints[joint][target];

        tl->anims[animIndex].joints[joint][target] = (SceneSkeletonChannel){
            .frameCount = assetChannel->frameCount,
            .times      = (const f32*)mem_at_u8(tl->animData, assetChannel->timeData),
            .values_raw = mem_at_u8(tl->animData, assetChannel->valueData),
        };
      }
    }
  }

  tl->bindPoseInvMats = (const GeoMatrix*)mem_at_u8(tl->animData, asset->bindPoseInvMats);
  tl->defaultPose     = (const SceneJointPose*)mem_at_u8(tl->animData, asset->defaultPose);
  tl->parentIndices   = (const u32*)mem_at_u8(tl->animData, asset->parentIndices);
  tl->skinCounts      = (const u32*)mem_at_u8(tl->animData, asset->skinCounts);
  tl->jointNames      = (const StringHash*)mem_at_u8(tl->animData, asset->jointNames);
  tl->rootPose        = (const SceneJointPose*)mem_at_u8(tl->animData, asset->rootTransform);
  tl->rootTransform   = geo_matrix_trs(tl->rootPose->t, tl->rootPose->r, tl->rootPose->s);
}

static void scene_skeleton_templ_load_done(EcsWorld* world, EcsIterator* itr, const bool failure) {
  const EcsEntityId       entity = ecs_view_entity(itr);
  SceneSkeletonTemplComp* tl     = ecs_view_write_t(itr, SceneSkeletonTemplComp);

  asset_release(world, entity);
  if (tl->mesh) {
    asset_release(world, tl->mesh);
  }
  tl->state = failure ? SkeletonTemplState_FinishedFailure : SkeletonTemplState_FinishedSuccess;
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
        scene_skeleton_templ_load_done(world, itr, false);
        break; // Graphic failed to load, or was of unexpected type.
      }
      if (!graphic->mesh) {
        scene_skeleton_templ_load_done(world, itr, false);
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
      const bool meshLoadFailure = ecs_world_has_t(world, tl->mesh, AssetFailedComp);
      scene_skeleton_templ_load_done(world, itr, meshLoadFailure);
      break;
    }
    case SkeletonTemplState_FinishedSuccess:
    case SkeletonTemplState_FinishedFailure:
      diag_crash();
    }
  }
}

static void anim_set_weights_neg1(const SceneSkeletonTemplComp* tl, f32* weights) {
  for (u32 c = 0; c != tl->jointCount * 3; ++c) {
    weights[c] = -1.0f;
  }
}

static u32 anim_find_frame(const SceneSkeletonChannel* ch, const f32 t) {
  /**
   * Binary search for the first frame with a higher time (and then return the frame before it).
   */
  u32 count = ch->frameCount;
  u32 begin = 0;
  while (count) {
    const u32 step   = count / 2;
    const u32 middle = begin + step;
    if (ch->times[middle] < t) {
      begin = middle + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  return begin - (begin ? 1 : 0);
}

static GeoVector anim_channel_get_vec(const SceneSkeletonChannel* ch, const f32 t) {
  const u32 frame = anim_find_frame(ch, t);
  if (frame == ch->frameCount - 1) {
    return ch->values_vec[frame];
  }
  const f32 fromT = ch->times[frame];
  const f32 toT   = ch->times[frame + 1];
  const f32 frac  = (t - fromT) / (toT - fromT);
  return geo_vector_lerp(ch->values_vec[frame], ch->values_vec[frame + 1], frac);
}

static GeoQuat anim_channel_get_quat(const SceneSkeletonChannel* ch, const f32 t) {
  const u32 frame = anim_find_frame(ch, t);
  if (frame == ch->frameCount - 1) {
    return ch->values_quat[frame];
  }
  const f32 fromT = ch->times[frame];
  const f32 toT   = ch->times[frame + 1];
  const f32 frac  = (t - fromT) / (toT - fromT);

  const GeoQuat from = ch->values_quat[frame];
  GeoQuat       to   = ch->values_quat[frame + 1];
  return geo_quat_slerp(from, to, frac);
}

static void anim_blend_vec(const GeoVector v, const f32 weight, f32* outWeight, GeoVector* outVec) {
  if (*outWeight < 0) {
    *outVec    = v;
    *outWeight = weight;
  } else {
    const f32 frac = (1.0f - *outWeight) * weight;
    *outVec        = geo_vector_lerp(*outVec, v, frac);
    *outWeight += frac;
  }
}

static void anim_blend_quat(GeoQuat q, const f32 weight, f32* outWeight, GeoQuat* outQuat) {
  if (*outWeight < 0) {
    *outQuat   = q;
    *outWeight = weight;
  } else {
    const f32 frac = (1.0f - *outWeight) * weight;
    if (geo_quat_dot(q, *outQuat) < 0) {
      // Compensate for quaternion double-cover (two quaternions representing the same rot).
      q = geo_quat_flip(q);
    }
    *outQuat = geo_quat_slerp(*outQuat, q, frac);
    *outWeight += frac;
  }
}

static void anim_sample_layer(
    const SceneSkeletonTemplComp* tl,
    const SceneAnimLayer*         layer,
    const u32                     layerIndex,
    f32*                          weights,
    SceneJointPose*               out) {
  const SceneSkeletonAnim* anim = &tl->anims[layerIndex];
  for (u32 j = 0; j != tl->jointCount; ++j) {
    if (!scene_skeleton_mask_test(&layer->mask, j)) {
      continue; // Layer is disabled for this joint.
    }

    f32* weightT = &weights[j * AssetMeshAnimTarget_Count + AssetMeshAnimTarget_Translation];
    f32* weightR = &weights[j * AssetMeshAnimTarget_Count + AssetMeshAnimTarget_Rotation];
    f32* weightS = &weights[j * AssetMeshAnimTarget_Count + AssetMeshAnimTarget_Scale];

    const SceneSkeletonChannel* chT = &anim->joints[j][AssetMeshAnimTarget_Translation];
    const SceneSkeletonChannel* chR = &anim->joints[j][AssetMeshAnimTarget_Rotation];
    const SceneSkeletonChannel* chS = &anim->joints[j][AssetMeshAnimTarget_Scale];

    if (chT->frameCount && *weightT < scene_weight_max) {
      anim_blend_vec(anim_channel_get_vec(chT, layer->time), layer->weight, weightT, &out[j].t);
    }
    if (chR->frameCount && *weightR < scene_weight_max) {
      anim_blend_quat(anim_channel_get_quat(chR, layer->time), layer->weight, weightR, &out[j].r);
    }
    if (chS->frameCount && *weightS < scene_weight_max) {
      anim_blend_vec(anim_channel_get_vec(chS, layer->time), layer->weight, weightS, &out[j].s);
    }
  }
}

static void anim_sample_def(const SceneSkeletonTemplComp* tl, f32* weights, SceneJointPose* out) {
  for (u32 j = 0; j != tl->jointCount; ++j) {
    f32* weightT = &weights[j * AssetMeshAnimTarget_Count + AssetMeshAnimTarget_Translation];
    f32* weightR = &weights[j * AssetMeshAnimTarget_Count + AssetMeshAnimTarget_Rotation];
    f32* weightS = &weights[j * AssetMeshAnimTarget_Count + AssetMeshAnimTarget_Scale];

    if (*weightT < scene_weight_max) {
      anim_blend_vec(tl->defaultPose[j].t, 1.0f, weightT, &out[j].t);
    }
    if (*weightR < scene_weight_max) {
      anim_blend_quat(tl->defaultPose[j].r, 1.0f, weightR, &out[j].r);
    }
    if (*weightS < scene_weight_max) {
      anim_blend_vec(tl->defaultPose[j].s, 1.0f, weightS, &out[j].s);
    }
  }
}

static void anim_apply(const SceneSkeletonTemplComp* tl, SceneJointPose* poses, GeoMatrix* out) {
  out[0] = tl->rootTransform;
  for (u32 joint = 0; joint != tl->jointCount; ++joint) {
    const GeoMatrix poseMat     = geo_matrix_trs(poses[joint].t, poses[joint].r, poses[joint].s);
    const u32       parentIndex = tl->parentIndices[joint];
    out[joint]                  = geo_matrix_mul(&out[parentIndex], &poseMat);
  }
}

static void anim_mul_all(const SceneSkeletonTemplComp* tl, const GeoMatrix* t, GeoMatrix* out) {
  for (u32 joint = 0; joint != tl->jointCount; ++joint) {
    out[joint] = geo_matrix_mul(t, &out[joint]);
  }
}

static void anim_mul_rec(
    const SceneSkeletonTemplComp* tl, const u32 joint, const GeoMatrix* t, GeoMatrix* out) {
  if (joint == 0) {
    anim_mul_all(tl, t, out);
    return;
  }
  out[joint]            = geo_matrix_mul(t, &out[joint]);
  const u32 parentIndex = tl->parentIndices[joint];
  for (u32 i = joint + 1; i != tl->jointCount && tl->parentIndices[i] > parentIndex; ++i) {
    out[i] = geo_matrix_mul(t, &out[i]);
  }
}

/**
 * Assign the weight based on the animation progress.
 */
static void anim_layer_auto_weight_fade(SceneAnimLayer* layer) {
  const f32 tQuad = (layer->time / layer->duration) * 4.0f;
  layer->weight   = 1.0f;
  if (layer->flags & SceneAnimFlags_AutoFadeIn) {
    // Fade-in over the first 25%.
    layer->weight = math_min(1.0f, tQuad);
  }
  if (layer->flags & SceneAnimFlags_AutoFadeOut) {
    // Fade-out over the last 25%.
    layer->weight -= math_max(0.0f, tQuad - 3.0f);
  }
}

ecs_view_define(UpdateView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_write(SceneSkeletonComp);
  ecs_access_write(SceneAnimationComp);
}

ecs_system_define(SceneSkeletonUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const f32 deltaSeconds = scene_delta_seconds(ecs_view_read_t(globalItr, SceneTimeComp));

  EcsView*     updateView = ecs_world_view_t(world, UpdateView);
  EcsIterator* templItr   = ecs_view_itr(ecs_world_view_t(world, SkeletonTemplView));

  SceneJointPose poses[scene_skeleton_joints_max];       // Per joint.
  f32            weights[scene_skeleton_joints_max * 3]; // Per joint per channel.

  /**
   * Sample the animation layers.
   * NOTE: System runs in multiple parallel steps.
   */
  for (EcsIterator* itr = ecs_view_itr_step(updateView, parCount, parIndex); ecs_view_walk(itr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    SceneSkeletonComp*         sk         = ecs_view_write_t(itr, SceneSkeletonComp);
    SceneAnimationComp*        anim       = ecs_view_write_t(itr, SceneAnimationComp);

    if (UNLIKELY(!ecs_world_has_t(world, renderable->graphic, SceneSkeletonTemplLoadedComp))) {
      // Template has been removed; reset the skeleton and animation.
      ecs_world_remove_t(world, ecs_view_entity(itr), SceneSkeletonComp);
      ecs_world_remove_t(world, ecs_view_entity(itr), SceneAnimationComp);
      continue;
    }

    ecs_view_jump(templItr, renderable->graphic);
    const SceneSkeletonTemplComp* tl = ecs_view_read_t(templItr, SceneSkeletonTemplComp);

    anim_set_weights_neg1(tl, weights);

    for (u32 i = 0; i != anim->layerCount; ++i) {
      SceneAnimLayer* layer = &anim->layers[i];
      if (LIKELY(layer->duration > scene_anim_duration_min)) {
        layer->time += deltaSeconds * layer->speed;
        if (layer->flags & SceneAnimFlags_Loop) {
          layer->time = math_mod_f32(layer->time, layer->duration);
        } else if (layer->time > layer->duration) {
          layer->time = layer->duration;
        }
      }
      if (layer->flags & SceneAnimFlags_AutoFade) {
        anim_layer_auto_weight_fade(layer);
      }
      if (layer->weight > scene_weight_min) {
        anim_sample_layer(tl, layer, i, weights, poses);
      }
    }
    anim_sample_def(tl, weights, poses);
    anim_apply(tl, poses, sk->jointTransforms);

    if (!sentinel_check(sk->postTransJointIdx)) {
      anim_mul_rec(tl, sk->postTransJointIdx, &sk->postTransMat, sk->jointTransforms);
    }
  }
}

ecs_view_define(DirtyTemplateView) {
  ecs_access_with(SceneSkeletonTemplComp);
  ecs_access_with(SceneSkeletonTemplLoadedComp);
  ecs_access_with(AssetChangedComp);
}

ecs_system_define(SceneSkeletonClearDirtyTemplateSys) {
  /**
   * Clear skeleton templates for changed graphic assets.
   */
  EcsView* dirtyTemplateView = ecs_world_view_t(world, DirtyTemplateView);
  for (EcsIterator* itr = ecs_view_itr(dirtyTemplateView); ecs_view_walk(itr);) {
    ecs_world_remove_t(world, ecs_view_entity(itr), SceneSkeletonTemplComp);
    ecs_world_remove_t(world, ecs_view_entity(itr), SceneSkeletonTemplLoadedComp);
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
  ecs_register_view(DirtyTemplateView);

  ecs_register_system(
      SceneSkeletonInitSys, ecs_view_id(SkeletonInitView), ecs_view_id(SkeletonTemplView));

  ecs_register_system(SceneSkeletonTemplLoadSys, ecs_view_id(TemplLoadView), ecs_view_id(MeshView));

  ecs_register_system(
      SceneSkeletonUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(UpdateView),
      ecs_view_id(SkeletonTemplView));

  ecs_parallel(SceneSkeletonUpdateSys, 8);

  ecs_register_system(SceneSkeletonClearDirtyTemplateSys, ecs_view_id(DirtyTemplateView));
}

SceneAnimLayer* scene_animation_layer(SceneAnimationComp* anim, const StringHash layer) {
  for (u32 i = 0; i != anim->layerCount; ++i) {
    if (anim->layers[i].nameHash == layer) {
      return &anim->layers[i];
    }
  }
  return null;
}

bool scene_animation_set_time(SceneAnimationComp* anim, const StringHash layer, const f32 time) {
  SceneAnimLayer* state = scene_animation_layer(anim, layer);
  if (state) {
    state->time = time;
    return true;
  }
  return false;
}

bool scene_animation_set_weight(
    SceneAnimationComp* anim, const StringHash layer, const f32 weight) {
  SceneAnimLayer* state = scene_animation_layer(anim, layer);
  if (state) {
    state->weight = weight;
    return true;
  }
  return false;
}

void scene_skeleton_post_transform(SceneSkeletonComp* sk, const u32 joint, const GeoMatrix* m) {
  sk->postTransJointIdx = joint;
  sk->postTransMat      = *m;
}

u32 scene_skeleton_joint_count(const SceneSkeletonTemplComp* tl) { return tl->jointCount; }

StringHash scene_skeleton_joint_name(const SceneSkeletonTemplComp* tl, const u32 joint) {
  diag_assert(joint < tl->jointCount);
  return tl->jointNames[joint];
}

u32 scene_skeleton_joint_parent(const SceneSkeletonTemplComp* tl, const u32 joint) {
  diag_assert(joint < tl->jointCount);
  return tl->parentIndices[joint];
}

u32 scene_skeleton_joint_skin_count(const SceneSkeletonTemplComp* tl, const u32 joint) {
  diag_assert(joint < tl->jointCount);
  return tl->skinCounts[joint];
}

GeoMatrix scene_skeleton_joint_world(
    const SceneTransformComp* trans,
    const SceneScaleComp*     scale,
    const SceneSkeletonComp*  skel,
    const u32                 joint) {
  diag_assert(joint < skel->jointCount);
  const GeoMatrix world = scene_matrix_world(trans, scale);
  return geo_matrix_mul(&world, &skel->jointTransforms[joint]);
}

u32 scene_skeleton_joint_by_name(const SceneSkeletonTemplComp* tl, const StringHash name) {
  for (u32 joint = 0; joint != tl->jointCount; ++joint) {
    if (tl->jointNames[joint] == name) {
      return joint;
    }
  }
  return sentinel_u32;
}

SceneJointInfo
scene_skeleton_info(const SceneSkeletonTemplComp* tl, const u32 layer, const u32 joint) {
  diag_assert(layer < tl->animCount);
  diag_assert(joint < tl->jointCount);
  return (SceneJointInfo){
      .frameCountT = tl->anims[layer].joints[joint][AssetMeshAnimTarget_Translation].frameCount,
      .frameCountR = tl->anims[layer].joints[joint][AssetMeshAnimTarget_Rotation].frameCount,
      .frameCountS = tl->anims[layer].joints[joint][AssetMeshAnimTarget_Scale].frameCount,
  };
}

SceneJointPose scene_skeleton_sample(
    const SceneSkeletonTemplComp* tl, const u32 layer, const u32 joint, const f32 time) {
  diag_assert(layer < tl->animCount);
  diag_assert(joint < tl->jointCount);

  const SceneSkeletonChannel* chT =
      &tl->anims[layer].joints[joint][AssetMeshAnimTarget_Translation];
  const SceneSkeletonChannel* chR = &tl->anims[layer].joints[joint][AssetMeshAnimTarget_Rotation];
  const SceneSkeletonChannel* chS = &tl->anims[layer].joints[joint][AssetMeshAnimTarget_Scale];

  return (SceneJointPose){
      .t = chT->frameCount ? anim_channel_get_vec(chT, time) : geo_vector(0),
      .r = chR->frameCount ? anim_channel_get_quat(chR, time) : geo_quat_ident,
      .s = chS->frameCount ? anim_channel_get_vec(chS, time) : geo_vector(1, 1, 1),
  };
}

SceneJointPose scene_skeleton_sample_def(const SceneSkeletonTemplComp* tl, const u32 joint) {
  diag_assert(joint < tl->jointCount);
  return tl->defaultPose[joint];
}

SceneJointPose scene_skeleton_root(const SceneSkeletonTemplComp* tl) { return *tl->rootPose; }

void scene_skeleton_mask_set(SceneSkeletonMask* mask, const u32 joint) {
  bitset_set(bitset_from_array(mask->jointBits), joint);
}

void scene_skeleton_mask_set_rec(
    SceneSkeletonMask* mask, const SceneSkeletonTemplComp* tl, const u32 joint) {
  const BitSet bitset     = bitset_from_array(mask->jointBits);
  const u32    jointCount = scene_skeleton_joint_count(tl);
  if (joint == 0) {
    bitset_set_all(bitset, jointCount);
    return;
  }
  const u32 parentIndex = tl->parentIndices[joint];

  diag_assert(joint < jointCount);

  bitset_set(bitset, joint);
  for (u32 i = joint + 1; i != jointCount && tl->parentIndices[i] > parentIndex; ++i) {
    bitset_set(bitset, i);
  }
}

void scene_skeleton_mask_clear(SceneSkeletonMask* mask, const u32 joint) {
  bitset_clear(bitset_from_array(mask->jointBits), joint);
}

void scene_skeleton_mask_clear_rec(
    SceneSkeletonMask* mask, const SceneSkeletonTemplComp* tl, const u32 joint) {
  const BitSet bitset = bitset_from_array(mask->jointBits);
  if (joint == 0) {
    bitset_clear_all(bitset);
    return;
  }
  const u32 jointCount  = scene_skeleton_joint_count(tl);
  const u32 parentIndex = tl->parentIndices[joint];

  diag_assert(joint < jointCount);

  bitset_clear(bitset, joint);
  for (u32 i = joint + 1; i != jointCount && tl->parentIndices[i] > parentIndex; ++i) {
    bitset_clear(bitset, i);
  }
}

bool scene_skeleton_mask_test(const SceneSkeletonMask* mask, const u32 joint) {
  return (mask->jointBits[bits_to_bytes(joint)] & (1u << bit_in_byte(joint))) != 0;
}

void scene_skeleton_delta(
    const SceneSkeletonComp* sk, const SceneSkeletonTemplComp* tl, GeoMatrix* restrict out) {
  diag_assert(sk->jointCount == tl->jointCount);
  geo_matrix_mul_batch(sk->jointTransforms, tl->bindPoseInvMats, out, sk->jointCount);
}
