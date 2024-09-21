#include "asset_graphic.h"
#include "asset_manager.h"
#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_bounds.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_time.h"
#include "scene_transform.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

#define scene_skeleton_max_loads 16
#define scene_anim_duration_min 0.001f
#define scene_weight_min 0.001f
#define scene_weight_max 0.999f

ecs_comp_define_public(SceneSkeletonComp);
ecs_comp_define_public(SceneAnimationComp);
ecs_comp_define(SceneSkeletonLoadedComp);

typedef enum {
  SkeletonTemplState_Start,
  SkeletonTemplState_LoadGraphic,
  SkeletonTemplState_LoadMesh,
  SkeletonTemplState_FinishedSuccess,
  SkeletonTemplState_FinishedFailure,
} SkeletonTemplState;

typedef struct {
  u32        frameCount;
  const u16* times; // Normalized, fractions of the anim duration.
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
  SceneSkeletonAnim*    anims;          // [animCount].
  const GeoMatrix*      bindMatInv;     // [jointCount].
  const SceneJointPose* defaultPose;    // [jointCount].
  const SceneJointPose* rootPose;       // [1].
  const u32*            parentIndices;  // [jointCount].
  const u32*            skinCounts;     // [jointCount]. Amount of verts skinned to each joint.
  const f32*            boundingRadius; // f32[jointCount]. Bounding sphere radius for each joint.
  const StringHash*     jointNames;     // [jointCount].
  GeoMatrix             rootTransform;
  u32                   jointCount;
  u32                   animCount;
  Mem                   data;
};
ecs_comp_define(SceneSkeletonTemplLoadedComp);

static void ecs_destruct_skeleton_comp(void* data) {
  SceneSkeletonComp* sk = data;
  alloc_free_array_t(g_allocHeap, sk->jointTransforms, sk->jointCount);
}

static void ecs_destruct_animation_comp(void* data) {
  SceneAnimationComp* anim = data;
  alloc_free_array_t(g_allocHeap, anim->layers, anim->layerCount);
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
    alloc_free_array_t(g_allocHeap, comp->anims, comp->animCount);
  }
  if (comp->data.size) {
    alloc_free(g_allocHeap, comp->data);
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
  ecs_access_without(SceneSkeletonLoadedComp);
}

ecs_view_define(MeshView) {
  ecs_access_with(AssetMeshComp);
  ecs_access_read(AssetMeshSkeletonComp);
}

ecs_view_define(SkeletonTemplView) { ecs_access_read(SceneSkeletonTemplComp); }

static void
scene_skeleton_init(EcsWorld* world, const EcsEntityId entity, const SceneSkeletonTemplComp* tl) {
  if (!tl->jointCount) {
    return;
  }

  SceneSkeletonComp* skel = ecs_world_add_t(
      world,
      entity,
      SceneSkeletonComp,
      .jointCount        = tl->jointCount,
      .jointTransforms   = alloc_array_t(g_allocHeap, GeoMatrix, tl->jointCount),
      .postTransJointIdx = sentinel_u32,
      .postTransMat      = geo_matrix_ident());

  // Initialize all joint transforms.
  for (u32 i = 0; i != tl->jointCount; ++i) {
    skel->jointTransforms[i] = geo_matrix_ident();
  }

  SceneAnimLayer* layers = alloc_array_t(g_allocHeap, SceneAnimLayer, tl->animCount);
  for (u32 i = 0; i != tl->animCount; ++i) {
    const bool isLowestLayer = i == tl->animCount - 1;

    layers[i] = (SceneAnimLayer){
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

static bool scene_graphic_asset_valid(EcsWorld* world, const EcsEntityId assetEntity) {
  return ecs_world_exists(world, assetEntity) && ecs_world_has_t(world, assetEntity, AssetComp);
}

ecs_system_define(SceneSkeletonInitSys) {
  EcsView*     initView = ecs_world_view_t(world, SkeletonInitView);
  EcsIterator* templItr = ecs_view_itr(ecs_world_view_t(world, SkeletonTemplView));

  u32 startedLoads = 0;

  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    const EcsEntityId          graphic    = renderable->graphic;
    if (UNLIKELY(!graphic || !scene_graphic_asset_valid(world, graphic))) {
      ecs_world_add_empty_t(world, entity, SceneSkeletonLoadedComp);
      continue;
    }

    if (ecs_view_maybe_jump(templItr, graphic)) {
      const SceneSkeletonTemplComp* tl = ecs_view_read_t(templItr, SceneSkeletonTemplComp);
      if (tl->state == SkeletonTemplState_FinishedSuccess) {
        scene_skeleton_init(world, entity, tl);
        ecs_world_add_empty_t(world, entity, SceneSkeletonLoadedComp);
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
  tl->data       = alloc_dup(g_allocHeap, data_mem(asset->data), 16);

  tl->anims     = alloc_array_t(g_allocHeap, SceneSkeletonAnim, asset->anims.count);
  tl->animCount = (u32)asset->anims.count;
  for (u32 animIndex = 0; animIndex != asset->anims.count; ++animIndex) {
    const AssetMeshAnim* assetAnim = &asset->anims.values[animIndex];
    tl->anims[animIndex].nameHash  = string_hash(assetAnim->name);
    tl->anims[animIndex].duration  = assetAnim->duration;

    for (u32 joint = 0; joint != asset->jointCount; ++joint) {
      for (AssetMeshAnimTarget target = 0; target != AssetMeshAnimTarget_Count; ++target) {
        const AssetMeshAnimChannel* assetChannel = &assetAnim->joints[joint][target];

        tl->anims[animIndex].joints[joint][target] = (SceneSkeletonChannel){
            .frameCount = assetChannel->frameCount,
            .times      = (const u16*)mem_at_u8(tl->data, assetChannel->timeData),
            .values_raw = mem_at_u8(tl->data, assetChannel->valueData),
        };
      }
    }
  }

  tl->bindMatInv     = (const GeoMatrix*)mem_at_u8(tl->data, asset->bindMatInv);
  tl->defaultPose    = (const SceneJointPose*)mem_at_u8(tl->data, asset->defaultPose);
  tl->parentIndices  = (const u32*)mem_at_u8(tl->data, asset->parentIndices);
  tl->skinCounts     = (const u32*)mem_at_u8(tl->data, asset->skinCounts);
  tl->boundingRadius = (const f32*)mem_at_u8(tl->data, asset->boundingRadius);
  tl->jointNames     = (const StringHash*)mem_at_u8(tl->data, asset->jointNameHashes);
  tl->rootPose       = (const SceneJointPose*)mem_at_u8(tl->data, asset->rootTransform);
  tl->rootTransform  = geo_matrix_trs(tl->rootPose->t, tl->rootPose->r, tl->rootPose->s);

  // Add the joint names to the string-table for debug purposes.
  const u8* jointNamesItr = mem_at_u8(tl->data, asset->jointNames);
  for (u32 joint = 0; joint != asset->jointCount; ++joint) {
    const u8 size = *jointNamesItr++;
    stringtable_add(g_stringtable, mem_create(jointNamesItr, size));
    jointNamesItr += size;
  }
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
        log_e("Invalid graphic asset", log_param("entity", ecs_entity_fmt(entity)));
        scene_skeleton_templ_load_done(world, itr, false /* failure */);
        break; // Graphic failed to load, or was of unexpected type.
      }
      if (!graphic->mesh) {
        scene_skeleton_templ_load_done(world, itr, false /* failure */);
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

/**
 * Search for the first frame with a higher (or equal) time (and then return the frame before it).
 * Pre-condition: ch->frameCount > 1.
 * Pre-condition: tNorm16 > ch->times[0].
 * Pre-condition: tNorm16 < ch->times[ch->frameCount - 1].
 */
INLINE_HINT static u32 anim_find_frame(const SceneSkeletonChannel* ch, const u16 tNorm16) {
#ifdef VOLO_SIMD
  const SimdVec tVec = simd_vec_broadcast_u16(tNorm16);
  for (u32 i = 0;; i += 8) {
    /**
     * NOTE: This can read past the end of the times array (as we always read 8 entries), but the
     * times array starts at a 16-byte boundary and the whole animData is padded to 16 bytes so we
     * can never read past the end of the data.
     */
    const SimdVec timesVec    = simd_vec_load(&ch->times[i]);
    const u32     greaterMask = simd_vec_mask_u8(simd_vec_greater_eq_u16(timesVec, tVec));
    if (greaterMask) {
      const u32 greaterIndex = i + intrinsic_ctz_32(greaterMask) / 2;
      return greaterIndex - 1;
    }
  }
#else
  u32 count = ch->frameCount;
  u32 begin = 0;
  while (count) {
    const u32 step   = count >> 1;
    const u32 middle = begin + step;
    if (ch->times[middle] < tNorm16) {
      begin = middle + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  return begin - 1;
#endif
}

static GeoVector anim_channel_get_vec(const SceneSkeletonChannel* ch, const u16 tNorm16) {
  if (tNorm16 <= ch->times[0]) {
    return ch->values_vec[0];
  }
  if (tNorm16 >= ch->times[ch->frameCount - 1]) {
    return ch->values_vec[ch->frameCount - 1];
  }
  const u32 frame   = anim_find_frame(ch, tNorm16);
  const u16 fromT16 = ch->times[frame];
  const u16 toT16   = ch->times[frame + 1];
  const f32 frac    = (f32)(tNorm16 - fromT16) / (f32)(toT16 - fromT16);
  return geo_vector_lerp(ch->values_vec[frame], ch->values_vec[frame + 1], frac);
}

static GeoQuat anim_channel_get_quat(const SceneSkeletonChannel* ch, const u16 tNorm16) {
  if (tNorm16 <= ch->times[0]) {
    return ch->values_quat[0];
  }
  if (tNorm16 >= ch->times[ch->frameCount - 1]) {
    return ch->values_quat[ch->frameCount - 1];
  }
  const u32 frame   = anim_find_frame(ch, tNorm16);
  const u16 fromT16 = ch->times[frame];
  const u16 toT16   = ch->times[frame + 1];
  const f32 frac    = (f32)(tNorm16 - fromT16) / (f32)(toT16 - fromT16);
  return geo_quat_slerp(ch->values_quat[frame], ch->values_quat[frame + 1], frac);
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
    const f32                     layerWeight,
    const f32                     layerTimeNorm,
    f32*                          weights,
    SceneJointPose*               out) {
  const u16                tNorm16 = (u16)(layerTimeNorm * u16_max);
  const SceneSkeletonAnim* anim    = &tl->anims[layerIndex];
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
      anim_blend_vec(anim_channel_get_vec(chT, tNorm16), layerWeight, weightT, &out[j].t);
    }
    if (chR->frameCount && *weightR < scene_weight_max) {
      anim_blend_quat(anim_channel_get_quat(chR, tNorm16), layerWeight, weightR, &out[j].r);
    }
    if (chS->frameCount && *weightS < scene_weight_max) {
      anim_blend_vec(anim_channel_get_vec(chS, tNorm16), layerWeight, weightS, &out[j].s);
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

static GeoBox anim_calc_bounds(const SceneSkeletonTemplComp* tl, const GeoMatrix* jointTransforms) {
  const GeoVector joint0Pos = geo_matrix_to_translation(&jointTransforms[0]);
  GeoBox          bounds    = geo_box_from_sphere(joint0Pos, tl->boundingRadius[0]);
  for (u32 i = 1; i != tl->jointCount; ++i) {
    const GeoVector jointPos    = geo_matrix_to_translation(&jointTransforms[i]);
    const GeoBox    jointBounds = geo_box_from_sphere(jointPos, tl->boundingRadius[i]);
    bounds                      = geo_box_encapsulate_box(&bounds, &jointBounds);
  }
  return bounds;
}

static f32 anim_compute_fade(const f32 timeNorm, const SceneAnimFlags flags) {
  const f32 tQuad    = timeNorm * 4.0f;
  f32       strength = 1.0f;
  if (flags & SceneAnimFlags_AutoFadeIn) {
    // Fade-in over the first 25%.
    strength = math_min(1.0f, tQuad);
  }
  if (flags & SceneAnimFlags_AutoFadeOut) {
    // Fade-out over the last 25%.
    strength -= math_max(0.0f, tQuad - 3.0f);
  }
  return strength;
}

static f32 anim_time_clamp(const f32 time, const f32 duration) {
  return math_clamp_f32(time, 0, duration);
}

static f32 anim_time_wrap(f32 time, const f32 duration) {
  if (time < 0) {
    do {
      time += duration;
    } while (time < 0);
    return time;
  }
  return math_mod_f32(time, duration);
}

ecs_view_define(UpdateView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_with(SceneSkeletonLoadedComp);
  ecs_access_write(SceneAnimationComp);
  ecs_access_write(SceneSkeletonComp);
  ecs_access_maybe_write(SceneBoundsComp);
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

    diag_assert(sk->jointCount); // Skeleton needs atleast one joint.

    ecs_view_jump(templItr, renderable->graphic);
    const SceneSkeletonTemplComp* tl = ecs_view_read_t(templItr, SceneSkeletonTemplComp);

    anim_set_weights_neg1(tl, weights);

    for (u32 i = 0; i != anim->layerCount; ++i) {
      SceneAnimLayer* layer = &anim->layers[i];
      if (LIKELY(layer->duration > scene_anim_duration_min)) {
        layer->time += deltaSeconds * layer->speed;
        if (layer->flags & SceneAnimFlags_Loop) {
          layer->time = anim_time_wrap(layer->time, layer->duration);
        } else {
          layer->time = anim_time_clamp(layer->time, layer->duration);
        }
        diag_assert(layer->time >= 0 && layer->time <= layer->duration);
      } else {
        layer->time = 0;
      }
      const f32 layerTimeNorm = layer->duration > 0 ? (layer->time / layer->duration) : 0.0f;
      f32       layerWeight   = layer->weight;
      if (layer->flags & SceneAnimFlags_AutoFade) {
        layerWeight *= anim_compute_fade(layerTimeNorm, layer->flags);
      }
      if (layerWeight > scene_weight_min) {
        anim_sample_layer(tl, layer, i, layerWeight, layerTimeNorm, weights, poses);
      }
    }
    anim_sample_def(tl, weights, poses);
    anim_apply(tl, poses, sk->jointTransforms);

    if (!sentinel_check(sk->postTransJointIdx)) {
      anim_mul_rec(tl, sk->postTransJointIdx, &sk->postTransMat, sk->jointTransforms);
    }

    SceneBoundsComp* bounds = ecs_view_write_t(itr, SceneBoundsComp);
    if (bounds) {
      bounds->local = anim_calc_bounds(tl, sk->jointTransforms);
    }
  }
}

ecs_view_define(DirtyTemplateView) {
  ecs_access_with(SceneSkeletonTemplComp);
  ecs_access_with(SceneSkeletonTemplLoadedComp);
  ecs_access_with(AssetChangedComp);
}

ecs_view_define(DirtyRenderableView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_with(SceneSkeletonLoadedComp);
}

ecs_system_define(SceneSkeletonClearDirtyTemplateSys) {
  EcsView* dirtyTemplateView   = ecs_world_view_t(world, DirtyTemplateView);
  EcsView* dirtyRenderableView = ecs_world_view_t(world, DirtyRenderableView);

  /**
   * Clear skeleton templates for changed graphic assets.
   */
  DynArray clearedTemplates = dynarray_create_t(g_allocScratch, EcsEntityId, 0);
  for (EcsIterator* itr = ecs_view_itr(dirtyTemplateView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, SceneSkeletonTemplComp);
    ecs_world_remove_t(world, entity, SceneSkeletonTemplLoadedComp);

    *dynarray_insert_sorted_t(&clearedTemplates, EcsEntityId, ecs_compare_entity, &entity) = entity;
  }

  /**
   * Remove skeletons for renderables where the template was cleared.
   */
  if (clearedTemplates.size) {
    for (EcsIterator* itr = ecs_view_itr(dirtyRenderableView); ecs_view_walk(itr);) {
      const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);

      if (dynarray_search_binary(&clearedTemplates, ecs_compare_entity, &renderable->graphic)) {
        ecs_world_remove_t(world, ecs_view_entity(itr), SceneSkeletonLoadedComp);
        ecs_utils_maybe_remove_t(world, ecs_view_entity(itr), SceneSkeletonComp);
        ecs_utils_maybe_remove_t(world, ecs_view_entity(itr), SceneAnimationComp);
      }
    }
  }
}

ecs_module_init(scene_skeleton_module) {
  ecs_register_comp(SceneSkeletonComp, .destructor = ecs_destruct_skeleton_comp);
  ecs_register_comp(SceneAnimationComp, .destructor = ecs_destruct_animation_comp);
  ecs_register_comp_empty(SceneSkeletonLoadedComp);
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
  ecs_register_view(DirtyRenderableView);

  ecs_register_system(
      SceneSkeletonInitSys, ecs_view_id(SkeletonInitView), ecs_view_id(SkeletonTemplView));

  ecs_register_system(SceneSkeletonTemplLoadSys, ecs_view_id(TemplLoadView), ecs_view_id(MeshView));

  ecs_register_system(
      SceneSkeletonUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(UpdateView),
      ecs_view_id(SkeletonTemplView));

  ecs_parallel(SceneSkeletonUpdateSys, g_jobsWorkerCount * 2);

  ecs_register_system(
      SceneSkeletonClearDirtyTemplateSys,
      ecs_view_id(DirtyTemplateView),
      ecs_view_id(DirtyRenderableView));
}

const SceneAnimLayer* scene_animation_layer(const SceneAnimationComp* a, const StringHash layer) {
  for (u32 i = 0; i != a->layerCount; ++i) {
    if (a->layers[i].nameHash == layer) {
      return &a->layers[i];
    }
  }
  return null;
}

SceneAnimLayer* scene_animation_layer_mut(SceneAnimationComp* a, const StringHash layer) {
  return (SceneAnimLayer*)scene_animation_layer(a, layer);
}

bool scene_animation_set_time(SceneAnimationComp* a, const StringHash layer, const f32 time) {
  SceneAnimLayer* state = scene_animation_layer_mut(a, layer);
  if (state) {
    state->time = time;
    return true;
  }
  return false;
}

bool scene_animation_set_weight(SceneAnimationComp* a, const StringHash layer, const f32 weight) {
  SceneAnimLayer* state = scene_animation_layer_mut(a, layer);
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

f32 scene_skeleton_joint_bounding_radius(const SceneSkeletonTemplComp* tl, const u32 joint) {
  diag_assert(joint < tl->jointCount);
  return tl->boundingRadius[joint];
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

  const f32 duration = tl->anims[layer].duration;
  const f32 tNorm    = duration > 0 ? (anim_time_clamp(time, duration) / duration) : 0.0f;
  const u16 tNorm16  = (u16)(tNorm * u16_max);

  return (SceneJointPose){
      .t = chT->frameCount ? anim_channel_get_vec(chT, tNorm16) : geo_vector(0),
      .r = chR->frameCount ? anim_channel_get_quat(chR, tNorm16) : geo_quat_ident,
      .s = chS->frameCount ? anim_channel_get_vec(chS, tNorm16) : geo_vector(1, 1, 1),
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
  geo_matrix_mul_batch(sk->jointTransforms, tl->bindMatInv, out, sk->jointCount);
}
