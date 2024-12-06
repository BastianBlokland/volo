#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the scene library.
 */

ecs_comp_extern(SceneActionQueueComp);
ecs_comp_extern(SceneAnimationComp);
ecs_comp_extern(SceneAttachmentComp);
ecs_comp_extern(SceneAttackComp);
ecs_comp_extern(SceneBarkComp);
ecs_comp_extern(SceneBoundsComp);
ecs_comp_extern(SceneCameraComp);
ecs_comp_extern(SceneCollisionComp);
ecs_comp_extern(SceneCollisionEnvComp);
ecs_comp_extern(SceneDeadComp);
ecs_comp_extern(SceneDebugComp);
ecs_comp_extern(SceneDebugEnvComp);
ecs_comp_extern(SceneFactionComp);
ecs_comp_extern(SceneFootstepComp);
ecs_comp_extern(SceneHealthComp);
ecs_comp_extern(SceneHealthRequestComp);
ecs_comp_extern(SceneHealthStatsComp);
ecs_comp_extern(SceneKnowledgeComp);
ecs_comp_extern(SceneLevelInstanceComp);
ecs_comp_extern(SceneLevelManagerComp);
ecs_comp_extern(SceneLifetimeDurationComp);
ecs_comp_extern(SceneLifetimeOwnerComp);
ecs_comp_extern(SceneLightAmbientComp);
ecs_comp_extern(SceneLightDirComp);
ecs_comp_extern(SceneLightPointComp);
ecs_comp_extern(SceneLocationComp);
ecs_comp_extern(SceneLocomotionComp);
ecs_comp_extern(SceneNameComp);
ecs_comp_extern(SceneNavAgentComp);
ecs_comp_extern(SceneNavBlockerComp);
ecs_comp_extern(SceneNavEnvComp);
ecs_comp_extern(SceneNavPathComp);
ecs_comp_extern(ScenePrefabEnvComp);
ecs_comp_extern(ScenePrefabInstanceComp);
ecs_comp_extern(SceneProductionComp);
ecs_comp_extern(SceneProjectileComp);
ecs_comp_extern(SceneRenderableComp);
ecs_comp_extern(SceneScaleComp);
ecs_comp_extern(SceneScriptComp);
ecs_comp_extern(SceneSetEnvComp);
ecs_comp_extern(SceneSetMemberComp);
ecs_comp_extern(SceneSkeletonComp);
ecs_comp_extern(SceneSkeletonLoadedComp);
ecs_comp_extern(SceneSkeletonTemplComp);
ecs_comp_extern(SceneSoundComp);
ecs_comp_extern(SceneSoundListenerComp);
ecs_comp_extern(SceneStatusComp);
ecs_comp_extern(SceneStatusRequestComp);
ecs_comp_extern(SceneTagComp);
ecs_comp_extern(SceneTargetFinderComp);
ecs_comp_extern(SceneTerrainComp);
ecs_comp_extern(SceneTimeComp);
ecs_comp_extern(SceneTransformComp);
ecs_comp_extern(SceneVelocityComp);
ecs_comp_extern(SceneVfxDecalComp);
ecs_comp_extern(SceneVfxSystemComp);
ecs_comp_extern(SceneVisibilityComp);
ecs_comp_extern(SceneVisibilityEnvComp);
ecs_comp_extern(SceneVisionComp);
ecs_comp_extern(SceneWeaponResourceComp);

typedef enum eSceneFaction     SceneFaction;
typedef enum eSceneLayer       SceneLayer;
typedef enum eSceneNavLayer    SceneNavLayer;
typedef enum eSceneStatusType  SceneStatusType;
typedef enum eSceneTags        SceneTags;
typedef struct sSceneAnimLayer SceneAnimLayer;
typedef struct sSceneJointPose SceneJointPose;
typedef struct sSceneTagFilter SceneTagFilter;
typedef u8                     SceneScriptSlot;
typedef u8                     SceneStatusMask;
