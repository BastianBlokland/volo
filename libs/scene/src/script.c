#include "asset_manager.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_thread.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "geo_sphere.h"
#include "log_logger.h"
#include "scene_action.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_debug.h"
#include "scene_health.h"
#include "scene_knowledge.h"
#include "scene_lifetime.h"
#include "scene_light.h"
#include "scene_location.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_prefab.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_script.h"
#include "scene_set.h"
#include "scene_skeleton.h"
#include "scene_sound.h"
#include "scene_status.h"
#include "scene_tag.h"
#include "scene_target.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_visibility.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_enum.h"

#define scene_script_max_asset_loads 8
#define scene_script_line_of_sight_min 1.0f
#define scene_script_line_of_sight_max 100.0f
#define scene_script_query_values_max 512
#define scene_script_query_max 10

typedef enum {
  SceneScriptCapability_NavTravel,
  SceneScriptCapability_Animation,
  SceneScriptCapability_Attack,
  SceneScriptCapability_Status,
  SceneScriptCapability_Teleport,
  SceneScriptCapability_Bark,
  SceneScriptCapability_Renderable,
  SceneScriptCapability_Vfx,
  SceneScriptCapability_Light,
  SceneScriptCapability_Sound,

  SceneScriptCapability_Count
} SceneScriptCapability;

static const String g_sceneScriptCapabilityNames[] = {
    string_static("NavTravel"),
    string_static("Animation"),
    string_static("Attack"),
    string_static("Status"),
    string_static("Teleport"),
    string_static("Bark"),
    string_static("Renderable"),
    string_static("Vfx"),
    string_static("Light"),
    string_static("Sound"),
};
ASSERT(array_elems(g_sceneScriptCapabilityNames) == SceneScriptCapability_Count, "Missing name");

// clang-format off

static ScriptEnum g_scriptEnumFaction,
                  g_scriptEnumClock,
                  g_scriptEnumNavLayer,
                  g_scriptEnumNavFind,
                  g_scriptEnumCapability,
                  g_scriptEnumActivity,
                  g_scriptEnumTargetExclude,
                  g_scriptEnumRenderableParam,
                  g_scriptEnumVfxParam,
                  g_scriptEnumLightParam,
                  g_scriptEnumSoundParam,
                  g_scriptEnumAnimParam,
                  g_scriptEnumLayer,
                  g_scriptEnumQueryOption,
                  g_scriptEnumStatus,
                  g_scriptEnumBark,
                  g_scriptEnumHealthStat;

// clang-format on

static void eval_enum_init_faction(void) {
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionA"), SceneFaction_A);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionB"), SceneFaction_B);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionC"), SceneFaction_C);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionD"), SceneFaction_D);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionNone"), SceneFaction_None);
}

static void eval_enum_init_clock(void) {
  script_enum_push(&g_scriptEnumClock, string_lit("Time"), 0);
  script_enum_push(&g_scriptEnumClock, string_lit("RealTime"), 1);
  script_enum_push(&g_scriptEnumClock, string_lit("Delta"), 2);
  script_enum_push(&g_scriptEnumClock, string_lit("RealDelta"), 3);
  script_enum_push(&g_scriptEnumClock, string_lit("Ticks"), 4);
}

static void eval_enum_init_nav_layer(void) {
  for (SceneNavLayer layer = 0; layer != SceneNavLayer_Count; ++layer) {
    script_enum_push(&g_scriptEnumNavLayer, g_sceneNavLayerNames[layer], layer);
  }
}

static void eval_enum_init_nav_find(void) {
  script_enum_push(&g_scriptEnumNavFind, string_lit("ClosestCell"), 0);
  script_enum_push(&g_scriptEnumNavFind, string_lit("UnblockedCell"), 1);
  script_enum_push(&g_scriptEnumNavFind, string_lit("FreeCell"), 2);
}

static void eval_enum_init_capability(void) {
  for (SceneScriptCapability cap = 0; cap != SceneScriptCapability_Count; ++cap) {
    script_enum_push(&g_scriptEnumCapability, g_sceneScriptCapabilityNames[cap], cap);
  }
}

static void eval_enum_init_activity(void) {
  script_enum_push(&g_scriptEnumActivity, string_lit("Dead"), 0);
  script_enum_push(&g_scriptEnumActivity, string_lit("Moving"), 1);
  script_enum_push(&g_scriptEnumActivity, string_lit("Traveling"), 2);
  script_enum_push(&g_scriptEnumActivity, string_lit("Attacking"), 3);
  script_enum_push(&g_scriptEnumActivity, string_lit("Firing"), 4);
  script_enum_push(&g_scriptEnumActivity, string_lit("AttackReadying"), 5);
  script_enum_push(&g_scriptEnumActivity, string_lit("AttackAiming"), 6);
}

static void eval_enum_init_target_exclude(void) {
  script_enum_push(&g_scriptEnumTargetExclude, string_lit("Unreachable"), 0);
  script_enum_push(&g_scriptEnumTargetExclude, string_lit("Obscured"), 1);
}

static void eval_enum_init_renderable_param(void) {
#define PUSH_RENDERABLE_PARAM(_ENUM_, _NAME_)                                                      \
  script_enum_push((_ENUM_), string_lit(#_NAME_), SceneActionRenderableParam_##_NAME_);

  PUSH_RENDERABLE_PARAM(&g_scriptEnumRenderableParam, Color);
  PUSH_RENDERABLE_PARAM(&g_scriptEnumRenderableParam, Alpha);
  PUSH_RENDERABLE_PARAM(&g_scriptEnumRenderableParam, Emissive);

#undef PUSH_RENDERABLE_PARAM
}

static void eval_enum_init_vfx_param(void) {
#define PUSH_VFX_PARAM(_ENUM_, _NAME_)                                                             \
  script_enum_push((_ENUM_), string_lit(#_NAME_), SceneActionVfxParam_##_NAME_);

  PUSH_VFX_PARAM(&g_scriptEnumVfxParam, Alpha);
  PUSH_VFX_PARAM(&g_scriptEnumVfxParam, EmitMultiplier);

#undef PUSH_VFX_PARAM
}

static void eval_enum_init_light_param(void) {
#define PUSH_LIGHT_PARAM(_ENUM_, _NAME_)                                                           \
  script_enum_push((_ENUM_), string_lit(#_NAME_), SceneActionLightParam_##_NAME_);

  PUSH_LIGHT_PARAM(&g_scriptEnumLightParam, Radiance);

#undef PUSH_LIGHT_PARAM
}

static void eval_enum_init_sound_param(void) {
#define PUSH_SOUND_PARAM(_ENUM_, _NAME_)                                                           \
  script_enum_push((_ENUM_), string_lit(#_NAME_), SceneActionSoundParam_##_NAME_);

  PUSH_SOUND_PARAM(&g_scriptEnumSoundParam, Gain);
  PUSH_SOUND_PARAM(&g_scriptEnumSoundParam, Pitch);

#undef PUSH_SOUND_PARAM
}

static void eval_enum_init_anim_param(void) {
#define PUSH_ANIM_PARAM(_ENUM_, _NAME_)                                                            \
  script_enum_push((_ENUM_), string_lit(#_NAME_), SceneActionAnimParam_##_NAME_);

  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, Time);
  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, TimeNorm);
  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, Speed);
  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, Weight);
  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, Active);
  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, Loop);
  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, FadeIn);
  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, FadeOut);
  PUSH_ANIM_PARAM(&g_scriptEnumAnimParam, Duration);

#undef PUSH_ANIM_PARAM
}

static void eval_enum_init_layer(void) {
  // clang-format off
  script_enum_push(&g_scriptEnumLayer, string_lit("Environment"),       SceneLayer_Environment);
  script_enum_push(&g_scriptEnumLayer, string_lit("Destructible"),      SceneLayer_Destructible);
  script_enum_push(&g_scriptEnumLayer, string_lit("Infantry"),          SceneLayer_Infantry);
  script_enum_push(&g_scriptEnumLayer, string_lit("Vehicle"),           SceneLayer_Vehicle);
  script_enum_push(&g_scriptEnumLayer, string_lit("Structure"),         SceneLayer_Structure);
  script_enum_push(&g_scriptEnumLayer, string_lit("Unit"),              SceneLayer_Unit);
  script_enum_push(&g_scriptEnumLayer, string_lit("Debug"),             SceneLayer_Debug);
  script_enum_push(&g_scriptEnumLayer, string_lit("AllIncludingDebug"), SceneLayer_AllIncludingDebug);
  script_enum_push(&g_scriptEnumLayer, string_lit("AllNonDebug"),       SceneLayer_AllNonDebug);
  // clang-format on
}

static void eval_enum_init_query_option(void) {
  script_enum_push(&g_scriptEnumQueryOption, string_lit("FactionSelf"), 1);
  script_enum_push(&g_scriptEnumQueryOption, string_lit("FactionOther"), 2);
}

static void eval_enum_init_status(void) {
  for (SceneStatusType type = 0; type != SceneStatusType_Count; ++type) {
    script_enum_push(&g_scriptEnumStatus, scene_status_name(type), type);
  }
}

static void eval_enum_init_bark(void) {
  for (SceneBarkType bark = 0; bark != SceneBarkType_Count; ++bark) {
    script_enum_push(&g_scriptEnumBark, scene_bark_name(bark), bark);
  }
}

static void eval_enum_init_health_stat(void) {
  for (SceneHealthStat stat = 0; stat != SceneHealthStat_Count; ++stat) {
    script_enum_push(&g_scriptEnumHealthStat, scene_health_stat_name(stat), stat);
  }
}

ecs_view_define(EvalGlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneSetEnvComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneVisibilityEnvComp);
  ecs_access_maybe_read(SceneDebugEnvComp);
}

ecs_view_define(EvalAnimView) { ecs_access_read(SceneAnimationComp); }
ecs_view_define(EvalFactionView) { ecs_access_read(SceneFactionComp); }
ecs_view_define(EvalHealthStatsView) { ecs_access_read(SceneHealthStatsComp); }
ecs_view_define(EvalHealthView) { ecs_access_read(SceneHealthComp); }
ecs_view_define(EvalLightDirView) { ecs_access_read(SceneLightDirComp); }
ecs_view_define(EvalLightPointView) { ecs_access_read(SceneLightPointComp); }
ecs_view_define(EvalLocoView) { ecs_access_read(SceneLocomotionComp); }
ecs_view_define(EvalNameView) { ecs_access_read(SceneNameComp); }
ecs_view_define(EvalNavAgentView) { ecs_access_read(SceneNavAgentComp); }
ecs_view_define(EvalPrefabInstanceView) { ecs_access_read(ScenePrefabInstanceComp); }
ecs_view_define(EvalRenderableView) { ecs_access_read(SceneRenderableComp); }
ecs_view_define(EvalScaleView) { ecs_access_read(SceneScaleComp); }
ecs_view_define(EvalSoundView) { ecs_access_read(SceneSoundComp); }
ecs_view_define(EvalStatusView) { ecs_access_read(SceneStatusComp); }
ecs_view_define(EvalTransformView) { ecs_access_read(SceneTransformComp); }
ecs_view_define(EvalVelocityView) { ecs_access_read(SceneVelocityComp); }
ecs_view_define(EvalVfxDecalView) { ecs_access_read(SceneVfxDecalComp); }
ecs_view_define(EvalVfxSysView) { ecs_access_read(SceneVfxSystemComp); }
ecs_view_define(EvalVisionView) { ecs_access_read(SceneVisionComp); }

ecs_view_define(EvalAttackView) {
  ecs_access_read(SceneAttackComp);
  ecs_access_maybe_read(SceneAttackAimComp);
}

ecs_view_define(EvalTargetView) { ecs_access_read(SceneTargetFinderComp); }

ecs_view_define(EvalLineOfSightView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneLocationComp);
  ecs_access_maybe_read(SceneCollisionComp);
}

ecs_view_define(EvalSkeletonView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneSkeletonComp);
  ecs_access_read(SceneTransformComp);
}
ecs_view_define(EvalSkeletonTemplView) { ecs_access_read(SceneSkeletonTemplComp); }

typedef struct {
  EcsEntityId values[scene_script_query_values_max];
  u32         count, itr;
} EvalQuery;

typedef struct {
  EcsWorld*    world;
  EcsIterator* animItr;
  EcsIterator* attackItr;
  EcsIterator* factionItr;
  EcsIterator* globalItr;
  EcsIterator* healthItr;
  EcsIterator* healthStatsItr;
  EcsIterator* lightDirItr;
  EcsIterator* lightPointItr;
  EcsIterator* lineOfSightItr;
  EcsIterator* locoItr;
  EcsIterator* nameItr;
  EcsIterator* navAgentItr;
  EcsIterator* prefabInstanceItr;
  EcsIterator* renderableItr;
  EcsIterator* scaleItr;
  EcsIterator* skeletonItr;
  EcsIterator* skeletonTemplItr;
  EcsIterator* soundItr;
  EcsIterator* statusItr;
  EcsIterator* targetItr;
  EcsIterator* transformItr;
  EcsIterator* veloItr;
  EcsIterator* vfxDecalItr;
  EcsIterator* vfxSysItr;
  EcsIterator* visionItr;

  EcsEntityId           instigator;
  SceneFaction          instigatorFaction;
  SceneScriptSlot       slot;
  SceneScriptComp*      scriptInstance;
  SceneKnowledgeComp*   scriptKnowledge;
  const ScriptProgram*  scriptProgram;
  String                scriptId;
  SceneActionQueueComp* actions;
  SceneDebugComp*       debug;
  GeoRay                debugRay;

  EvalQuery* queries; // EvalQuery[scene_script_query_max]
  u32        usedQueries;
} EvalContext;

static bool
context_is_capable(EvalContext* ctx, const EcsEntityId e, const SceneScriptCapability cap) {
  if (!ecs_world_exists(ctx->world, e)) {
    return false;
  }
  switch (cap) {
  case SceneScriptCapability_NavTravel:
    return ecs_world_has_t(ctx->world, e, SceneNavAgentComp);
  case SceneScriptCapability_Animation:
    return ecs_world_has_t(ctx->world, e, SceneAnimationComp);
  case SceneScriptCapability_Attack:
    return ecs_world_has_t(ctx->world, e, SceneAttackComp);
  case SceneScriptCapability_Status:
    return ecs_world_has_t(ctx->world, e, SceneStatusComp);
  case SceneScriptCapability_Teleport:
    return ecs_world_has_t(ctx->world, e, SceneTransformComp);
  case SceneScriptCapability_Bark:
    return ecs_world_has_t(ctx->world, e, SceneBarkComp);
  case SceneScriptCapability_Renderable:
    return ecs_world_has_t(ctx->world, e, SceneRenderableComp);
  case SceneScriptCapability_Vfx:
    return ecs_world_has_t(ctx->world, e, SceneVfxSystemComp) ||
           ecs_world_has_t(ctx->world, e, SceneVfxDecalComp);
  case SceneScriptCapability_Light:
    return ecs_world_has_t(ctx->world, e, SceneLightDirComp) ||
           ecs_world_has_t(ctx->world, e, SceneLightPointComp);
  case SceneScriptCapability_Sound:
    return ecs_world_has_t(ctx->world, e, SceneSoundComp);
  case SceneScriptCapability_Count:
    break;
  }
  UNREACHABLE
}

static EvalQuery* context_query_alloc(EvalContext* ctx) {
  return ctx->usedQueries < scene_script_query_max ? &ctx->queries[ctx->usedQueries++] : null;
}

static u32 context_query_id(EvalContext* ctx, const EvalQuery* query) {
  return (u32)(query - ctx->queries);
}

static EvalQuery* context_query_get(EvalContext* ctx, const u32 id) {
  return id < ctx->usedQueries ? &ctx->queries[id] : null;
}

static EcsEntityId arg_asset(EvalContext* ctx, ScriptBinderCall* call, const u16 i) {
  const EcsEntityId e = script_arg_entity(call, i);
  if (UNLIKELY(!ecs_world_exists(ctx->world, e) || !ecs_world_has_t(ctx->world, e, AssetComp))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_ArgumentInvalid, .argIndex = i});
  }
  return e;
}

static SceneLayer arg_layer(ScriptBinderCall* call, const u16 i) {
  if (call->argCount <= i) {
    return SceneLayer_Environment;
  }
  return (SceneLayer)script_arg_enum(call, i, &g_scriptEnumLayer);
}

static SceneLayer arg_layer_mask(ScriptBinderCall* call, const u16 i) {
  if (call->argCount <= i) {
    return SceneLayer_AllNonDebug;
  }
  SceneLayer layerMask = 0;
  for (u8 argIndex = i; argIndex != call->argCount; ++argIndex) {
    layerMask |= (SceneLayer)script_arg_enum(call, argIndex, &g_scriptEnumLayer);
  }
  return layerMask;
}

static SceneQueryFilter
arg_query_filter(const SceneFaction factionSelf, ScriptBinderCall* call, const u16 i) {
  const u32  option    = script_arg_opt_enum(call, i, &g_scriptEnumQueryOption, 0);
  SceneLayer layerMask = arg_layer_mask(call, i + 1);
  switch (option) {
  case 1 /* FactionSelf */:
    layerMask &= scene_faction_layers(factionSelf);
    break;
  case 2 /* FactionOther */:
    layerMask &= ~scene_faction_layers(factionSelf);
    break;
  }
  return (SceneQueryFilter){.layerMask = layerMask};
}

static ScriptVal eval_self(EvalContext* ctx, ScriptBinderCall* call) {
  (void)call;
  return script_entity(ctx->instigator);
}

static ScriptVal eval_exists(EvalContext* ctx, ScriptBinderCall* call) {
  script_arg_check(call, 0, script_mask_entity | script_mask_null);
  const EcsEntityId e = script_get_entity(call->args[0], ecs_entity_invalid);
  return script_bool(e && ecs_world_exists(ctx->world, e));
}

static ScriptVal eval_position(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId  e   = script_arg_entity(call, 0);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->transformItr, e);
  return itr ? script_vec3(ecs_view_read_t(itr, SceneTransformComp)->position) : script_null();
}

static ScriptVal eval_velocity(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId  e   = script_arg_entity(call, 0);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->veloItr, e);
  return itr ? script_vec3(ecs_view_read_t(itr, SceneVelocityComp)->velocityAvg) : script_null();
}

static ScriptVal eval_rotation(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId  e   = script_arg_entity(call, 0);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->transformItr, e);
  return itr ? script_quat(ecs_view_read_t(itr, SceneTransformComp)->rotation) : script_null();
}

static ScriptVal eval_scale(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId  e   = script_arg_entity(call, 0);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->scaleItr, e);
  return itr ? script_num(ecs_view_read_t(itr, SceneScaleComp)->scale) : script_null();
}

static ScriptVal eval_name(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId  e   = script_arg_entity(call, 0);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->nameItr, e);
  return itr ? script_str(ecs_view_read_t(itr, SceneNameComp)->name) : script_null();
}

static ScriptVal eval_faction(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  if (call->argCount == 1) {
    if (ecs_view_maybe_jump(ctx->factionItr, e)) {
      const SceneFactionComp* factionComp = ecs_view_read_t(ctx->factionItr, SceneFactionComp);
      const StringHash factionName = script_enum_lookup_name(&g_scriptEnumFaction, factionComp->id);
      return factionName ? script_str(factionName) : script_null();
    }
    return script_null();
  }
  const SceneFaction faction = script_arg_enum(call, 1, &g_scriptEnumFaction);
  SceneAction*       act     = scene_action_push(ctx->actions, SceneActionType_UpdateFaction);
  act->updateFaction         = (SceneActionUpdateFaction){.entity = e, .faction = faction};
  return script_null();
}

static ScriptVal eval_health(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e          = script_arg_entity(call, 0);
  const bool        normalized = script_arg_opt_bool(call, 1, false);
  if (ecs_view_maybe_jump(ctx->healthItr, e)) {
    const SceneHealthComp* healthComp = ecs_view_read_t(ctx->healthItr, SceneHealthComp);
    if (normalized) {
      return script_num(healthComp->norm);
    }
    return script_num(scene_health_points(healthComp));
  }
  return script_null();
}

static ScriptVal eval_health_stat(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId     e    = script_arg_entity(call, 0);
  const SceneHealthStat stat = script_arg_enum(call, 1, &g_scriptEnumHealthStat);
  if (ecs_view_maybe_jump(ctx->healthStatsItr, e)) {
    return script_num(ecs_view_read_t(ctx->healthStatsItr, SceneHealthStatsComp)->values[stat]);
  }
  return script_null();
}

static ScriptVal eval_vision(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  if (ecs_view_maybe_jump(ctx->visionItr, e)) {
    const SceneVisionComp* visionComp = ecs_view_read_t(ctx->visionItr, SceneVisionComp);
    return script_num(visionComp->radius);
  }
  return script_null();
}

static ScriptVal eval_visible(EvalContext* ctx, ScriptBinderCall* call) {
  const GeoVector    pos        = script_arg_vec3(call, 0);
  const SceneFaction factionDef = ctx->instigatorFaction;
  const SceneFaction faction    = script_arg_opt_enum(call, 1, &g_scriptEnumFaction, factionDef);
  const SceneVisibilityEnvComp* env = ecs_view_read_t(ctx->globalItr, SceneVisibilityEnvComp);
  return script_bool(scene_visible_pos(env, faction, pos));
}

static ScriptVal eval_time(EvalContext* ctx, ScriptBinderCall* call) {
  const SceneTimeComp* time = ecs_view_read_t(ctx->globalItr, SceneTimeComp);
  if (!call->argCount) {
    return script_time(time->time);
  }
  switch (script_arg_enum(call, 0, &g_scriptEnumClock)) {
  case 0 /* Time */:
    return script_time(time->time);
  case 1 /* RealTime */:
    return script_time(time->realTime);
  case 2 /* Delta */:
    return script_time(time->delta);
  case 3 /* RealDelta */:
    return script_time(time->realDelta);
  case 4 /* Ticks */:
    return script_num(time->ticks);
  }
  return script_null();
}

static bool eval_set_allowed(EvalContext* ctx, const EcsEntityId e) {
  if (UNLIKELY(ecs_world_exists(ctx->world, e) && ecs_world_has_t(ctx->world, e, AssetComp))) {
    return false; // Adding assets to sets is not allowed (because set entries can be destroyed).
  }
  return true;
}

static ScriptVal eval_set(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  if (UNLIKELY(!eval_set_allowed(ctx, e))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_ArgumentInvalid, .argIndex = 0});
  }
  const StringHash set = script_arg_str(call, 1);
  if (UNLIKELY(!set)) {
    return script_null();
  }
  if (call->argCount == 2) {
    const SceneSetEnvComp* setEnv = ecs_view_read_t(ctx->globalItr, SceneSetEnvComp);
    return script_bool(scene_set_contains(setEnv, set, e));
  }
  const bool add = script_arg_bool(call, 2);

  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_UpdateSet);
  act->updateSet   = (SceneActionUpdateSet){.entity = e, .set = set, .add = add};

  return script_null();
}

static ScriptVal eval_query_set(EvalContext* ctx, ScriptBinderCall* call) {
  const SceneSetEnvComp* setEnv = ecs_view_read_t(ctx->globalItr, SceneSetEnvComp);

  const StringHash set = script_arg_str(call, 0);
  if (UNLIKELY(!set)) {
    return script_null();
  }

  EvalQuery* query = context_query_alloc(ctx);
  if (UNLIKELY(!query)) {
    script_panic_raise(call->panicHandler, (ScriptPanic){ScriptPanic_QueryLimitExceeded});
  }

  const EcsEntityId* begin = scene_set_begin(setEnv, set);
  const EcsEntityId* end   = scene_set_end(setEnv, set);

  query->count = math_min((u32)(end - begin), scene_query_max_hits);
  query->itr   = 0;

  mem_cpy(
      mem_create(query->values, sizeof(EcsEntityId) * scene_query_max_hits),
      mem_create(begin, sizeof(EcsEntityId) * query->count));

  return script_num(context_query_id(ctx, query));
}

static ScriptVal eval_query_sphere(EvalContext* ctx, ScriptBinderCall* call) {
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(ctx->globalItr, SceneCollisionEnvComp);

  const GeoVector        pos    = script_arg_vec3(call, 0);
  const f32              radius = (f32)script_arg_num_range(call, 1, 0.01, 100.0);
  const SceneQueryFilter filter = arg_query_filter(ctx->instigatorFaction, call, 2);

  EvalQuery* query = context_query_alloc(ctx);
  if (UNLIKELY(!query)) {
    script_panic_raise(call->panicHandler, (ScriptPanic){ScriptPanic_QueryLimitExceeded});
  }

  ASSERT(array_elems(query->values) >= scene_query_max_hits, "Maximum query count too small")

  const GeoSphere sphere = {.point = pos, .radius = radius};

  query->count = scene_query_sphere_all(colEnv, &sphere, &filter, query->values);
  query->itr   = 0;

  return script_num(context_query_id(ctx, query));
}

static ScriptVal eval_query_box(EvalContext* ctx, ScriptBinderCall* call) {
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(ctx->globalItr, SceneCollisionEnvComp);

  const GeoVector        pos    = script_arg_vec3(call, 0);
  const GeoVector        size   = script_arg_vec3(call, 1);
  const GeoQuat          rot    = script_arg_opt_quat(call, 2, geo_quat_ident);
  const SceneQueryFilter filter = arg_query_filter(ctx->instigatorFaction, call, 3);

  EvalQuery* query = context_query_alloc(ctx);
  if (UNLIKELY(!query)) {
    script_panic_raise(call->panicHandler, (ScriptPanic){ScriptPanic_QueryLimitExceeded});
  }

  ASSERT(array_elems(query->values) >= scene_query_max_hits, "Maximum query count too small")

  GeoBoxRotated boxRotated;
  boxRotated.box      = geo_box_from_center(pos, size);
  boxRotated.rotation = rot;

  query->count = scene_query_box_all(colEnv, &boxRotated, &filter, query->values);
  query->itr   = 0;

  return script_num(context_query_id(ctx, query));
}

static ScriptVal eval_query_pop(EvalContext* ctx, ScriptBinderCall* call) {
  const u32  queryId = (u32)script_arg_num(call, 0);
  EvalQuery* query   = context_query_get(ctx, queryId);
  if (UNLIKELY(!query)) {
    script_panic_raise(
        call->panicHandler,
        (ScriptPanic){ScriptPanic_QueryInvalid, .argIndex = 0, .contextInt = queryId});
  }
  if (query->itr == query->count) {
    return script_null();
  }
  return script_entity(query->values[query->itr++]);
}

static ScriptVal eval_query_random(EvalContext* ctx, ScriptBinderCall* call) {
  const u32  queryId = (u32)script_arg_num(call, 0);
  EvalQuery* query   = context_query_get(ctx, queryId);
  if (UNLIKELY(!query)) {
    script_panic_raise(
        call->panicHandler,
        (ScriptPanic){ScriptPanic_QueryInvalid, .argIndex = 0, .contextInt = queryId});
  }
  if (query->itr == query->count) {
    return script_null();
  }
  const u32 index = (u32)rng_sample_range(g_rng, query->itr, query->count);
  diag_assert(index < query->count);
  return script_entity(query->values[index]);
}

static ScriptVal eval_nav_find(EvalContext* ctx, ScriptBinderCall* call) {
  const GeoVector     pos = script_arg_vec3(call, 0);
  const SceneNavLayer layer =
      (SceneNavLayer)script_arg_opt_enum(call, 1, &g_scriptEnumNavLayer, SceneNavLayer_Normal);

  const SceneNavEnvComp* navEnv = ecs_view_read_t(ctx->globalItr, SceneNavEnvComp);
  const GeoNavGrid*      grid   = scene_nav_grid(navEnv, layer);
  const GeoNavCell       cell   = geo_nav_at_position(grid, pos);
  if (call->argCount < 3) {
    return script_vec3(geo_nav_position(grid, cell));
  }
  switch (script_arg_enum(call, 2, &g_scriptEnumNavFind)) {
  case 0 /* ClosestCell */:
    return script_vec3(geo_nav_position(grid, cell));
  case 1 /* UnblockedCell */: {
    const GeoNavCell unblockedCell = geo_nav_closest(grid, cell, GeoNavCond_Unblocked);
    return script_vec3(geo_nav_position(grid, unblockedCell));
  }
  case 2 /* FreeCell */: {
    const GeoNavCell freeCell = geo_nav_closest(grid, cell, GeoNavCond_Free);
    return script_vec3(geo_nav_position(grid, freeCell));
  }
  }
  return script_null();
}

static ScriptVal eval_nav_target(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId        e     = script_arg_entity(call, 0);
  const EcsIterator*       itr   = ecs_view_maybe_jump(ctx->navAgentItr, e);
  const SceneNavAgentComp* agent = itr ? ecs_view_read_t(itr, SceneNavAgentComp) : null;
  if (!agent) {
    return script_null();
  }
  return agent->targetEntity ? script_entity(agent->targetEntity) : script_vec3(agent->targetPos);
}

static GeoVector eval_aim_center(
    const SceneTransformComp* trans, const SceneScaleComp* scale, const SceneLocationComp* loc) {
  if (loc) {
    const GeoBoxRotated volume = scene_location(loc, trans, scale, SceneLocationType_AimTarget);
    return geo_box_center(&volume.box);
  }
  return trans->position;
}

static GeoVector eval_aim_closest(
    const SceneTransformComp* trans,
    const SceneScaleComp*     scale,
    const SceneLocationComp*  loc,
    const GeoVector           reference) {
  if (loc) {
    const GeoBoxRotated volume = scene_location(loc, trans, scale, SceneLocationType_AimTarget);
    return geo_box_rotated_closest_point(&volume, reference);
  }
  return trans->position;
}

typedef struct {
  EcsEntityId srcEntity, tgtEntity;
} EvalLineOfSightFilterCtx;

static bool eval_line_of_sight_filter(const void* ctx, const EcsEntityId entity, const u32 layer) {
  (void)layer;
  const EvalLineOfSightFilterCtx* losCtx = ctx;
  if (entity == losCtx->srcEntity) {
    return false; // Ignore collisions with the source.
  }
  static const SceneLayer g_layersToIgnore = SceneLayer_Infantry | SceneLayer_Vehicle;
  if (entity != losCtx->tgtEntity && (layer & g_layersToIgnore) != 0) {
    /**
     * Ignore collisions with other units, reason is that for friendly units the attacks pass
     * through them anyway and for hostile ones we are okay with hitting them.
     * NOTE: Structure units are an exception to this.
     */
    return false;
  }
  return true;
}

static ScriptVal eval_line_of_sight(EvalContext* ctx, ScriptBinderCall* call) {
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(ctx->globalItr, SceneCollisionEnvComp);

  const EcsEntityId srcEntity = script_arg_entity(call, 0);
  EcsIterator*      srcItr    = ecs_view_maybe_jump(ctx->lineOfSightItr, srcEntity);
  if (!srcItr) {
    return script_null(); // Source not valid.
  }
  const SceneTransformComp* srcTrans = ecs_view_read_t(srcItr, SceneTransformComp);
  const SceneScaleComp*     srcScale = ecs_view_read_t(srcItr, SceneScaleComp);
  const SceneLocationComp*  srcLoc   = ecs_view_read_t(srcItr, SceneLocationComp);

  /**
   * TODO: At the moment we are using the center of the aim-target volume as an estimation of the
   * line-of-sight source. This is obviously a very crude estimation, in the future we should
   * consider either sampling a joint or add a specific configurable entity location for this.
   */
  const GeoVector srcPos = eval_aim_center(srcTrans, srcScale, srcLoc);

  const EcsEntityId tgtEntity = script_arg_entity(call, 1);
  EcsIterator*      tgtItr    = ecs_view_maybe_jump(ctx->lineOfSightItr, tgtEntity);
  if (!tgtItr) {
    return script_null(); // Target not valid.
  }
  const SceneTransformComp* tgtTrans = ecs_view_read_t(tgtItr, SceneTransformComp);
  const SceneScaleComp*     tgtScale = ecs_view_read_t(tgtItr, SceneScaleComp);
  const SceneLocationComp*  tgtLoc   = ecs_view_read_t(tgtItr, SceneLocationComp);
  const SceneCollisionComp* tgtCol   = ecs_view_read_t(tgtItr, SceneCollisionComp);
  const GeoVector           tgtPos   = eval_aim_closest(tgtTrans, tgtScale, tgtLoc, srcPos);

  if (!tgtCol) {
    return script_null(); // Target does not have collision.
  }

  const GeoVector toTgt = geo_vector_sub(tgtPos, srcPos);
  const f32       dist  = geo_vector_mag(toTgt);
  if (dist < scene_script_line_of_sight_min) {
    return script_num(dist); // Close enough that we always have line-of-sight.
  }
  if (dist > scene_script_line_of_sight_max) {
    return script_null(); // Far enough that we never have line-of-sight.
  }

  const EvalLineOfSightFilterCtx filterCtx = {.srcEntity = srcEntity, .tgtEntity = tgtEntity};

  const SceneQueryFilter filter = {
      .layerMask = SceneLayer_Environment | SceneLayer_Structure | tgtCol->layer,
      .callback  = eval_line_of_sight_filter,
      .context   = &filterCtx,
  };

  const GeoRay ray    = {.point = srcPos, .dir = geo_vector_div(toTgt, dist)};
  const f32    radius = (f32)script_arg_opt_num_range(call, 2, 0.0, 10.0, 0.0);

  SceneRayHit hit;
  bool        hasHit;
  if (radius < f32_epsilon) {
    hasHit = scene_query_ray(colEnv, &ray, dist, &filter, &hit);
  } else {
    hasHit = scene_query_ray_fat(colEnv, &ray, radius, dist, &filter, &hit);
  }
  const bool hasLos = hasHit && hit.entity == tgtEntity;
  return hasLos ? script_num(hit.time) : script_null();
}

static ScriptVal eval_capable(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId           e   = script_arg_entity(call, 0);
  const SceneScriptCapability cap = script_arg_enum(call, 1, &g_scriptEnumCapability);
  return script_bool(context_is_capable(ctx, e, cap));
}

static ScriptVal eval_active(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  switch (script_arg_enum(call, 1, &g_scriptEnumActivity)) {
  case 0 /* Dead */: {
    bool dead = false;
    if (ecs_world_exists(ctx->world, e)) {
      dead = ecs_world_has_t(ctx->world, e, SceneDeadComp);
    }
    return script_bool(dead);
  }
  case 1 /* Moving */: {
    const EcsIterator*         itr  = ecs_view_maybe_jump(ctx->locoItr, e);
    const SceneLocomotionComp* loco = itr ? ecs_view_read_t(itr, SceneLocomotionComp) : null;
    return script_bool(loco && (loco->flags & SceneLocomotion_Moving) != 0);
  }
  case 2 /* Traveling */: {
    const EcsIterator*       itr   = ecs_view_maybe_jump(ctx->navAgentItr, e);
    const SceneNavAgentComp* agent = itr ? ecs_view_read_t(itr, SceneNavAgentComp) : null;
    return script_bool(agent && (agent->flags & SceneNavAgent_Traveling) != 0);
  }
  case 3 /* Attacking */: {
    const EcsIterator*     itr    = ecs_view_maybe_jump(ctx->attackItr, e);
    const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
    return script_bool(attack && ecs_entity_valid(attack->targetCurrent));
  }
  case 4 /* Firing */: {
    const EcsIterator*     itr    = ecs_view_maybe_jump(ctx->attackItr, e);
    const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
    return script_bool(attack && (attack->flags & SceneAttackFlags_Firing) != 0);
  }
  case 5 /* AttackReadying */: {
    const EcsIterator*     itr    = ecs_view_maybe_jump(ctx->attackItr, e);
    const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
    return script_bool(attack && (attack->flags & SceneAttackFlags_Readying) != 0);
  }
  case 6 /* AttackAiming */: {
    const EcsIterator*        itr       = ecs_view_maybe_jump(ctx->attackItr, e);
    const SceneAttackAimComp* attackAim = itr ? ecs_view_read_t(itr, SceneAttackAimComp) : null;
    return script_bool(attackAim && attackAim->isAiming);
  }
  }
  return script_null();
}

static ScriptVal eval_target_primary(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  if (ecs_view_maybe_jump(ctx->targetItr, e)) {
    const SceneTargetFinderComp* finder = ecs_view_read_t(ctx->targetItr, SceneTargetFinderComp);
    return script_entity_or_null(scene_target_primary(finder));
  }
  return script_null();
}

static ScriptVal eval_target_range_min(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  if (ecs_view_maybe_jump(ctx->targetItr, e)) {
    const SceneTargetFinderComp* finder = ecs_view_read_t(ctx->targetItr, SceneTargetFinderComp);
    return script_num(finder->rangeMin);
  }
  return script_null();
}

static ScriptVal eval_target_range_max(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  if (ecs_view_maybe_jump(ctx->targetItr, e)) {
    const SceneTargetFinderComp* finder = ecs_view_read_t(ctx->targetItr, SceneTargetFinderComp);
    return script_num(finder->rangeMax);
  }
  return script_null();
}

static ScriptVal eval_target_exclude(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  if (ecs_view_maybe_jump(ctx->targetItr, e)) {
    const SceneTargetFinderComp* finder = ecs_view_read_t(ctx->targetItr, SceneTargetFinderComp);
    switch (script_arg_enum(call, 1, &g_scriptEnumTargetExclude)) {
    case 0 /* Unreachable */:
      return script_bool((finder->config & SceneTargetConfig_ExcludeUnreachable) != 0);
    case 1 /* Obscured */:
      return script_bool((finder->config & SceneTargetConfig_ExcludeObscured) != 0);
    }
  }
  return script_null();
}

static ScriptVal eval_tell(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e     = script_arg_entity(call, 0);
  const StringHash  key   = script_arg_str(call, 1);
  const ScriptVal   value = script_arg_any(call, 2);

  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_Tell);
  act->tell        = (SceneActionTell){.entity = e, .memKey = key, .value = value};
  return script_null();
}

static ScriptVal eval_ask(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e      = script_arg_entity(call, 0);
  const EcsEntityId target = script_arg_entity(call, 1);
  const StringHash  key    = script_arg_str(call, 2);

  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_Ask);
  act->ask         = (SceneActionAsk){.entity = e, .target = target, .memKey = key};
  return script_null();
}

static ScriptVal eval_prefab_spawn(EvalContext* ctx, ScriptBinderCall* call) {
  const StringHash prefabId = script_arg_str(call, 0);

  const EcsEntityId result = ecs_world_entity_create(ctx->world);

  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_Spawn);

  act->spawn = (SceneActionSpawn){
      .entity   = result,
      .prefabId = prefabId,
      .position = script_arg_opt_vec3(call, 1, geo_vector(0)),
      .rotation = script_arg_opt_quat(call, 2, geo_quat_ident),
      .scale    = (f32)script_arg_opt_num_range(call, 3, 0.001, 1000.0, 1.0),
      .faction  = script_arg_opt_enum(call, 4, &g_scriptEnumFaction, SceneFaction_None),
  };

  return script_entity(result);
}

static ScriptVal eval_prefab_id(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId e = script_arg_entity(call, 0);
  if (ecs_view_maybe_jump(ctx->prefabInstanceItr, e)) {
    return script_str(ecs_view_read_t(ctx->prefabInstanceItr, ScenePrefabInstanceComp)->prefabId);
  }
  return script_null();
}

static bool eval_destroy_allowed(EvalContext* ctx, const EcsEntityId e) {
  if (UNLIKELY(ecs_world_exists(ctx->world, e) && ecs_world_has_t(ctx->world, e, AssetComp))) {
    return false; // Destroying assets is not allowed.
  }
  return true;
}

static ScriptVal eval_destroy(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  if (UNLIKELY(!eval_destroy_allowed(ctx, entity))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_ArgumentInvalid, .argIndex = 0});
  }
  if (ecs_world_exists(ctx->world, entity)) {
    ecs_world_entity_destroy(ctx->world, entity);
  }
  return script_null();
}

static ScriptVal eval_destroy_after(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  if (UNLIKELY(!eval_destroy_allowed(ctx, entity))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_ArgumentInvalid, .argIndex = 0});
  }

  script_arg_check(call, 1, script_mask_entity | script_mask_time);
  const EcsEntityId  owner = script_arg_maybe_entity(call, 1, 0);
  const TimeDuration delay = script_arg_maybe_time(call, 1, 0);

  if (ecs_world_exists(ctx->world, entity)) {
    if (owner) {
      ecs_world_add_t(ctx->world, entity, SceneLifetimeOwnerComp, .owners[0] = owner);
    } else {
      ecs_world_add_t(ctx->world, entity, SceneLifetimeDurationComp, .duration = delay);
    }
  }
  return script_null();
}

static ScriptVal eval_teleport(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_Teleport))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }
  const GeoVector pos = script_arg_opt_vec3(call, 1, geo_vector(0));
  const GeoQuat   rot = script_arg_opt_quat(call, 2, geo_quat_ident);

  const bool posValid = geo_vector_mag_sqr(pos) <= (1e5f * 1e5f);
  if (UNLIKELY(!posValid)) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_ArgumentInvalid, .argIndex = 1});
  }

  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_Teleport);
  act->teleport    = (SceneActionTeleport){.entity = entity, .position = pos, .rotation = rot};

  return script_null();
}

static ScriptVal eval_nav_travel(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  script_arg_check(call, 1, script_mask_entity | script_mask_vec3);

  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_NavTravel))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }
  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_NavTravel);
  act->navTravel   = (SceneActionNavTravel){
        .entity         = entity,
        .targetEntity   = script_arg_maybe_entity(call, 1, ecs_entity_invalid),
        .targetPosition = script_arg_maybe_vec3(call, 1, geo_vector(0)),
  };
  return script_null();
}

static ScriptVal eval_nav_stop(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_NavTravel))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }
  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_NavStop);
  act->navStop     = (SceneActionNavStop){.entity = entity};

  return script_null();
}

static bool eval_attach_allowed(EvalContext* ctx, const EcsEntityId e) {
  if (UNLIKELY(ecs_world_exists(ctx->world, e) && ecs_world_has_t(ctx->world, e, AssetComp))) {
    return false; // Attaching assets is not allowed.
  }
  return true;
}

static ScriptVal eval_attach(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  if (UNLIKELY(!eval_attach_allowed(ctx, entity))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_ArgumentInvalid, .argIndex = 0});
  }
  const EcsEntityId target = script_arg_entity(call, 1);

  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_Attach);

  act->attach = (SceneActionAttach){
      .entity    = entity,
      .target    = target,
      .jointName = script_arg_opt_str(call, 2, 0),
      .offset    = script_arg_opt_vec3(call, 3, geo_vector(0)),
  };

  return script_null();
}

static ScriptVal eval_detach(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);

  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_Detach);
  act->detach      = (SceneActionDetach){.entity = entity};
  return script_null();
}

static ScriptVal eval_damage(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  const f32         amount = (f32)script_arg_num_range(call, 1, 1.0, 10000.0);
  if (amount > f32_epsilon) {
    SceneAction* act = scene_action_push(ctx->actions, SceneActionType_HealthMod);
    act->healthMod   = (SceneActionHealthMod){
          .entity = entity,
          .amount = -amount /* negate for damage */,
    };
  }
  return script_null();
}

static ScriptVal eval_heal(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  const f32         amount = (f32)script_arg_num_range(call, 1, 1.0, 10000.0);
  if (amount > f32_epsilon) {
    SceneAction* act = scene_action_push(ctx->actions, SceneActionType_HealthMod);
    act->healthMod   = (SceneActionHealthMod){.entity = entity, .amount = amount};
  }
  return script_null();
}

static ScriptVal eval_attack(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);

  script_arg_check(call, 1, script_mask_entity | script_mask_null);
  const EcsEntityId target = script_arg_maybe_entity(call, 1, ecs_entity_invalid);

  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_Attack))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }
  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_Attack);
  act->attack      = (SceneActionAttack){.entity = entity, .target = target};
  return script_null();
}

static ScriptVal eval_attack_target(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId      entity = script_arg_entity(call, 0);
  const EcsIterator*     itr    = ecs_view_maybe_jump(ctx->attackItr, entity);
  const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
  return attack ? script_entity_or_null(attack->targetCurrent) : script_null();
}

static ScriptVal eval_attack_weapon(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId      entity = script_arg_entity(call, 0);
  const EcsIterator*     itr    = ecs_view_maybe_jump(ctx->attackItr, entity);
  const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
  return attack ? script_str_or_null(attack->weaponName) : script_null();
}

static ScriptVal eval_bark(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId   entity = script_arg_entity(call, 0);
  const SceneBarkType type   = (SceneBarkType)script_arg_enum(call, 1, &g_scriptEnumBark);
  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_Bark))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }
  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_Bark);
  act->bark        = (SceneActionBark){.entity = entity, .type = type};
  return script_null();
}

static bool eval_status_allowed(EvalContext* ctx, const EcsEntityId e) {
  if (UNLIKELY(ecs_world_exists(ctx->world, e) && ecs_world_has_t(ctx->world, e, AssetComp))) {
    return false; // Assets are not allowed to have status effects
  }
  return true;
}

static ScriptVal eval_status(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  if (UNLIKELY(!eval_status_allowed(ctx, entity))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_ArgumentInvalid, .argIndex = 0});
  }
  const SceneStatusType type = (SceneStatusType)script_arg_enum(call, 1, &g_scriptEnumStatus);
  if (call->argCount < 3) {
    if (ecs_view_maybe_jump(ctx->statusItr, entity)) {
      const SceneStatusComp* statusComp = ecs_view_read_t(ctx->statusItr, SceneStatusComp);
      return script_bool(scene_status_active(statusComp, type));
    }
    return script_null();
  }
  if (script_arg_bool(call, 2)) {
    scene_status_add(ctx->world, entity, type, ctx->instigator);
  } else {
    scene_status_remove(ctx->world, entity, type);
  }
  return script_null();
}

static ScriptVal eval_renderable_spawn(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId asset             = arg_asset(ctx, call, 0);
  const GeoVector   pos               = script_arg_vec3(call, 1);
  const GeoQuat     rot               = script_arg_opt_quat(call, 2, geo_quat_ident);
  const f32         scale             = (f32)script_arg_opt_num_range(call, 3, 0.0001, 10000, 1.0);
  const GeoColor    color             = script_arg_opt_color(call, 4, geo_color_white);
  const f32         emissive          = (f32)script_arg_opt_num_range(call, 5, 0.0, 1.0, 0.0);
  const bool        requireVisibility = script_arg_opt_bool(call, 6, false);

  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);
  ecs_world_add_t(ctx->world, result, SceneTransformComp, .position = pos, .rotation = rot);
  if (scale < 0.999f || scale > 1.001f) {
    ecs_world_add_t(ctx->world, result, SceneScaleComp, .scale = scale);
  }
  // NOTE: Tags are needed to make the selection outline work.
  ecs_world_add_t(ctx->world, result, SceneTagComp, .tags = SceneTags_Default);
  ecs_world_add_t(
      ctx->world,
      result,
      SceneRenderableComp,
      .graphic  = asset,
      .emissive = emissive,
      .color    = color);
  if (requireVisibility) {
    ecs_world_add_t(ctx->world, result, SceneVisibilityComp);
  }
  return script_entity(result);
}

static ScriptVal eval_renderable_param(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  const i32         param  = script_arg_enum(call, 1, &g_scriptEnumRenderableParam);
  if (call->argCount == 2) {
    if (ecs_view_maybe_jump(ctx->renderableItr, entity)) {
      const SceneRenderableComp* r = ecs_view_read_t(ctx->renderableItr, SceneRenderableComp);
      switch (param) {
      case 0 /* Color */:
        return script_color(r->color);
      case 1 /* Alpha */:
        return script_num(r->color.a);
      case 2 /* Emissive */:
        return script_num(r->emissive);
      }
    }
    return script_null();
  }

  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_Renderable))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }

  SceneActionUpdateRenderableParam update;
  update.entity = entity;
  update.param  = param;
  switch (param) {
  case SceneActionRenderableParam_Color:
    update.value_color = script_arg_color(call, 2);
    break;
  case SceneActionRenderableParam_Alpha:
  case SceneActionRenderableParam_Emissive:
    update.value_num = (f32)script_arg_num_range(call, 2, 0.0, 1.0);
    break;
  }

  SceneAction* act = scene_action_push(ctx->actions, SceneActionType_UpdateRenderableParam);
  act->updateRenderableParam = update;

  return script_null();
}

static ScriptVal eval_vfx_system_spawn(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId asset             = arg_asset(ctx, call, 0);
  const GeoVector   pos               = script_arg_vec3(call, 1);
  const GeoQuat     rot               = script_arg_quat(call, 2);
  const f32         alpha             = (f32)script_arg_opt_num_range(call, 3, 0.0, 1.0, 1.0);
  const f32         emitMultiplier    = (f32)script_arg_opt_num_range(call, 4, 0.0, 1.0, 1.0);
  const bool        requireVisibility = script_arg_opt_bool(call, 5, false);

  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, result, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(
      ctx->world,
      result,
      SceneVfxSystemComp,
      .asset          = asset,
      .alpha          = alpha,
      .emitMultiplier = emitMultiplier);
  if (requireVisibility) {
    ecs_world_add_t(ctx->world, result, SceneVisibilityComp);
  }
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);
  return script_entity(result);
}

static ScriptVal eval_vfx_decal_spawn(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId asset             = arg_asset(ctx, call, 0);
  const GeoVector   pos               = script_arg_vec3(call, 1);
  const GeoQuat     rot               = script_arg_quat(call, 2);
  const f32         alpha             = (f32)script_arg_opt_num_range(call, 3, 0.0, 100.0, 1.0);
  const bool        requireVisibility = script_arg_opt_bool(call, 4, false);

  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, result, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(ctx->world, result, SceneVfxDecalComp, .asset = asset, .alpha = alpha);
  if (requireVisibility) {
    ecs_world_add_t(ctx->world, result, SceneVisibilityComp);
  }
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);
  return script_entity(result);
}

static ScriptVal eval_vfx_param(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  const i32         param  = script_arg_enum(call, 1, &g_scriptEnumVfxParam);
  if (call->argCount == 2) {
    if (ecs_view_maybe_jump(ctx->vfxSysItr, entity)) {
      const SceneVfxSystemComp* vfxSysComp = ecs_view_read_t(ctx->vfxSysItr, SceneVfxSystemComp);
      switch (param) {
      case SceneActionVfxParam_Alpha:
        return script_num(vfxSysComp->alpha);
      case SceneActionVfxParam_EmitMultiplier:
        return script_num(vfxSysComp->emitMultiplier);
      }
    }
    if (ecs_view_maybe_jump(ctx->vfxDecalItr, entity)) {
      const SceneVfxDecalComp* vfxDecalComp = ecs_view_read_t(ctx->vfxDecalItr, SceneVfxDecalComp);
      switch (param) {
      case SceneActionVfxParam_Alpha:
        return script_num(vfxDecalComp->alpha);
      }
    }
    return script_null();
  }
  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_Vfx))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }

  SceneAction* act    = scene_action_push(ctx->actions, SceneActionType_UpdateVfxParam);
  act->updateVfxParam = (SceneActionUpdateVfxParam){
      .entity = entity,
      .param  = param,
      .value  = (f32)script_arg_num_range(call, 2, 0.0, 1.0),
  };

  return script_null();
}

static ScriptVal eval_collision_box_spawn(EvalContext* ctx, ScriptBinderCall* call) {
  const GeoVector  pos        = script_arg_vec3(call, 0);
  const GeoVector  size       = script_arg_vec3(call, 1);
  const GeoQuat    rot        = script_arg_opt_quat(call, 2, geo_quat_ident);
  const SceneLayer layer      = arg_layer(call, 3);
  const bool       navBlocker = script_arg_opt_bool(call, 4, false);

  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, result, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);

  const SceneCollisionShape shape = {
      .type = SceneCollisionType_Box,
      .box =
          {
              .box.min  = geo_vector_mul(size, -0.5f),
              .box.max  = geo_vector_mul(size, 0.5f),
              .rotation = geo_quat_ident,
          },
  };
  scene_collision_add(ctx->world, result, layer, &shape, 1 /* shapeCount */);

  if (navBlocker) {
    scene_nav_add_blocker(ctx->world, result, SceneNavBlockerMask_All);
  }

  return script_entity(result);
}

static ScriptVal eval_collision_sphere_spawn(EvalContext* ctx, ScriptBinderCall* call) {
  const GeoVector  pos        = script_arg_vec3(call, 0);
  const f32        radius     = (f32)script_arg_num(call, 1);
  const SceneLayer layer      = arg_layer(call, 2);
  const bool       navBlocker = script_arg_opt_bool(call, 3, false);

  const GeoQuat     rot    = geo_quat_ident;
  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, result, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);

  const SceneCollisionShape shape = {
      .type   = SceneCollisionType_Sphere,
      .sphere = {.radius = radius},
  };
  scene_collision_add(ctx->world, result, layer, &shape, 1 /* shapeCount */);

  if (navBlocker) {
    scene_nav_add_blocker(ctx->world, result, SceneNavBlockerMask_All);
  }

  return script_entity(result);
}

static ScriptVal eval_light_point_spawn(EvalContext* ctx, ScriptBinderCall* call) {
  const GeoVector pos      = script_arg_vec3(call, 0);
  const GeoQuat   rot      = geo_quat_ident;
  const GeoColor  radiance = script_arg_color(call, 1);
  const f32       radius   = (f32)script_arg_num_range(call, 2, 1e-3f, 1e+3f);

  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, result, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(ctx->world, result, SceneLightPointComp, .radiance = radiance, .radius = radius);
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);
  return script_entity(result);
}

static ScriptVal eval_light_param(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  const i32         param  = script_arg_enum(call, 1, &g_scriptEnumLightParam);
  if (call->argCount == 2) {
    if (ecs_view_maybe_jump(ctx->lightPointItr, entity)) {
      const SceneLightPointComp* point = ecs_view_read_t(ctx->lightPointItr, SceneLightPointComp);
      switch (param) {
      case SceneActionLightParam_Radiance:
        return script_color(point->radiance);
      }
    }
    if (ecs_view_maybe_jump(ctx->lightDirItr, entity)) {
      const SceneLightDirComp* dir = ecs_view_read_t(ctx->lightDirItr, SceneLightDirComp);
      switch (param) {
      case SceneActionLightParam_Radiance:
        return script_color(dir->radiance);
      }
    }
    return script_null();
  }
  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_Light))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }

  SceneAction* act      = scene_action_push(ctx->actions, SceneActionType_UpdateLightParam);
  act->updateLightParam = (SceneActionUpdateLightParam){
      .entity = entity,
      .param  = param,
      .value  = script_arg_color(call, 2),
  };

  return script_null();
}

static ScriptVal eval_sound_spawn(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId asset = arg_asset(ctx, call, 0);
  GeoVector         pos;
  const bool        is3d = script_arg_has(call, 1);
  if (is3d) {
    pos = script_arg_vec3(call, 1);
  }
  const f32  gain              = (f32)script_arg_opt_num_range(call, 2, 0.0, 10.0, 1.0);
  const f32  pitch             = (f32)script_arg_opt_num_range(call, 3, 0.0, 10.0, 1.0);
  const bool looping           = script_arg_opt_bool(call, 4, false);
  const bool requireVisibility = is3d && script_arg_opt_bool(call, 5, false);

  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);
  if (is3d) {
    ecs_world_add_t(
        ctx->world, result, SceneTransformComp, .position = pos, .rotation = geo_quat_ident);
  }
  ecs_world_add_t(
      ctx->world,
      result,
      SceneSoundComp,
      .asset   = asset,
      .gain    = gain,
      .pitch   = pitch,
      .looping = looping);
  if (requireVisibility) {
    ecs_world_add_t(ctx->world, result, SceneVisibilityComp);
  }
  return script_entity(result);
}

static ScriptVal eval_sound_param(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity = script_arg_entity(call, 0);
  const i32         param  = script_arg_enum(call, 1, &g_scriptEnumSoundParam);
  if (call->argCount == 2) {
    if (ecs_view_maybe_jump(ctx->soundItr, entity)) {
      const SceneSoundComp* soundComp = ecs_view_read_t(ctx->soundItr, SceneSoundComp);
      switch (param) {
      case SceneActionSoundParam_Gain:
        return script_num(soundComp->gain);
      case SceneActionSoundParam_Pitch:
        return script_num(soundComp->pitch);
      }
    }
    return script_null();
  }
  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_Sound))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }
  SceneAction* act      = scene_action_push(ctx->actions, SceneActionType_UpdateSoundParam);
  act->updateSoundParam = (SceneActionUpdateSoundParam){
      .entity = entity,
      .param  = param,
      .value  = (f32)script_arg_num_range(call, 2, 0.0, 10.0),
  };
  return script_null();
}

static ScriptVal eval_anim_param(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity    = script_arg_entity(call, 0);
  const StringHash  layerName = script_arg_str(call, 1);
  const i32         param     = script_arg_enum(call, 2, &g_scriptEnumAnimParam);

  if (call->argCount == 3) {
    if (ecs_view_maybe_jump(ctx->animItr, entity)) {
      const SceneAnimationComp* animComp = ecs_view_read_t(ctx->animItr, SceneAnimationComp);
      const SceneAnimLayer*     layer    = scene_animation_layer(animComp, layerName);
      if (layer) {
        switch (param) {
        case SceneActionAnimParam_Time:
          return script_num(layer->time);
        case SceneActionAnimParam_TimeNorm:
          return script_num(layer->duration > 0 ? (layer->time / layer->duration) : 0.0f);
        case SceneActionAnimParam_Speed:
          return script_num(layer->speed);
        case SceneActionAnimParam_Weight:
          return script_num(layer->weight);
        case SceneActionAnimParam_Active:
          return script_bool((layer->flags & SceneAnimFlags_Active) != 0);
        case SceneActionAnimParam_Loop:
          return script_bool((layer->flags & SceneAnimFlags_Loop) != 0);
        case SceneActionAnimParam_FadeIn:
          return script_bool((layer->flags & SceneAnimFlags_AutoFadeIn) != 0);
        case SceneActionAnimParam_FadeOut:
          return script_bool((layer->flags & SceneAnimFlags_AutoFadeOut) != 0);
        case SceneActionAnimParam_Duration:
          return script_num(layer->duration);
        }
      }
    }
    return script_null();
  }

  if (UNLIKELY(!context_is_capable(ctx, entity, SceneScriptCapability_Animation))) {
    script_panic_raise(
        call->panicHandler, (ScriptPanic){ScriptPanic_MissingCapability, .argIndex = 0});
  }

  SceneActionUpdateAnimParam update;
  update.entity    = entity;
  update.layerName = layerName;
  update.param     = param;
  switch (param) {
  case SceneActionAnimParam_Time:
    update.value_f32 = (f32)script_arg_num_range(call, 3, 0.0, 1000.0);
    break;
  case SceneActionAnimParam_TimeNorm:
    update.value_f32 = (f32)script_arg_num_range(call, 3, 0.0, 1.0);
    break;
  case SceneActionAnimParam_Speed:
    update.value_f32 = (f32)script_arg_num_range(call, 3, -1000.0, 1000.0);
    break;
  case SceneActionAnimParam_Weight:
    update.value_f32 = (f32)script_arg_num_range(call, 3, 0.0, 1.0);
    break;
  case SceneActionAnimParam_Active:
  case SceneActionAnimParam_Loop:
  case SceneActionAnimParam_FadeIn:
  case SceneActionAnimParam_FadeOut:
    update.value_bool = script_arg_bool(call, 3);
    break;
  case SceneActionAnimParam_Duration:
    script_panic_raise(call->panicHandler, (ScriptPanic){ScriptPanic_ReadonlyParam, .argIndex = 3});
  }
  SceneAction* act     = scene_action_push(ctx->actions, SceneActionType_UpdateAnimParam);
  act->updateAnimParam = update;

  return script_null();
}

static ScriptVal eval_joint_position(EvalContext* ctx, ScriptBinderCall* call) {
  const EcsEntityId entity    = script_arg_entity(call, 0);
  const StringHash  jointName = script_arg_str(call, 1);

  if (!ecs_view_maybe_jump(ctx->skeletonItr, entity)) {
    return script_null(); // Entity does not have a skeleton.
  }
  const SceneRenderableComp* renderable = ecs_view_read_t(ctx->skeletonItr, SceneRenderableComp);
  const SceneScaleComp*      scale      = ecs_view_read_t(ctx->skeletonItr, SceneScaleComp);
  const SceneSkeletonComp*   skeleton   = ecs_view_read_t(ctx->skeletonItr, SceneSkeletonComp);
  const SceneTransformComp*  trans      = ecs_view_read_t(ctx->skeletonItr, SceneTransformComp);

  /**
   * Lookup the joint-index by name from the skeleton template.
   * NOTE: In the future we could consider making an api that takes join-indices directly as that is
   * considerably cheaper.
   */
  if (!ecs_view_maybe_jump(ctx->skeletonTemplItr, renderable->graphic)) {
    return script_null(); // Graphic does not have a skeleton template.
  }
  const SceneSkeletonTemplComp* template =
      ecs_view_read_t(ctx->skeletonTemplItr, SceneSkeletonTemplComp);

  const u32 jointIndex = scene_skeleton_joint_by_name(template, jointName);
  if (sentinel_check(jointIndex)) {
    return script_null(); // Skeleton does not have joint with the given name.
  }

  const GeoMatrix jointMat = scene_skeleton_joint_world(trans, scale, skeleton, jointIndex);
  const GeoVector jointPos = geo_matrix_to_translation(&jointMat);

  return script_vec3(jointPos);
}

static ScriptVal eval_random_of(EvalContext* ctx, ScriptBinderCall* call) {
  (void)ctx;
  /**
   * Return a random (non-null) value from the given arguments.
   * NOTE: This function arguably belongs in the language itself and not as an externally bound
   * function, unfortunately the language does not support intrinsics with variable argument counts
   * so its easier for it to live here for the time being.
   */
  ScriptVal choices[10];
  u32       choiceCount = 0;

  if (UNLIKELY(call->argCount > array_elems(choices))) {
    script_panic_raise(call->panicHandler, (ScriptPanic){ScriptPanic_ArgumentCountExceedsMaximum});
  }

  for (u16 i = 0; i != call->argCount; ++i) {
    if (script_non_null(call->args[i])) {
      choices[choiceCount++] = call->args[i];
    }
  }
  return choiceCount ? choices[(u32)(choiceCount * rng_sample_f32(g_rng))] : script_null();
}

static ScriptVal eval_debug_log(EvalContext* ctx, ScriptBinderCall* call) {
  DynString buffer = dynstring_create_over(alloc_alloc(g_allocScratch, usize_kibibyte, 1));
  for (u16 i = 0; i != call->argCount; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_write(call->args[i], &buffer);
  }

  const ScriptRangeLineCol scriptRange    = script_prog_location(ctx->scriptProgram, call->callId);
  const String             scriptRangeStr = fmt_write_scratch(
      "{}:{}-{}:{}",
      fmt_int(scriptRange.start.line + 1),
      fmt_int(scriptRange.start.column + 1),
      fmt_int(scriptRange.end.line + 1),
      fmt_int(scriptRange.end.column + 1));

  log_i(
      "script: {}",
      log_param("text", fmt_text(dynstring_view(&buffer))),
      log_param("entity", ecs_entity_fmt(ctx->instigator)),
      log_param("script", fmt_text(ctx->scriptId)),
      log_param("script-range", fmt_text(scriptRangeStr)));

  return script_null();
}

static SceneDebugSource eval_debug_source(EvalContext* ctx, ScriptBinderCall* call) {
  return (SceneDebugSource){
      .scriptSlot = ctx->slot,
      .scriptPos  = script_prog_location(ctx->scriptProgram, call->callId),
  };
}

static ScriptVal eval_debug_line(EvalContext* ctx, ScriptBinderCall* call) {
  SceneDebugLine data;
  data.start = script_arg_vec3(call, 0);
  data.end   = script_arg_vec3(call, 1);
  data.color = script_arg_opt_color(call, 2, geo_color_white);

  if (ctx->debug) {
    scene_debug_line(ctx->debug, data, eval_debug_source(ctx, call));
  }
  return script_null();
}

static ScriptVal eval_debug_sphere(EvalContext* ctx, ScriptBinderCall* call) {
  SceneDebugSphere data;
  data.pos    = script_arg_vec3(call, 0);
  data.radius = (f32)script_arg_opt_num_range(call, 1, 0.01f, 100.0f, 0.25f);
  data.color  = script_arg_opt_color(call, 2, geo_color_white);

  if (ctx->debug) {
    scene_debug_sphere(ctx->debug, data, eval_debug_source(ctx, call));
  }
  return script_null();
}

static ScriptVal eval_debug_box(EvalContext* ctx, ScriptBinderCall* call) {
  SceneDebugBox data;
  data.pos   = script_arg_vec3(call, 0);
  data.size  = script_arg_vec3(call, 1);
  data.rot   = script_arg_opt_quat(call, 2, geo_quat_ident);
  data.color = script_arg_opt_color(call, 3, geo_color_white);

  if (ctx->debug) {
    scene_debug_box(ctx->debug, data, eval_debug_source(ctx, call));
  }
  return script_null();
}

static ScriptVal eval_debug_arrow(EvalContext* ctx, ScriptBinderCall* call) {
  SceneDebugArrow data;
  data.start  = script_arg_vec3(call, 0);
  data.end    = script_arg_vec3(call, 1);
  data.radius = (f32)script_arg_opt_num_range(call, 2, 0.01f, 10.0f, 0.25f);
  data.color  = script_arg_opt_color(call, 3, geo_color_white);

  if (ctx->debug) {
    scene_debug_arrow(ctx->debug, data, eval_debug_source(ctx, call));
  }
  return script_null();
}

static ScriptVal eval_debug_orientation(EvalContext* ctx, ScriptBinderCall* call) {
  SceneDebugOrientation data;
  data.pos  = script_arg_vec3(call, 0);
  data.rot  = script_arg_quat(call, 1);
  data.size = (f32)script_arg_opt_num_range(call, 2, 0.01f, 10.0f, 1.0f);

  if (ctx->debug) {
    scene_debug_orientation(ctx->debug, data, eval_debug_source(ctx, call));
  }
  return script_null();
}

static ScriptVal eval_debug_text(EvalContext* ctx, ScriptBinderCall* call) {
  SceneDebugText data;
  data.pos      = script_arg_vec3(call, 0);
  data.color    = script_arg_color(call, 1);
  data.fontSize = (u16)script_arg_num_range(call, 2, 6.0, 30.0);

  DynString buffer = dynstring_create_over(alloc_alloc(g_allocScratch, usize_kibibyte, 1));
  for (u16 i = 3; i < call->argCount; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_write(call->args[i], &buffer);
  }
  data.text = dynstring_view(&buffer);

  if (ctx->debug) {
    scene_debug_text(ctx->debug, data, eval_debug_source(ctx, call));
  }
  return script_null();
}

static ScriptVal eval_debug_trace(EvalContext* ctx, ScriptBinderCall* call) {
  SceneDebugTrace data;

  DynString buffer = dynstring_create_over(alloc_alloc(g_allocScratch, usize_kibibyte, 1));
  for (u16 i = 0; i < call->argCount; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_write(call->args[i], &buffer);
  }
  data.text = dynstring_view(&buffer);

  if (ctx->debug) {
    scene_debug_trace(ctx->debug, data, eval_debug_source(ctx, call));
  }
  return script_null();
}

static ScriptVal eval_debug_break(EvalContext* ctx, ScriptBinderCall* call) {
  (void)ctx;
  (void)call;
  diag_break();
  return script_null();
}

typedef struct {
  GeoVector   pos;
  GeoVector   normal;
  EcsEntityId entity;
} DebugInputHit;

static bool eval_debug_input_hit(EvalContext* ctx, const SceneQueryFilter* f, DebugInputHit* out) {
  const SceneTerrainComp*      terrain = ecs_view_read_t(ctx->globalItr, SceneTerrainComp);
  const SceneCollisionEnvComp* colEnv  = ecs_view_read_t(ctx->globalItr, SceneCollisionEnvComp);

  static const f32 g_maxDist = 1e4f;

  f32 terrainHitT = f32_max;
  if (scene_terrain_loaded(terrain)) {
    terrainHitT = scene_terrain_intersect_ray(terrain, &ctx->debugRay, g_maxDist);
  }
  SceneRayHit hit;
  if (scene_query_ray(colEnv, &ctx->debugRay, g_maxDist, f, &hit) && hit.time < terrainHitT) {
    *out = (DebugInputHit){
        .pos    = hit.position,
        .normal = hit.normal,
        .entity = hit.entity,
    };
    return true;
  }
  if (terrainHitT < g_maxDist) {
    const GeoVector terrainHitPos = geo_ray_position(&ctx->debugRay, terrainHitT);

    *out = (DebugInputHit){
        .pos    = terrainHitPos,
        .normal = scene_terrain_normal(terrain, terrainHitPos),

    };
    return true;
  }
  return false;
}

static ScriptVal eval_debug_input_position(EvalContext* ctx, ScriptBinderCall* call) {
  const SceneQueryFilter filter = arg_query_filter(ctx->instigatorFaction, call, 0);
  DebugInputHit          hit;
  if (eval_debug_input_hit(ctx, &filter, &hit)) {
    return script_vec3(hit.pos);
  }
  return script_null();
}

static ScriptVal eval_debug_input_rotation(EvalContext* ctx, ScriptBinderCall* call) {
  const SceneQueryFilter filter = arg_query_filter(ctx->instigatorFaction, call, 0);
  DebugInputHit          hit;
  if (eval_debug_input_hit(ctx, &filter, &hit)) {
    return script_quat(geo_quat_look(hit.normal, geo_up));
  }
  return script_null();
}

static ScriptVal eval_debug_input_entity(EvalContext* ctx, ScriptBinderCall* call) {
  const SceneQueryFilter filter = arg_query_filter(ctx->instigatorFaction, call, 0);
  DebugInputHit          hit;
  if (eval_debug_input_hit(ctx, &filter, &hit)) {
    return script_entity_or_null(hit.entity);
  }
  return script_null();
}

static ScriptBinder* g_scriptBinder;

typedef ScriptVal (*SceneScriptBinderFunc)(EvalContext*, ScriptBinderCall*);

static void eval_bind(ScriptBinder* b, const String name, SceneScriptBinderFunc f) {
  const ScriptSig* nullSig       = null;
  const String     documentation = string_empty;
  // NOTE: Func pointer cast is needed to type-erase the context type.
  script_binder_declare(b, name, documentation, nullSig, (ScriptBinderFunc)f);
}

static void eval_binder_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_scriptBinder)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_scriptBinder) {
    const ScriptBinderFlags flags = ScriptBinderFlags_None;
    ScriptBinder*           b = script_binder_create(g_allocPersist, string_lit("scene"), flags);

    eval_enum_init_faction();
    eval_enum_init_clock();
    eval_enum_init_nav_layer();
    eval_enum_init_nav_find();
    eval_enum_init_capability();
    eval_enum_init_activity();
    eval_enum_init_target_exclude();
    eval_enum_init_renderable_param();
    eval_enum_init_vfx_param();
    eval_enum_init_light_param();
    eval_enum_init_sound_param();
    eval_enum_init_anim_param();
    eval_enum_init_layer();
    eval_enum_init_query_option();
    eval_enum_init_status();
    eval_enum_init_bark();
    eval_enum_init_health_stat();

    // clang-format off
    eval_bind(b, string_lit("self"),                   eval_self);
    eval_bind(b, string_lit("exists"),                 eval_exists);
    eval_bind(b, string_lit("position"),               eval_position);
    eval_bind(b, string_lit("velocity"),               eval_velocity);
    eval_bind(b, string_lit("rotation"),               eval_rotation);
    eval_bind(b, string_lit("scale"),                  eval_scale);
    eval_bind(b, string_lit("name"),                   eval_name);
    eval_bind(b, string_lit("faction"),                eval_faction);
    eval_bind(b, string_lit("health"),                 eval_health);
    eval_bind(b, string_lit("health_stat"),            eval_health_stat);
    eval_bind(b, string_lit("vision"),                 eval_vision);
    eval_bind(b, string_lit("visible"),                eval_visible);
    eval_bind(b, string_lit("time"),                   eval_time);
    eval_bind(b, string_lit("set"),                    eval_set);
    eval_bind(b, string_lit("query_set"),              eval_query_set);
    eval_bind(b, string_lit("query_sphere"),           eval_query_sphere);
    eval_bind(b, string_lit("query_box"),              eval_query_box);
    eval_bind(b, string_lit("query_pop"),              eval_query_pop);
    eval_bind(b, string_lit("query_random"),           eval_query_random);
    eval_bind(b, string_lit("nav_find"),               eval_nav_find);
    eval_bind(b, string_lit("nav_target"),             eval_nav_target);
    eval_bind(b, string_lit("line_of_sight"),          eval_line_of_sight);
    eval_bind(b, string_lit("capable"),                eval_capable);
    eval_bind(b, string_lit("active"),                 eval_active);
    eval_bind(b, string_lit("target_primary"),         eval_target_primary);
    eval_bind(b, string_lit("target_range_min"),       eval_target_range_min);
    eval_bind(b, string_lit("target_range_max"),       eval_target_range_max);
    eval_bind(b, string_lit("target_exclude"),         eval_target_exclude);
    eval_bind(b, string_lit("tell"),                   eval_tell);
    eval_bind(b, string_lit("ask"),                    eval_ask);
    eval_bind(b, string_lit("prefab_spawn"),           eval_prefab_spawn);
    eval_bind(b, string_lit("prefab_id"),              eval_prefab_id);
    eval_bind(b, string_lit("destroy"),                eval_destroy);
    eval_bind(b, string_lit("destroy_after"),          eval_destroy_after);
    eval_bind(b, string_lit("teleport"),               eval_teleport);
    eval_bind(b, string_lit("nav_travel"),             eval_nav_travel);
    eval_bind(b, string_lit("nav_stop"),               eval_nav_stop);
    eval_bind(b, string_lit("attach"),                 eval_attach);
    eval_bind(b, string_lit("detach"),                 eval_detach);
    eval_bind(b, string_lit("damage"),                 eval_damage);
    eval_bind(b, string_lit("heal"),                   eval_heal);
    eval_bind(b, string_lit("attack"),                 eval_attack);
    eval_bind(b, string_lit("attack_target"),          eval_attack_target);
    eval_bind(b, string_lit("attack_weapon"),          eval_attack_weapon);
    eval_bind(b, string_lit("bark"),                   eval_bark);
    eval_bind(b, string_lit("status"),                 eval_status);
    eval_bind(b, string_lit("renderable_spawn"),       eval_renderable_spawn);
    eval_bind(b, string_lit("renderable_param"),       eval_renderable_param);
    eval_bind(b, string_lit("vfx_system_spawn"),       eval_vfx_system_spawn);
    eval_bind(b, string_lit("vfx_decal_spawn"),        eval_vfx_decal_spawn);
    eval_bind(b, string_lit("vfx_param"),              eval_vfx_param);
    eval_bind(b, string_lit("collision_box_spawn"),    eval_collision_box_spawn);
    eval_bind(b, string_lit("collision_sphere_spawn"), eval_collision_sphere_spawn);
    eval_bind(b, string_lit("light_point_spawn"),      eval_light_point_spawn);
    eval_bind(b, string_lit("light_param"),            eval_light_param);
    eval_bind(b, string_lit("sound_spawn"),            eval_sound_spawn);
    eval_bind(b, string_lit("sound_param"),            eval_sound_param);
    eval_bind(b, string_lit("anim_param"),             eval_anim_param);
    eval_bind(b, string_lit("joint_position"),         eval_joint_position);
    eval_bind(b, string_lit("random_of"),              eval_random_of);
    eval_bind(b, string_lit("debug_log"),              eval_debug_log);
    eval_bind(b, string_lit("debug_line"),             eval_debug_line);
    eval_bind(b, string_lit("debug_sphere"),           eval_debug_sphere);
    eval_bind(b, string_lit("debug_box"),              eval_debug_box);
    eval_bind(b, string_lit("debug_arrow"),            eval_debug_arrow);
    eval_bind(b, string_lit("debug_orientation"),      eval_debug_orientation);
    eval_bind(b, string_lit("debug_text"),             eval_debug_text);
    eval_bind(b, string_lit("debug_trace"),            eval_debug_trace);
    eval_bind(b, string_lit("debug_break"),            eval_debug_break);
    eval_bind(b, string_lit("debug_input_position"),   eval_debug_input_position);
    eval_bind(b, string_lit("debug_input_rotation"),   eval_debug_input_rotation);
    eval_bind(b, string_lit("debug_input_entity"),     eval_debug_input_entity);
    // clang-format on

    script_binder_finalize(b);
    g_scriptBinder = b;
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  SceneScriptRes_ResourceAcquired  = 1 << 0,
  SceneScriptRes_ResourceUnloading = 1 << 1,
} SceneScriptResFlags;

typedef struct {
  u8               resVersion;
  EcsEntityId      asset;
  SceneScriptStats stats;
  ScriptPanic      panic;
} SceneScriptData;

ecs_comp_define(SceneScriptComp) {
  SceneScriptFlags flags : 8;
  u8               slotCount;
  SceneScriptData* slots; // SceneScriptData[slotCount].
};

ecs_comp_define(SceneScriptResourceComp) {
  SceneScriptResFlags flags : 8;
  u8                  resVersion; // NOTE: Allowed to wrap around.
};

static void ecs_destruct_script_instance(void* data) {
  SceneScriptComp* scriptInstance = data;
  alloc_free_array_t(g_allocHeap, scriptInstance->slots, scriptInstance->slotCount);
}

static void ecs_combine_script_resource(void* dataA, void* dataB) {
  SceneScriptResourceComp* a = dataA;
  SceneScriptResourceComp* b = dataB;
  a->flags |= b->flags;
  a->resVersion = math_max(a->resVersion, b->resVersion);
}

ecs_view_define(ScriptUpdateView) {
  ecs_access_write(SceneScriptComp);
  ecs_access_write(SceneKnowledgeComp);
  ecs_access_write(SceneActionQueueComp);
  ecs_access_maybe_write(SceneDebugComp);
  ecs_access_maybe_read(SceneFactionComp);
}

ecs_view_define(ResourceAssetView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetScriptComp);
  ecs_access_read(SceneScriptResourceComp);
}

ecs_view_define(ResourceLoadView) { ecs_access_write(SceneScriptResourceComp); }

ecs_system_define(SceneScriptResourceLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, ResourceLoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    SceneScriptResourceComp* res = ecs_view_write_t(itr, SceneScriptResourceComp);

    if (!(res->flags & (SceneScriptRes_ResourceAcquired | SceneScriptRes_ResourceUnloading))) {
      asset_acquire(world, ecs_view_entity(itr));
      res->flags |= SceneScriptRes_ResourceAcquired;
      ++res->resVersion;
    }
  }
}

ecs_system_define(SceneScriptResourceUnloadChangedSys) {
  EcsView* loadView = ecs_world_view_t(world, ResourceLoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId        entity = ecs_view_entity(itr);
    SceneScriptResourceComp* res    = ecs_view_write_t(itr, SceneScriptResourceComp);

    const bool isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (res->flags & SceneScriptRes_ResourceAcquired && (isLoaded || isFailed) && hasChanged) {
      log_i("Unloading script asset", log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, entity);
      res->flags &= ~SceneScriptRes_ResourceAcquired;
      res->flags |= SceneScriptRes_ResourceUnloading;
    }
    if (res->flags & SceneScriptRes_ResourceUnloading && !isLoaded) {
      res->flags &= ~SceneScriptRes_ResourceUnloading;
    }
  }
}

static void scene_script_eval(EvalContext* ctx) {
  SceneScriptData* data = &ctx->scriptInstance->slots[ctx->slot];
  if (UNLIKELY(ctx->scriptInstance->flags & SceneScriptFlags_PauseEvaluation)) {
    data->stats = (SceneScriptStats){0};
    data->panic = (ScriptPanic){0};
    return;
  }

  ScriptMem* mem = scene_knowledge_memory_mut(ctx->scriptKnowledge);

  // Eval.
  const TimeSteady       startTime = time_steady_clock();
  const ScriptProgResult evalRes   = script_prog_eval(ctx->scriptProgram, mem, g_scriptBinder, ctx);

  // Handle panics.
  if (UNLIKELY(evalRes.panic.kind)) {
    const String msg            = script_panic_scratch(&evalRes.panic, ScriptPanicOutput_Default);
    const String scriptRangeStr = fmt_write_scratch(
        "{}:{}-{}:{}",
        fmt_int(evalRes.panic.range.start.line + 1),
        fmt_int(evalRes.panic.range.start.column + 1),
        fmt_int(evalRes.panic.range.end.line + 1),
        fmt_int(evalRes.panic.range.end.column + 1));

    log_e(
        "Script panic",
        log_param("panic", fmt_text(msg)),
        log_param("script", fmt_text(ctx->scriptId)),
        log_param("script-range", fmt_text(scriptRangeStr)),
        log_param("entity", ecs_entity_fmt(ctx->instigator)));

    ctx->scriptInstance->flags |= SceneScriptFlags_DidPanic;
    data->panic = evalRes.panic;
  } else {
    data->panic = (ScriptPanic){0};
  }

  // Update stats.
  data->stats.executedOps = evalRes.executedOps;
  data->stats.executedDur = time_steady_duration(startTime, time_steady_clock());
}

ecs_system_define(SceneScriptUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, EvalGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependency not yet initialized.
  }
  const SceneDebugEnvComp* debugEnv = ecs_view_read_t(globalItr, SceneDebugEnvComp);

  EcsView* scriptView        = ecs_world_view_t(world, ScriptUpdateView);
  EcsView* resourceAssetView = ecs_world_view_t(world, ResourceAssetView);

  EcsIterator* resourceAssetItr = ecs_view_itr(resourceAssetView);

  EvalQuery   queries[scene_script_query_max];
  EvalContext ctx = {
      .world             = world,
      .globalItr         = globalItr,
      .animItr           = ecs_view_itr(ecs_world_view_t(world, EvalAnimView)),
      .attackItr         = ecs_view_itr(ecs_world_view_t(world, EvalAttackView)),
      .factionItr        = ecs_view_itr(ecs_world_view_t(world, EvalFactionView)),
      .healthItr         = ecs_view_itr(ecs_world_view_t(world, EvalHealthView)),
      .healthStatsItr    = ecs_view_itr(ecs_world_view_t(world, EvalHealthStatsView)),
      .lightDirItr       = ecs_view_itr(ecs_world_view_t(world, EvalLightDirView)),
      .lightPointItr     = ecs_view_itr(ecs_world_view_t(world, EvalLightPointView)),
      .lineOfSightItr    = ecs_view_itr(ecs_world_view_t(world, EvalLineOfSightView)),
      .locoItr           = ecs_view_itr(ecs_world_view_t(world, EvalLocoView)),
      .nameItr           = ecs_view_itr(ecs_world_view_t(world, EvalNameView)),
      .navAgentItr       = ecs_view_itr(ecs_world_view_t(world, EvalNavAgentView)),
      .prefabInstanceItr = ecs_view_itr(ecs_world_view_t(world, EvalPrefabInstanceView)),
      .renderableItr     = ecs_view_itr(ecs_world_view_t(world, EvalRenderableView)),
      .scaleItr          = ecs_view_itr(ecs_world_view_t(world, EvalScaleView)),
      .skeletonItr       = ecs_view_itr(ecs_world_view_t(world, EvalSkeletonView)),
      .skeletonTemplItr  = ecs_view_itr(ecs_world_view_t(world, EvalSkeletonTemplView)),
      .soundItr          = ecs_view_itr(ecs_world_view_t(world, EvalSoundView)),
      .statusItr         = ecs_view_itr(ecs_world_view_t(world, EvalStatusView)),
      .targetItr         = ecs_view_itr(ecs_world_view_t(world, EvalTargetView)),
      .transformItr      = ecs_view_itr(ecs_world_view_t(world, EvalTransformView)),
      .veloItr           = ecs_view_itr(ecs_world_view_t(world, EvalVelocityView)),
      .vfxDecalItr       = ecs_view_itr(ecs_world_view_t(world, EvalVfxDecalView)),
      .vfxSysItr         = ecs_view_itr(ecs_world_view_t(world, EvalVfxSysView)),
      .visionItr         = ecs_view_itr(ecs_world_view_t(world, EvalVisionView)),
      .debugRay          = debugEnv ? scene_debug_ray(debugEnv) : (GeoRay){.dir = geo_forward},
      .queries           = queries,
  };

  u32 startedAssetLoads = 0;
  for (EcsIterator* itr = ecs_view_itr_step(scriptView, parCount, parIndex); ecs_view_walk(itr);) {
    const SceneFactionComp* factionComp = ecs_view_read_t(itr, SceneFactionComp);

    ctx.instigator        = ecs_view_entity(itr);
    ctx.instigatorFaction = factionComp ? factionComp->id : SceneFaction_None;
    ctx.scriptInstance    = ecs_view_write_t(itr, SceneScriptComp);
    ctx.scriptKnowledge   = ecs_view_write_t(itr, SceneKnowledgeComp);
    ctx.actions           = ecs_view_write_t(itr, SceneActionQueueComp);
    ctx.debug             = ecs_view_write_t(itr, SceneDebugComp);

    for (SceneScriptSlot slot = 0; slot != ctx.scriptInstance->slotCount; ++slot) {
      SceneScriptData* data = &ctx.scriptInstance->slots[slot];
      ctx.slot              = slot;
      ctx.usedQueries       = 0;

      // Evaluate the script if the asset is loaded.
      if (ecs_view_maybe_jump(resourceAssetItr, data->asset)) {
        ctx.scriptId                       = asset_id(ecs_view_read_t(resourceAssetItr, AssetComp));
        const AssetScriptComp* scriptAsset = ecs_view_read_t(resourceAssetItr, AssetScriptComp);
        if (UNLIKELY(scriptAsset->domain != AssetScriptDomain_Scene)) {
          log_e("Mismatched script-domain", log_param("script", fmt_text(ctx.scriptId)));
          continue;
        }
        ctx.scriptProgram = &scriptAsset->prog;

        const u8 version = ecs_view_read_t(resourceAssetItr, SceneScriptResourceComp)->resVersion;
        if (UNLIKELY(data->resVersion != version)) {
          ctx.scriptInstance->flags &= ~SceneScriptFlags_DidPanic;
          data->resVersion = version;
        }
        scene_script_eval(&ctx);
      } else {
        // Script asset not loaded; clear any previous stats and start loading it.
        data->stats = (SceneScriptStats){0};
        data->panic = (ScriptPanic){0};
        if (!ecs_world_has_t(world, data->asset, SceneScriptResourceComp)) {
          if (++startedAssetLoads < scene_script_max_asset_loads) {
            ecs_world_add_t(world, data->asset, SceneScriptResourceComp);
          }
        }
      }
    }
  }
}

ecs_module_init(scene_script_module) {
  eval_binder_init();

  ecs_register_comp(SceneScriptComp, .destructor = ecs_destruct_script_instance);
  ecs_register_comp(SceneScriptResourceComp, .combinator = ecs_combine_script_resource);

  ecs_register_view(ResourceAssetView);
  ecs_register_view(ResourceLoadView);
  ecs_register_view(ScriptUpdateView);

  ecs_register_system(SceneScriptResourceLoadSys, ecs_view_id(ResourceLoadView));
  ecs_register_system(SceneScriptResourceUnloadChangedSys, ecs_view_id(ResourceLoadView));

  ecs_register_system(
      SceneScriptUpdateSys,
      ecs_view_id(ScriptUpdateView),
      ecs_view_id(ResourceAssetView),
      ecs_register_view(EvalAnimView),
      ecs_register_view(EvalAttackView),
      ecs_register_view(EvalFactionView),
      ecs_register_view(EvalGlobalView),
      ecs_register_view(EvalHealthStatsView),
      ecs_register_view(EvalHealthView),
      ecs_register_view(EvalLightDirView),
      ecs_register_view(EvalLightPointView),
      ecs_register_view(EvalLineOfSightView),
      ecs_register_view(EvalLocoView),
      ecs_register_view(EvalNameView),
      ecs_register_view(EvalNavAgentView),
      ecs_register_view(EvalPrefabInstanceView),
      ecs_register_view(EvalRenderableView),
      ecs_register_view(EvalScaleView),
      ecs_register_view(EvalSkeletonTemplView),
      ecs_register_view(EvalSkeletonView),
      ecs_register_view(EvalSoundView),
      ecs_register_view(EvalStatusView),
      ecs_register_view(EvalTargetView),
      ecs_register_view(EvalTransformView),
      ecs_register_view(EvalVelocityView),
      ecs_register_view(EvalVfxDecalView),
      ecs_register_view(EvalVfxSysView),
      ecs_register_view(EvalVisionView));

  ecs_order(SceneScriptUpdateSys, SceneOrder_ScriptUpdate);
  ecs_parallel(SceneScriptUpdateSys, g_jobsWorkerCount);
}

SceneScriptFlags scene_script_flags(const SceneScriptComp* script) { return script->flags; }

void scene_script_flags_set(SceneScriptComp* script, const SceneScriptFlags flags) {
  script->flags |= flags;
}

void scene_script_flags_unset(SceneScriptComp* script, const SceneScriptFlags flags) {
  script->flags &= ~flags;
}

void scene_script_flags_toggle(SceneScriptComp* script, const SceneScriptFlags flags) {
  script->flags ^= flags;
}

u32 scene_script_count(const SceneScriptComp* script) { return script->slotCount; }

EcsEntityId scene_script_asset(const SceneScriptComp* script, const SceneScriptSlot slot) {
  diag_assert(slot < script->slotCount);
  return script->slots[slot].asset;
}

const ScriptPanic* scene_script_panic(const SceneScriptComp* script, const SceneScriptSlot slot) {
  diag_assert(slot < script->slotCount);
  const ScriptPanic* panic = &script->slots[slot].panic;
  return panic->kind ? panic : null;
}

const SceneScriptStats*
scene_script_stats(const SceneScriptComp* script, const SceneScriptSlot slot) {
  diag_assert(slot < script->slotCount);
  return &script->slots[slot].stats;
}

SceneScriptComp* scene_script_add(
    EcsWorld*         world,
    const EcsEntityId entity,
    const EcsEntityId scriptAssets[],
    const u32         scriptAssetCount) {
  diag_assert(scriptAssetCount <= u8_max); // We represent slot indices as 8bit integers.
  diag_assert(scriptAssetCount);           // Need at least one script asset.

  SceneScriptComp* script = ecs_world_add_t(world, entity, SceneScriptComp);

  script->slotCount = (u8)scriptAssetCount;
  script->slots     = alloc_array_t(g_allocHeap, SceneScriptData, scriptAssetCount);
  for (u32 i = 0; i != scriptAssetCount; ++i) {
    diag_assert(ecs_world_exists(world, scriptAssets[i]));
    script->slots[i].asset = scriptAssets[i];
  }

  scene_action_queue_add(world, entity);

  return script;
}
