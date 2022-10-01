#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"

#define scene_skeleton_joints_max 75

typedef struct {
  u8 jointBits[scene_skeleton_joints_max / 8 + 1];
} SceneSkeletonMask;

ecs_comp_extern(SceneSkeletonTemplComp);

ecs_comp_extern_public(SceneSkeletonComp) {
  u32        jointCount;
  GeoMatrix* jointTransforms;
};

typedef enum {
  SceneAnimFlags_None           = 0,
  SceneAnimFlags_Loop           = 1 << 0,
  SceneAnimFlags_AutoWeightFade = 1 << 1, // Automatically set the weight to fade the animation.
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

SceneAnimLayer* scene_animation_layer(SceneAnimationComp*, StringHash layer);
bool            scene_animation_set_time(SceneAnimationComp*, StringHash layer, f32 time);
bool            scene_animation_set_weight(SceneAnimationComp*, StringHash layer, f32 weight);

u32        scene_skeleton_joint_count(const SceneSkeletonTemplComp*);
StringHash scene_skeleton_joint_name(const SceneSkeletonTemplComp*, u32 jointIndex);
u32        scene_skeleton_joint_parent(const SceneSkeletonTemplComp*, u32 jointIndex);
u32        scene_skeleton_joint_skin_count(const SceneSkeletonTemplComp*, u32 jointIndex);

u32            scene_skeleton_joint_by_name(const SceneSkeletonTemplComp*, StringHash name);
SceneJointInfo scene_skeleton_info(const SceneSkeletonTemplComp*, u32 layer, u32 joint);
SceneJointPose scene_skeleton_sample(const SceneSkeletonTemplComp*, u32 layer, u32 joint, f32 time);
SceneJointPose scene_skeleton_sample_def(const SceneSkeletonTemplComp*, u32 joint);
SceneJointPose scene_skeleton_root(const SceneSkeletonTemplComp*);

void scene_skeleton_mask_set(SceneSkeletonMask*, u32 joint);
void scene_skeleton_mask_set_all(SceneSkeletonMask*);
void scene_skeleton_mask_clear(SceneSkeletonMask*, u32 joint);
void scene_skeleton_mask_clear_all(SceneSkeletonMask*);
void scene_skeleton_mask_flip(SceneSkeletonMask*, u32 joint);
bool scene_skeleton_mask_test(const SceneSkeletonMask*, u32 joint);

void scene_skeleton_delta(
    const SceneSkeletonComp*, const SceneSkeletonTemplComp*, GeoMatrix* restrict out);
