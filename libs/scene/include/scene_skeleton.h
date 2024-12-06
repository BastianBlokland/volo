#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"
#include "scene.h"

#define scene_skeleton_joints_max 75

ecs_comp_extern(SceneSkeletonTemplComp);  // Skeleton template, present on graphic entities.
ecs_comp_extern(SceneSkeletonLoadedComp); // Indicates that the skeleton was loaded (if applicable).

ecs_comp_extern_public(SceneSkeletonComp) {
  GeoMatrix* jointTransforms;
  u32        jointCount;

  // Optional transformation to apply post animation sampling.
  u32       postTransJointIdx;
  GeoMatrix postTransMat;
};

typedef enum {
  SceneAnimFlags_None        = 0,
  SceneAnimFlags_Active      = 1 << 0,
  SceneAnimFlags_Loop        = 1 << 1,
  SceneAnimFlags_AutoFadeIn  = 1 << 2, // Automatically set the weight to fade the anim in.
  SceneAnimFlags_AutoFadeOut = 1 << 3, // Automatically set the weight to fade the anim out.
  SceneAnimFlags_AutoFade    = SceneAnimFlags_AutoFadeOut | SceneAnimFlags_AutoFadeIn,
} SceneAnimFlags;

typedef struct sSceneAnimLayer {
  f32            time; // Not normalized.
  f32            duration;
  f32            speed;
  f32            weight;
  StringHash     nameHash;
  SceneAnimFlags flags : 8;
} SceneAnimLayer;

typedef struct sSceneJointPose {
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
f32        scene_skeleton_joint_bounding_radius(const SceneSkeletonTemplComp*, u32 joint);

GeoMatrix scene_skeleton_joint_world(
    const SceneTransformComp*, const SceneScaleComp*, const SceneSkeletonComp*, u32 joint);

u32            scene_skeleton_joint_by_name(const SceneSkeletonTemplComp*, StringHash name);
SceneJointInfo scene_skeleton_info(const SceneSkeletonTemplComp*, u32 layer, u32 joint);
f32            scene_skeleton_mask(const SceneSkeletonTemplComp*, u32 layer, u32 joint);
SceneJointPose scene_skeleton_sample(const SceneSkeletonTemplComp*, u32 layer, u32 joint, f32 time);
SceneJointPose scene_skeleton_sample_def(const SceneSkeletonTemplComp*, u32 joint);
SceneJointPose scene_skeleton_root(const SceneSkeletonTemplComp*);

void scene_skeleton_delta(
    const SceneSkeletonComp*, const SceneSkeletonTemplComp*, GeoMatrix* restrict out);
