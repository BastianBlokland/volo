#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"

// Forward declare from 'scene_transform.h'
ecs_comp_extern_public(SceneTransformComp);
ecs_comp_extern_public(SceneScaleComp);

#define scene_skeleton_joints_max 75

typedef struct {
  u8 jointBits[scene_skeleton_joints_max / 8 + 1];
} SceneSkeletonMask;

ecs_comp_extern(SceneSkeletonTemplComp);

ecs_comp_extern_public(SceneSkeletonComp) {
  GeoMatrix* jointTransforms;
  u32        jointCount;

  // Optional transformation to apply post animation sampling.
  u32       postTransJointIdx;
  GeoMatrix postTransMat;
};

typedef enum {
  SceneAnimFlags_None        = 0,
  SceneAnimFlags_Loop        = 1 << 0,
  SceneAnimFlags_AutoFadeIn  = 1 << 1, // Automatically set the weight to fade the anim in.
  SceneAnimFlags_AutoFadeOut = 1 << 2, // Automatically set the weight to fade the anim out.
  SceneAnimFlags_AutoFade    = SceneAnimFlags_AutoFadeOut | SceneAnimFlags_AutoFadeIn,
} SceneAnimFlags;

typedef struct {
  f32               time;
  f32               duration;
  f32               speed;
  f32               weight;
  StringHash        nameHash;
  SceneAnimFlags    flags : 8;
  SceneSkeletonMask mask;
} SceneAnimLayer;

typedef struct {
  GeoVector t;
  GeoQuat   r;
  GeoVector s;
} SceneJointPose;

typedef struct {
  u32 frameCountT, frameCountR, frameCountS;
} SceneJointInfo;

ecs_comp_extern_public(SceneAnimationComp) {
  SceneAnimLayer* layers;
  u32             layerCount;
};

const SceneAnimLayer* scene_animation_layer(const SceneAnimationComp*, StringHash layer);
SceneAnimLayer*       scene_animation_layer_mut(SceneAnimationComp*, StringHash layer);
bool                  scene_animation_set_time(SceneAnimationComp*, StringHash layer, f32 time);
bool                  scene_animation_set_weight(SceneAnimationComp*, StringHash layer, f32 weight);

/**
 * Transformation to apply to the given joint post animation sampling.
 * NOTE: Only a single post transform is supported at this time.
 */
void scene_skeleton_post_transform(SceneSkeletonComp*, u32 joint, const GeoMatrix*);

u32        scene_skeleton_joint_count(const SceneSkeletonTemplComp*);
StringHash scene_skeleton_joint_name(const SceneSkeletonTemplComp*, u32 joint);
u32        scene_skeleton_joint_parent(const SceneSkeletonTemplComp*, u32 joint);
u32        scene_skeleton_joint_skin_count(const SceneSkeletonTemplComp*, u32 joint);

GeoMatrix scene_skeleton_joint_world(
    const SceneTransformComp*, const SceneScaleComp*, const SceneSkeletonComp*, u32 joint);

u32            scene_skeleton_joint_by_name(const SceneSkeletonTemplComp*, StringHash name);
SceneJointInfo scene_skeleton_info(const SceneSkeletonTemplComp*, u32 layer, u32 joint);
SceneJointPose scene_skeleton_sample(const SceneSkeletonTemplComp*, u32 layer, u32 joint, f32 time);
SceneJointPose scene_skeleton_sample_def(const SceneSkeletonTemplComp*, u32 joint);
SceneJointPose scene_skeleton_root(const SceneSkeletonTemplComp*);

void scene_skeleton_mask_set(SceneSkeletonMask*, u32 joint);
void scene_skeleton_mask_set_rec(SceneSkeletonMask*, const SceneSkeletonTemplComp*, u32 joint);
void scene_skeleton_mask_clear(SceneSkeletonMask*, u32 joint);
void scene_skeleton_mask_clear_rec(SceneSkeletonMask*, const SceneSkeletonTemplComp*, u32 joint);
bool scene_skeleton_mask_test(const SceneSkeletonMask*, u32 joint);

void scene_skeleton_delta(
    const SceneSkeletonComp*, const SceneSkeletonTemplComp*, GeoMatrix* restrict out);
