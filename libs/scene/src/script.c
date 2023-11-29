#include "asset_manager.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_bark.h"
#include "scene_collision.h"
#include "scene_health.h"
#include "scene_knowledge.h"
#include "scene_level.h"
#include "scene_lifetime.h"
#include "scene_location.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_prefab.h"
#include "scene_register.h"
#include "scene_script.h"
#include "scene_set.h"
#include "scene_sound.h"
#include "scene_status.h"
#include "scene_tag.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_visibility.h"
#include "script_binder.h"
#include "script_enum.h"
#include "script_error.h"
#include "script_eval.h"

#define scene_script_max_asset_loads 8
#define scene_script_line_of_sight_min 1.0f
#define scene_script_line_of_sight_max 50.0f
#define scene_script_query_values_max 512

// clang-format off

static ScriptEnum g_scriptEnumFaction,
                  g_scriptEnumClock,
                  g_scriptEnumNavFind,
                  g_scriptEnumCapability,
                  g_scriptEnumActivity,
                  g_scriptEnumVfxParam,
                  g_scriptEnumSoundParam,
                  g_scriptEnumLayer,
                  g_scriptEnumStatus,
                  g_scriptEnumBark;

// clang-format on

static void eval_enum_init_faction() {
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionA"), SceneFaction_A);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionB"), SceneFaction_B);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionC"), SceneFaction_C);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionD"), SceneFaction_D);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionNone"), SceneFaction_None);
}

static void eval_enum_init_clock() {
  script_enum_push(&g_scriptEnumClock, string_lit("Time"), 0);
  script_enum_push(&g_scriptEnumClock, string_lit("RealTime"), 1);
  script_enum_push(&g_scriptEnumClock, string_lit("Delta"), 2);
  script_enum_push(&g_scriptEnumClock, string_lit("RealDelta"), 3);
  script_enum_push(&g_scriptEnumClock, string_lit("Ticks"), 4);
}

static void eval_enum_init_nav_find() {
  script_enum_push(&g_scriptEnumNavFind, string_lit("ClosestCell"), 0);
  script_enum_push(&g_scriptEnumNavFind, string_lit("UnblockedCell"), 1);
  script_enum_push(&g_scriptEnumNavFind, string_lit("FreeCell"), 2);
}

static void eval_enum_init_capability() {
  script_enum_push(&g_scriptEnumCapability, string_lit("NavTravel"), 0);
  script_enum_push(&g_scriptEnumCapability, string_lit("Attack"), 1);
  script_enum_push(&g_scriptEnumCapability, string_lit("Status"), 2);
}

static void eval_enum_init_activity() {
  script_enum_push(&g_scriptEnumActivity, string_lit("Moving"), 0);
  script_enum_push(&g_scriptEnumActivity, string_lit("Traveling"), 1);
  script_enum_push(&g_scriptEnumActivity, string_lit("Attacking"), 2);
  script_enum_push(&g_scriptEnumActivity, string_lit("Firing"), 3);
}

static void eval_enum_init_vfx_param() {
  script_enum_push(&g_scriptEnumVfxParam, string_lit("Alpha"), 0);
}

static void eval_enum_init_sound_param() {
  script_enum_push(&g_scriptEnumSoundParam, string_lit("Gain"), 0);
  script_enum_push(&g_scriptEnumSoundParam, string_lit("Pitch"), 1);
}

static void eval_enum_init_layer() {
  // clang-format off
  script_enum_push(&g_scriptEnumLayer, string_lit("Environment"),       SceneLayer_Environment);
  script_enum_push(&g_scriptEnumLayer, string_lit("Destructible"),      SceneLayer_Destructible);
  script_enum_push(&g_scriptEnumLayer, string_lit("Infantry"),          SceneLayer_Infantry);
  script_enum_push(&g_scriptEnumLayer, string_lit("Structure"),         SceneLayer_Structure);
  script_enum_push(&g_scriptEnumLayer, string_lit("Unit"),              SceneLayer_Unit);
  script_enum_push(&g_scriptEnumLayer, string_lit("Debug"),             SceneLayer_Debug);
  script_enum_push(&g_scriptEnumLayer, string_lit("AllIncludingDebug"), SceneLayer_AllIncludingDebug);
  script_enum_push(&g_scriptEnumLayer, string_lit("AllNonDebug"),       SceneLayer_AllNonDebug);
  // clang-format on
}

static void eval_enum_init_status() {
  for (SceneStatusType type = 0; type != SceneStatusType_Count; ++type) {
    script_enum_push(&g_scriptEnumStatus, scene_status_name(type), type);
  }
}

static void eval_enum_init_bark() {
  script_enum_push(&g_scriptEnumBark, string_lit("Death"), SceneBarkType_Death);
  script_enum_push(&g_scriptEnumBark, string_lit("Confirm"), SceneBarkType_Confirm);
}

typedef enum {
  ScriptActionType_Tell,
  ScriptActionType_Ask,
  ScriptActionType_Spawn,
  ScriptActionType_Teleport,
  ScriptActionType_NavTravel,
  ScriptActionType_NavStop,
  ScriptActionType_Attach,
  ScriptActionType_Detach,
  ScriptActionType_Damage,
  ScriptActionType_Attack,
  ScriptActionType_Bark,
  ScriptActionType_UpdateSet,
  ScriptActionType_UpdateTags,
  ScriptActionType_UpdateVfxParam,
  ScriptActionType_UpdateSoundParam,
} ScriptActionType;

typedef struct {
  EcsEntityId entity;
  StringHash  memKey;
  ScriptVal   value;
} ScriptActionTell;

typedef struct {
  EcsEntityId entity;
  EcsEntityId target;
  StringHash  memKey;
} ScriptActionAsk;

typedef struct {
  EcsEntityId  entity;
  StringHash   prefabId;
  f32          scale;
  SceneFaction faction;
  GeoVector    position;
  GeoQuat      rotation;
} ScriptActionSpawn;

typedef struct {
  EcsEntityId entity;
  GeoVector   position;
  GeoQuat     rotation;
} ScriptActionTeleport;

typedef struct {
  EcsEntityId entity;
  EcsEntityId targetEntity; // If zero: The targetPosition is used instead.
  GeoVector   targetPosition;
} ScriptActionNavTravel;

typedef struct {
  EcsEntityId entity;
} ScriptActionNavStop;

typedef struct {
  EcsEntityId entity;
  EcsEntityId target;
  StringHash  jointName;
} ScriptActionAttach;

typedef struct {
  EcsEntityId entity;
} ScriptActionDetach;

typedef struct {
  EcsEntityId entity;
  f32         amount;
} ScriptActionDamage;

typedef struct {
  EcsEntityId entity;
  EcsEntityId target;
} ScriptActionAttack;

typedef struct {
  EcsEntityId   entity;
  SceneBarkType type;
} ScriptActionBark;

typedef struct {
  EcsEntityId entity;
  StringHash  set;
  bool        add;
} ScriptActionUpdateSet;

typedef struct {
  EcsEntityId entity;
  SceneTags   toEnable, toDisable;
} ScriptActionUpdateTags;

typedef struct {
  EcsEntityId entity;
  i32         param;
  f32         value;
} ScriptActionUpdateVfxParam;

typedef struct {
  EcsEntityId entity;
  i32         param;
  f32         value;
} ScriptActionUpdateSoundParam;

typedef struct {
  ScriptActionType type;
  union {
    ScriptActionTell             data_tell;
    ScriptActionAsk              data_ask;
    ScriptActionSpawn            data_spawn;
    ScriptActionTeleport         data_teleport;
    ScriptActionNavTravel        data_navTravel;
    ScriptActionNavStop          data_navStop;
    ScriptActionAttach           data_attach;
    ScriptActionDetach           data_detach;
    ScriptActionDamage           data_damage;
    ScriptActionAttack           data_attack;
    ScriptActionBark             data_bark;
    ScriptActionUpdateSet        data_updateSet;
    ScriptActionUpdateTags       data_updateTags;
    ScriptActionUpdateVfxParam   data_updateVfxParam;
    ScriptActionUpdateSoundParam data_updateSoundParam;
  };
} ScriptAction;

ecs_view_define(EvalGlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneSetEnvComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneVisibilityEnvComp);
}

ecs_view_define(EvalTransformView) { ecs_access_read(SceneTransformComp); }
ecs_view_define(EvalVelocityView) { ecs_access_read(SceneVelocityComp); }
ecs_view_define(EvalScaleView) { ecs_access_read(SceneScaleComp); }
ecs_view_define(EvalNameView) { ecs_access_read(SceneNameComp); }
ecs_view_define(EvalFactionView) { ecs_access_read(SceneFactionComp); }
ecs_view_define(EvalHealthView) { ecs_access_read(SceneHealthComp); }
ecs_view_define(EvalStatusView) { ecs_access_read(SceneStatusComp); }
ecs_view_define(EvalTagView) { ecs_access_read(SceneTagComp); }
ecs_view_define(EvalVfxSysView) { ecs_access_read(SceneVfxSystemComp); }
ecs_view_define(EvalVfxDecalView) { ecs_access_read(SceneVfxDecalComp); }
ecs_view_define(EvalSoundView) { ecs_access_read(SceneSoundComp); }
ecs_view_define(EvalNavAgentView) { ecs_access_read(SceneNavAgentComp); }
ecs_view_define(EvalLocoView) { ecs_access_read(SceneLocomotionComp); }
ecs_view_define(EvalAttackView) { ecs_access_read(SceneAttackComp); }
ecs_view_define(EvalTargetView) { ecs_access_read(SceneTargetFinderComp); }

ecs_view_define(EvalLineOfSightView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneLocationComp);
  ecs_access_maybe_read(SceneCollisionComp);
}

typedef struct {
  EcsEntityId values[scene_script_query_values_max];
  u32         count, itr;
} EvalQuery;

typedef struct {
  EcsWorld*    world;
  EcsIterator* globalItr;
  EcsIterator* transformItr;
  EcsIterator* veloItr;
  EcsIterator* scaleItr;
  EcsIterator* nameItr;
  EcsIterator* factionItr;
  EcsIterator* healthItr;
  EcsIterator* statusItr;
  EcsIterator* tagItr;
  EcsIterator* vfxSysItr;
  EcsIterator* vfxDecalItr;
  EcsIterator* soundItr;
  EcsIterator* navAgentItr;
  EcsIterator* locoItr;
  EcsIterator* attackItr;
  EcsIterator* targetItr;
  EcsIterator* lineOfSightItr;

  EcsEntityId            instigator;
  SceneScriptComp*       scriptInstance;
  SceneKnowledgeComp*    scriptKnowledge;
  const AssetScriptComp* scriptAsset;
  String                 scriptId;
  DynArray*              actions; // ScriptAction[].
  DynArray*              debug;   // SceneScriptDebug[].

  EvalQuery* query;

  Mem (*transientDup)(SceneScriptComp*, Mem src, usize align);
} EvalContext;

static EcsEntityId arg_asset(EvalContext* ctx, const ScriptArgs a, const u16 i, ScriptError* err) {
  const EcsEntityId e = script_arg_entity(a, i, err);
  if (UNLIKELY(script_error_valid(err))) {
    return e;
  }
  if (UNLIKELY(!ecs_world_exists(ctx->world, e) || !ecs_world_has_t(ctx->world, e, AssetComp))) {
    *err = script_error_arg(ScriptError_ArgumentInvalid, i);
  }
  return e;
}

static ScriptVal eval_self(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  (void)args;
  (void)err;
  return script_entity(ctx->instigator);
}

static ScriptVal eval_exists(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  if (script_arg_check(args, 0, script_mask_entity | script_mask_null, err)) {
    const EcsEntityId e = script_get_entity(args.values[0], ecs_entity_invalid);
    return script_bool(e && ecs_world_exists(ctx->world, e));
  }
  return script_bool(false);
}

static ScriptVal eval_position(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId  e   = script_arg_entity(args, 0, err);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->transformItr, e);
  return itr ? script_vec3(ecs_view_read_t(itr, SceneTransformComp)->position) : script_null();
}

static ScriptVal eval_velocity(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId  e   = script_arg_entity(args, 0, err);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->veloItr, e);
  return itr ? script_vec3(ecs_view_read_t(itr, SceneVelocityComp)->velocityAvg) : script_null();
}

static ScriptVal eval_rotation(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId  e   = script_arg_entity(args, 0, err);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->transformItr, e);
  return itr ? script_quat(ecs_view_read_t(itr, SceneTransformComp)->rotation) : script_null();
}

static ScriptVal eval_scale(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId  e   = script_arg_entity(args, 0, err);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->scaleItr, e);
  return itr ? script_num(ecs_view_read_t(itr, SceneScaleComp)->scale) : script_null();
}

static ScriptVal eval_name(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId  e   = script_arg_entity(args, 0, err);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->nameItr, e);
  return itr ? script_str(ecs_view_read_t(itr, SceneNameComp)->name) : script_null();
}

static ScriptVal eval_faction(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId  e   = script_arg_entity(args, 0, err);
  const EcsIterator* itr = ecs_view_maybe_jump(ctx->factionItr, e);
  if (itr) {
    const SceneFactionComp* factionComp = ecs_view_read_t(itr, SceneFactionComp);
    const StringHash factionName = script_enum_lookup_name(&g_scriptEnumFaction, factionComp->id);
    return factionName ? script_str(factionName) : script_null();
  }
  return script_null();
}

static ScriptVal eval_health(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e = script_arg_entity(args, 0, err);
  if (ecs_view_maybe_jump(ctx->healthItr, e)) {
    const SceneHealthComp* healthComp = ecs_view_read_t(ctx->healthItr, SceneHealthComp);
    return script_num(scene_health_points(healthComp));
  }
  return script_null();
}

static ScriptVal eval_visible(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const GeoVector    pos        = script_arg_vec3(args, 0, err);
  const SceneFaction factionDef = SceneFaction_A;
  const SceneFaction faction = script_arg_opt_enum(args, 1, &g_scriptEnumFaction, factionDef, err);
  if (UNLIKELY(script_error_valid(err))) {
    return script_null();
  }
  const SceneVisibilityEnvComp* env = ecs_view_read_t(ctx->globalItr, SceneVisibilityEnvComp);
  return script_bool(scene_visible_pos(env, faction, pos));
}

static ScriptVal eval_time(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const SceneTimeComp* time = ecs_view_read_t(ctx->globalItr, SceneTimeComp);
  if (!args.count) {
    return script_time(time->time);
  }
  switch (script_arg_enum(args, 0, &g_scriptEnumClock, err)) {
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

static ScriptVal eval_set(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e = script_arg_entity(args, 0, err);
  if (UNLIKELY(!e)) {
    return script_null();
  }
  if (UNLIKELY(!eval_set_allowed(ctx, e))) {
    *err = script_error_arg(ScriptError_ArgumentInvalid, 0);
    return script_null();
  }
  const StringHash set = script_arg_str(args, 1, err);
  if (UNLIKELY(!set)) {
    return script_null();
  }
  if (args.count == 2) {
    const SceneSetEnvComp* setEnv = ecs_view_read_t(ctx->globalItr, SceneSetEnvComp);
    return script_bool(scene_set_contains(setEnv, set, e));
  }
  *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
      .type           = ScriptActionType_UpdateSet,
      .data_updateSet = {.entity = e, .set = set, .add = script_arg_bool(args, 2, err)},
  };
  return script_null();
}

static ScriptVal eval_query_set(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const SceneSetEnvComp* setEnv = ecs_view_read_t(ctx->globalItr, SceneSetEnvComp);

  EvalQuery* query = ctx->query;

  const StringHash set = script_arg_str(args, 0, err);
  if (UNLIKELY(!set)) {
    query->count = query->itr = 0;
    return script_null();
  }

  const EcsEntityId* begin = scene_set_begin(setEnv, set);
  const EcsEntityId* end   = scene_set_end(setEnv, set);

  query->count = math_min((u32)(end - begin), scene_query_max_hits);
  query->itr   = 0;

  mem_cpy(
      mem_create(query->values, sizeof(EcsEntityId) * scene_query_max_hits),
      mem_create(begin, sizeof(EcsEntityId) * query->count));

  return script_null();
}

static ScriptVal eval_query_sphere(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(ctx->globalItr, SceneCollisionEnvComp);

  EvalQuery* query = ctx->query;

  const GeoVector pos    = script_arg_vec3(args, 0, err);
  const f32       radius = (f32)script_arg_num_range(args, 1, 0.01, 100.0, err);

  SceneLayer layerMask;
  if (args.count < 3) {
    layerMask = SceneLayer_AllNonDebug;
  } else {
    layerMask = 0;
    for (u8 argIndex = 2; argIndex != args.count; ++argIndex) {
      layerMask |= (SceneLayer)script_arg_enum(args, argIndex, &g_scriptEnumLayer, err);
    }
  }

  if (UNLIKELY(script_error_valid(err))) {
    query->count = query->itr = 0;
    return script_null();
  }

  ASSERT(scene_script_query_values_max >= scene_query_max_hits, "Maximum query count too small")

  const SceneQueryFilter filter = {.layerMask = layerMask};
  const GeoSphere        sphere = {.point = pos, .radius = radius};

  query->count = scene_query_sphere_all(colEnv, &sphere, &filter, query->values);
  query->itr   = 0;

  return script_null();
}

static ScriptVal eval_query_next(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  (void)args;
  (void)err;

  EvalQuery* query = ctx->query;

  if (query->itr == query->count) {
    return script_null();
  }
  return script_entity(query->values[query->itr++]);
}

static ScriptVal eval_query_random(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  (void)args;
  (void)err;

  EvalQuery* query = ctx->query;

  if (query->itr == query->count) {
    return script_null();
  }
  const u32 index = (u32)rng_sample_range(g_rng, query->itr, query->count);
  diag_assert(index < query->count);
  return script_entity(query->values[index]);
}

static ScriptVal eval_nav_find(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const SceneNavEnvComp* navEnv = ecs_view_read_t(ctx->globalItr, SceneNavEnvComp);
  const GeoVector        pos    = script_arg_vec3(args, 0, err);
  if (UNLIKELY(err->kind)) {
    return script_null();
  }
  GeoNavCell                cell          = scene_nav_at_position(navEnv, pos);
  const GeoNavCellContainer cellContainer = {.cells = &cell, .capacity = 1};
  if (args.count == 1) {
    return script_vec3(scene_nav_position(navEnv, cell));
  }
  switch (script_arg_enum(args, 1, &g_scriptEnumNavFind, err)) {
  case 0 /* ClosestCell */:
    return script_vec3(scene_nav_position(navEnv, cell));
  case 1 /* UnblockedCell */:
    scene_nav_closest_unblocked_n(navEnv, cell, cellContainer);
    return script_vec3(scene_nav_position(navEnv, cell));
  case 2 /* FreeCell */:
    scene_nav_closest_free_n(navEnv, cell, cellContainer);
    return script_vec3(scene_nav_position(navEnv, cell));
  }
  return script_null();
}

static ScriptVal eval_nav_target(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId        e     = script_arg_entity(args, 0, err);
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
  EcsEntityId srcEntity;
} EvalLineOfSightFilterCtx;

static bool eval_line_of_sight_filter(const void* context, const EcsEntityId entity) {
  const EvalLineOfSightFilterCtx* ctx = context;
  if (entity == ctx->srcEntity) {
    return false; // Ignore collisions with the source.
  }
  return true;
}

static ScriptVal eval_line_of_sight(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(ctx->globalItr, SceneCollisionEnvComp);

  const EcsEntityId srcEntity = script_arg_entity(args, 0, err);
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

  const EcsEntityId tgtEntity = script_arg_entity(args, 1, err);
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

  const EvalLineOfSightFilterCtx filterCtx = {.srcEntity = srcEntity};
  const SceneQueryFilter         filter    = {
      .layerMask = SceneLayer_Environment | SceneLayer_Structure | tgtCol->layer,
      .callback  = eval_line_of_sight_filter,
      .context   = &filterCtx,
  };
  const GeoRay ray    = {.point = srcPos, .dir = geo_vector_div(toTgt, dist)};
  const f32    radius = (f32)script_arg_opt_num_range(args, 2, 0.0, 10.0, 0.0, err);

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

static ScriptVal eval_capable(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e = script_arg_entity(args, 0, err);
  if (!e || !ecs_world_exists(ctx->world, e)) {
    return script_bool(false);
  }
  switch (script_arg_enum(args, 1, &g_scriptEnumCapability, err)) {
  case 0 /* NavTravel */:
    return script_bool(ecs_world_has_t(ctx->world, e, SceneNavAgentComp));
  case 1 /* Attack */:
    return script_bool(ecs_world_has_t(ctx->world, e, SceneAttackComp));
  case 2 /* Status */:
    return script_bool(ecs_world_has_t(ctx->world, e, SceneStatusComp));
  }
  return script_null();
}

static ScriptVal eval_active(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e = script_arg_entity(args, 0, err);
  switch (script_arg_enum(args, 1, &g_scriptEnumActivity, err)) {
  case 0 /* Moving */: {
    const EcsIterator*         itr  = ecs_view_maybe_jump(ctx->locoItr, e);
    const SceneLocomotionComp* loco = itr ? ecs_view_read_t(itr, SceneLocomotionComp) : null;
    return script_bool(loco && (loco->flags & SceneLocomotion_Moving) != 0);
  }
  case 1 /* Traveling */: {
    const EcsIterator*       itr   = ecs_view_maybe_jump(ctx->navAgentItr, e);
    const SceneNavAgentComp* agent = itr ? ecs_view_read_t(itr, SceneNavAgentComp) : null;
    return script_bool(agent && (agent->flags & SceneNavAgent_Traveling) != 0);
  }
  case 2 /* Attacking */: {
    const EcsIterator*     itr    = ecs_view_maybe_jump(ctx->attackItr, e);
    const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
    return script_bool(attack && ecs_entity_valid(attack->targetEntity));
  }
  case 3 /* Firing */: {
    const EcsIterator*     itr    = ecs_view_maybe_jump(ctx->attackItr, e);
    const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
    return script_bool(attack && (attack->flags & SceneAttackFlags_Firing) != 0);
  }
  }
  return script_null();
}

static ScriptVal eval_target_primary(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e = script_arg_entity(args, 0, err);
  if (ecs_view_maybe_jump(ctx->targetItr, e)) {
    const SceneTargetFinderComp* finder = ecs_view_read_t(ctx->targetItr, SceneTargetFinderComp);
    return script_entity_or_null(scene_target_primary(finder));
  }
  return script_null();
}

static ScriptVal eval_target_range_min(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e = script_arg_entity(args, 0, err);
  if (ecs_view_maybe_jump(ctx->targetItr, e)) {
    const SceneTargetFinderComp* finder = ecs_view_read_t(ctx->targetItr, SceneTargetFinderComp);
    return script_num(finder->rangeMin);
  }
  return script_null();
}

static ScriptVal eval_target_range_max(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e = script_arg_entity(args, 0, err);
  if (ecs_view_maybe_jump(ctx->targetItr, e)) {
    const SceneTargetFinderComp* finder = ecs_view_read_t(ctx->targetItr, SceneTargetFinderComp);
    return script_num(finder->rangeMax);
  }
  return script_null();
}

static ScriptVal eval_tell(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e     = script_arg_entity(args, 0, err);
  const StringHash  key   = script_arg_str(args, 1, err);
  const ScriptVal   value = script_arg_any(args, 2, err);
  if (LIKELY(e && key)) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type      = ScriptActionType_Tell,
        .data_tell = {.entity = e, .memKey = key, .value = value},
    };
  }
  return script_null();
}

static ScriptVal eval_ask(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId e      = script_arg_entity(args, 0, err);
  const EcsEntityId target = script_arg_entity(args, 1, err);
  const StringHash  key    = script_arg_str(args, 2, err);
  if (LIKELY(e && target && key)) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type     = ScriptActionType_Ask,
        .data_ask = {.entity = e, .target = target, .memKey = key},
    };
  }
  return script_null();
}

static ScriptVal eval_spawn(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const StringHash prefabId = script_arg_str(args, 0, err);
  if (UNLIKELY(!prefabId)) {
    return script_null(); // Invalid prefab-id.
  }
  const EcsEntityId result                     = ecs_world_entity_create(ctx->world);
  *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
      .type = ScriptActionType_Spawn,
      .data_spawn =
          {
              .entity   = result,
              .prefabId = prefabId,
              .position = script_arg_opt_vec3(args, 1, geo_vector(0), err),
              .rotation = script_arg_opt_quat(args, 2, geo_quat_ident, err),
              .scale    = (f32)script_arg_opt_num_range(args, 3, 0.001, 1000.0, 1.0, err),
              .faction = script_arg_opt_enum(args, 4, &g_scriptEnumFaction, SceneFaction_None, err),

          },
  };
  return script_entity(result);
}

static bool eval_destroy_allowed(EvalContext* ctx, const EcsEntityId e) {
  if (UNLIKELY(ecs_world_exists(ctx->world, e) && ecs_world_has_t(ctx->world, e, AssetComp))) {
    return false; // Destroying assets is not allowed.
  }
  return true;
}

static ScriptVal eval_destroy(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (UNLIKELY(!entity)) {
    return script_null();
  }
  if (UNLIKELY(!eval_destroy_allowed(ctx, entity))) {
    *err = script_error_arg(ScriptError_ArgumentInvalid, 0);
    return script_null();
  }
  if (ecs_world_exists(ctx->world, entity)) {
    ecs_world_entity_destroy(ctx->world, entity);
  }
  return script_null();
}

static ScriptVal eval_destroy_after(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (UNLIKELY(!entity)) {
    return script_null();
  }
  if (UNLIKELY(!eval_destroy_allowed(ctx, entity))) {
    *err = script_error_arg(ScriptError_ArgumentInvalid, 0);
    return script_null();
  }
  if (UNLIKELY(!script_arg_check(args, 1, script_mask_entity | script_mask_time, err))) {
    return script_null();
  }
  const EcsEntityId  owner = script_arg_maybe_entity(args, 1, 0);
  const TimeDuration delay = script_arg_maybe_time(args, 1, 0);
  if (ecs_world_exists(ctx->world, entity)) {
    if (owner) {
      ecs_world_add_t(ctx->world, entity, SceneLifetimeOwnerComp, .owners[0] = owner);
    } else {
      ecs_world_add_t(ctx->world, entity, SceneLifetimeDurationComp, .duration = delay);
    }
  }
  return script_null();
}

static ScriptVal eval_teleport(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (LIKELY(entity)) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type = ScriptActionType_Teleport,
        .data_teleport =
            {
                .entity   = entity,
                .position = script_arg_opt_vec3(args, 1, geo_vector(0), err),
                .rotation = script_arg_opt_quat(args, 2, geo_quat_ident, err),
            },
    };
  }
  return script_null();
}

static ScriptVal eval_nav_travel(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity     = script_arg_entity(args, 0, err);
  const ScriptMask  targetMask = script_mask_entity | script_mask_vec3;
  if (LIKELY(entity && script_arg_check(args, 1, targetMask, err))) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type = ScriptActionType_NavTravel,
        .data_navTravel =
            {
                .entity         = entity,
                .targetEntity   = script_arg_maybe_entity(args, 1, ecs_entity_invalid),
                .targetPosition = script_arg_maybe_vec3(args, 1, geo_vector(0)),
            },
    };
  }
  return script_null();
}

static ScriptVal eval_nav_stop(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (LIKELY(entity)) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type         = ScriptActionType_NavStop,
        .data_navStop = {.entity = entity},
    };
  }
  return script_null();
}

static bool eval_attach_allowed(EvalContext* ctx, const EcsEntityId e) {
  if (UNLIKELY(ecs_world_exists(ctx->world, e) && ecs_world_has_t(ctx->world, e, AssetComp))) {
    return false; // Attaching assets is not allowed.
  }
  return true;
}

static ScriptVal eval_attach(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (UNLIKELY(!entity)) {
    return script_null();
  }
  if (UNLIKELY(!eval_attach_allowed(ctx, entity))) {
    *err = script_error_arg(ScriptError_ArgumentInvalid, 0);
    return script_null();
  }
  const EcsEntityId target = script_arg_entity(args, 1, err);
  if (UNLIKELY(!target)) {
    return script_null();
  }
  *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
      .type = ScriptActionType_Attach,
      .data_attach =
          {
              .entity    = entity,
              .target    = target,
              .jointName = script_arg_opt_str(args, 2, 0, err),
          },
  };
  return script_null();
}

static ScriptVal eval_detach(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (LIKELY(entity)) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type        = ScriptActionType_Detach,
        .data_detach = {.entity = entity},
    };
  }
  return script_null();
}

static ScriptVal eval_damage(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  const f32         amount = (f32)script_arg_num_range(args, 1, 1.0, 10000.0, err);
  if (LIKELY(entity) && amount > f32_epsilon) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type        = ScriptActionType_Damage,
        .data_damage = {.entity = entity, .amount = amount},
    };
  }
  return script_null();
}

static ScriptVal eval_attack(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity     = script_arg_entity(args, 0, err);
  const ScriptMask  targetMask = script_mask_entity | script_mask_null;
  const EcsEntityId target     = script_arg_maybe_entity(args, 1, ecs_entity_invalid);
  if (LIKELY(entity && script_arg_check(args, 1, targetMask, err))) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type        = ScriptActionType_Attack,
        .data_attack = {.entity = entity, .target = target},
    };
  }
  return script_null();
}

static ScriptVal eval_bark(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId   entity = script_arg_entity(args, 0, err);
  const SceneBarkType type   = (SceneBarkType)script_arg_enum(args, 1, &g_scriptEnumBark, err);
  if (LIKELY(!script_error_valid(err))) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type      = ScriptActionType_Bark,
        .data_bark = {.entity = entity, .type = type},
    };
  }
  return script_null();
}

static bool eval_status_allowed(EvalContext* ctx, const EcsEntityId e) {
  if (UNLIKELY(ecs_world_exists(ctx->world, e) && ecs_world_has_t(ctx->world, e, AssetComp))) {
    return false; // Assets are not allowed to have status effects
  }
  return true;
}

static ScriptVal eval_status(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (UNLIKELY(!entity)) {
    return script_null();
  }
  if (UNLIKELY(!eval_status_allowed(ctx, entity))) {
    *err = script_error_arg(ScriptError_ArgumentInvalid, 0);
    return script_null();
  }
  const SceneStatusType type = (SceneStatusType)script_arg_enum(args, 1, &g_scriptEnumStatus, err);
  if (args.count < 3) {
    if (ecs_view_maybe_jump(ctx->statusItr, entity)) {
      const SceneStatusComp* statusComp = ecs_view_read_t(ctx->statusItr, SceneStatusComp);
      return script_bool(scene_status_active(statusComp, type));
    }
    return script_null();
  }
  if (script_arg_bool(args, 2, err)) {
    scene_status_add(ctx->world, entity, type, ctx->instigator);
  } else {
    scene_status_remove(ctx->world, entity, type);
  }
  return script_null();
}

static ScriptVal eval_emit(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (UNLIKELY(!entity)) {
    return script_null();
  }
  if (args.count == 1) {
    if (ecs_view_maybe_jump(ctx->tagItr, entity)) {
      const SceneTagComp* tagComp = ecs_view_read_t(ctx->tagItr, SceneTagComp);
      return script_bool((tagComp->tags & SceneTags_Emit) != 0);
    }
    return script_null();
  }
  SceneTags toEnable = 0, toDisable = 0;
  if (script_arg_bool(args, 1, err)) {
    toEnable |= SceneTags_Emit;
  } else {
    toDisable |= SceneTags_Emit;
  }
  *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
      .type            = ScriptActionType_UpdateTags,
      .data_updateTags = {.entity = entity, .toEnable = toEnable, .toDisable = toDisable},
  };
  return script_null();
}

static ScriptVal eval_vfx_system(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId asset = arg_asset(ctx, args, 0, err);
  const GeoVector   pos   = script_arg_vec3(args, 1, err);
  const GeoQuat     rot   = script_arg_quat(args, 2, err);
  const f32         alpha = (f32)script_arg_opt_num_range(args, 3, 0.0, 100.0, 1.0, err);
  if (UNLIKELY(script_error_valid(err))) {
    return script_null();
  }
  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, result, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(ctx->world, result, SceneVfxSystemComp, .asset = asset, .alpha = alpha);
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);
  return script_entity(result);
}

static ScriptVal eval_vfx_decal(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId asset = arg_asset(ctx, args, 0, err);
  const GeoVector   pos   = script_arg_vec3(args, 1, err);
  const GeoQuat     rot   = script_arg_quat(args, 2, err);
  const f32         alpha = (f32)script_arg_opt_num_range(args, 3, 0.0, 100.0, 1.0, err);
  if (UNLIKELY(script_error_valid(err))) {
    return script_null();
  }
  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, result, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(ctx->world, result, SceneVfxDecalComp, .asset = asset, .alpha = alpha);
  ecs_world_add_empty_t(ctx->world, result, SceneLevelInstanceComp);
  return script_entity(result);
}

static ScriptVal eval_vfx_param(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (UNLIKELY(!entity)) {
    return script_null();
  }
  const i32 param = script_arg_enum(args, 1, &g_scriptEnumVfxParam, err);
  if (args.count == 2) {
    if (ecs_view_maybe_jump(ctx->vfxSysItr, entity)) {
      const SceneVfxSystemComp* vfxSysComp = ecs_view_read_t(ctx->vfxSysItr, SceneVfxSystemComp);
      switch (param) {
      case 0 /* Alpha */:
        return script_num(vfxSysComp->alpha);
      }
    }
    if (ecs_view_maybe_jump(ctx->vfxDecalItr, entity)) {
      const SceneVfxDecalComp* vfxDecalComp = ecs_view_read_t(ctx->vfxDecalItr, SceneVfxDecalComp);
      switch (param) {
      case 0 /* Alpha */:
        return script_num(vfxDecalComp->alpha);
      }
    }
    return script_null();
  }
  *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
      .type = ScriptActionType_UpdateVfxParam,
      .data_updateVfxParam =
          {
              .entity = entity,
              .param  = param,
              .value  = (f32)script_arg_num_range(args, 2, 0.0, 1.0, err),
          },
  };
  return script_null();
}

static ScriptVal eval_sound_play(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId asset = arg_asset(ctx, args, 0, err);
  GeoVector         pos;
  const bool        is3d = script_arg_has(args, 1);
  if (is3d) {
    pos = script_arg_vec3(args, 1, err);
  }
  const f32  gain    = (f32)script_arg_opt_num_range(args, 2, 0.0, 10.0, 1.0, err);
  const f32  pitch   = (f32)script_arg_opt_num_range(args, 3, 0.0, 10.0, 1.0, err);
  const bool looping = script_arg_opt_bool(args, 4, false, err);
  if (UNLIKELY(script_error_valid(err))) {
    return script_null();
  }
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
  return script_entity(result);
}

static ScriptVal eval_sound_param(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  const EcsEntityId entity = script_arg_entity(args, 0, err);
  if (UNLIKELY(!entity)) {
    return script_null();
  }
  const i32 param = script_arg_enum(args, 1, &g_scriptEnumSoundParam, err);
  if (args.count == 2) {
    if (ecs_view_maybe_jump(ctx->soundItr, entity)) {
      const SceneSoundComp* soundComp = ecs_view_read_t(ctx->soundItr, SceneSoundComp);
      switch (param) {
      case 0 /* Gain */:
        return script_num(soundComp->gain);
      case 1 /* Pitch */:
        return script_num(soundComp->pitch);
      }
    }
    return script_null();
  }
  *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
      .type = ScriptActionType_UpdateSoundParam,
      .data_updateSoundParam =
          {
              .entity = entity,
              .param  = param,
              .value  = (f32)script_arg_num_range(args, 2, 0.0, 10.0, err),
          },
  };
  return script_null();
}

static ScriptVal eval_random_of(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  (void)ctx;
  /**
   * Return a random (non-null) value from the given arguments.
   * NOTE: This function arguably belongs in the language itself and not as an externally bound
   * function, unfortunately the language does not support intrinsics with variable argument counts
   * so its easier for it to live here for the time being.
   */
  ScriptVal choices[10];
  u32       choiceCount = 0;

  if (UNLIKELY(args.count > array_elems(choices))) {
    *err = script_error(ScriptError_ArgumentCountExceedsMaximum);
    return script_null();
  }

  for (u16 i = 0; i != args.count; ++i) {
    if (script_val_has(args.values[i])) {
      choices[choiceCount++] = args.values[i];
    }
  }
  return choiceCount ? choices[(u32)(choiceCount * rng_sample_f32(g_rng))] : script_null();
}

static ScriptVal eval_debug_log(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  (void)err;
  DynString buffer = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte, 1));
  for (u16 i = 0; i != args.count; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_write(args.values[i], &buffer);
  }

  log_i(
      "script: {}",
      log_param("message", fmt_text(dynstring_view(&buffer))),
      log_param("entity", fmt_int(ctx->instigator, .base = 16)),
      log_param("script", fmt_text(ctx->scriptId)));

  return script_null();
}

static ScriptVal eval_debug_line(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  SceneScriptDebugLine data;
  data.start = script_arg_vec3(args, 0, err);
  data.end   = script_arg_vec3(args, 1, err);
  data.color = script_arg_opt_color(args, 2, geo_color_white, err);
  if (LIKELY(!script_error_valid(err))) {
    *dynarray_push_t(ctx->debug, SceneScriptDebug) = (SceneScriptDebug){
        .type      = SceneScriptDebugType_Line,
        .data_line = data,
    };
  }
  return script_null();
}

static ScriptVal eval_debug_sphere(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  SceneScriptDebugSphere data;
  data.pos    = script_arg_vec3(args, 0, err);
  data.radius = (f32)script_arg_opt_num_range(args, 1, 0.01f, 100.0f, 0.25f, err);
  data.color  = script_arg_opt_color(args, 2, geo_color_white, err);
  if (LIKELY(!script_error_valid(err))) {
    *dynarray_push_t(ctx->debug, SceneScriptDebug) = (SceneScriptDebug){
        .type        = SceneScriptDebugType_Sphere,
        .data_sphere = data,
    };
  }
  return script_null();
}

static ScriptVal eval_debug_arrow(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  SceneScriptDebugArrow data;
  data.start  = script_arg_vec3(args, 0, err);
  data.end    = script_arg_vec3(args, 1, err);
  data.radius = (f32)script_arg_opt_num_range(args, 2, 0.01f, 10.0f, 0.25f, err);
  data.color  = script_arg_opt_color(args, 3, geo_color_white, err);
  if (LIKELY(!script_error_valid(err))) {
    *dynarray_push_t(ctx->debug, SceneScriptDebug) = (SceneScriptDebug){
        .type       = SceneScriptDebugType_Arrow,
        .data_arrow = data,
    };
  }
  return script_null();
}

static ScriptVal eval_debug_orientation(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  SceneScriptDebugOrientation data;
  data.pos  = script_arg_vec3(args, 0, err);
  data.rot  = script_arg_quat(args, 1, err);
  data.size = (f32)script_arg_opt_num_range(args, 2, 0.01f, 10.0f, 1.0f, err);
  if (LIKELY(!script_error_valid(err))) {
    *dynarray_push_t(ctx->debug, SceneScriptDebug) = (SceneScriptDebug){
        .type             = SceneScriptDebugType_Orientation,
        .data_orientation = data,
    };
  }
  return script_null();
}

static ScriptVal eval_debug_text(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  SceneScriptDebugText data;
  data.pos      = script_arg_vec3(args, 0, err);
  data.color    = script_arg_color(args, 1, err);
  data.fontSize = (u16)script_arg_num_range(args, 2, 6.0, 30.0, err);

  DynString buffer = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte, 1));
  for (u16 i = 3; i < args.count; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_write(args.values[i], &buffer);
  }
  if (UNLIKELY(script_error_valid(err)) || !buffer.size) {
    return script_null();
  }
  data.text = ctx->transientDup(ctx->scriptInstance, dynstring_view(&buffer), 1);
  *dynarray_push_t(ctx->debug, SceneScriptDebug) = (SceneScriptDebug){
      .type      = SceneScriptDebugType_Text,
      .data_text = data,
  };
  return script_null();
}

static ScriptVal eval_debug_trace(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  (void)err;
  DynString buffer = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte, 1));
  for (u16 i = 0; i < args.count; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_write(args.values[i], &buffer);
  }
  if (buffer.size) {
    *dynarray_push_t(ctx->debug, SceneScriptDebug) = (SceneScriptDebug){
        .type            = SceneScriptDebugType_Trace,
        .data_trace.text = ctx->transientDup(ctx->scriptInstance, dynstring_view(&buffer), 1),
    };
  }
  return script_null();
}

static ScriptVal eval_debug_break(EvalContext* ctx, const ScriptArgs args, ScriptError* err) {
  (void)ctx;
  (void)args;
  (void)err;
  diag_break();
  return script_null();
}

static ScriptBinder* g_scriptBinder;

typedef ScriptVal (*SceneScriptBinderFunc)(EvalContext* ctx, ScriptArgs, ScriptError* err);

static void eval_bind(ScriptBinder* b, const String name, SceneScriptBinderFunc f) {
  const ScriptSig* nullSig       = null;
  const String     documentation = string_empty;
  // NOTE: Func pointer cast is needed to type-erase the context type.
  script_binder_declare(b, name, documentation, nullSig, (ScriptBinderFunc)f);
}

static void eval_binder_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_scriptBinder)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_scriptBinder) {
    ScriptBinder* b = script_binder_create(g_alloc_persist);

    eval_enum_init_faction();
    eval_enum_init_clock();
    eval_enum_init_nav_find();
    eval_enum_init_capability();
    eval_enum_init_activity();
    eval_enum_init_vfx_param();
    eval_enum_init_sound_param();
    eval_enum_init_layer();
    eval_enum_init_status();
    eval_enum_init_bark();

    // clang-format off
    eval_bind(b, string_lit("self"),               eval_self);
    eval_bind(b, string_lit("exists"),             eval_exists);
    eval_bind(b, string_lit("position"),           eval_position);
    eval_bind(b, string_lit("velocity"),           eval_velocity);
    eval_bind(b, string_lit("rotation"),           eval_rotation);
    eval_bind(b, string_lit("scale"),              eval_scale);
    eval_bind(b, string_lit("name"),               eval_name);
    eval_bind(b, string_lit("faction"),            eval_faction);
    eval_bind(b, string_lit("health"),             eval_health);
    eval_bind(b, string_lit("visible"),            eval_visible);
    eval_bind(b, string_lit("time"),               eval_time);
    eval_bind(b, string_lit("set"),                eval_set);
    eval_bind(b, string_lit("query_set"),          eval_query_set);
    eval_bind(b, string_lit("query_sphere"),       eval_query_sphere);
    eval_bind(b, string_lit("query_next"),         eval_query_next);
    eval_bind(b, string_lit("query_random"),       eval_query_random);
    eval_bind(b, string_lit("nav_find"),           eval_nav_find);
    eval_bind(b, string_lit("nav_target"),         eval_nav_target);
    eval_bind(b, string_lit("line_of_sight"),      eval_line_of_sight);
    eval_bind(b, string_lit("capable"),            eval_capable);
    eval_bind(b, string_lit("active"),             eval_active);
    eval_bind(b, string_lit("target_primary"),     eval_target_primary);
    eval_bind(b, string_lit("target_range_min"),   eval_target_range_min);
    eval_bind(b, string_lit("target_range_max"),   eval_target_range_max);
    eval_bind(b, string_lit("tell"),               eval_tell);
    eval_bind(b, string_lit("ask"),                eval_ask);
    eval_bind(b, string_lit("spawn"),              eval_spawn);
    eval_bind(b, string_lit("destroy"),            eval_destroy);
    eval_bind(b, string_lit("destroy_after"),      eval_destroy_after);
    eval_bind(b, string_lit("teleport"),           eval_teleport);
    eval_bind(b, string_lit("nav_travel"),         eval_nav_travel);
    eval_bind(b, string_lit("nav_stop"),           eval_nav_stop);
    eval_bind(b, string_lit("attach"),             eval_attach);
    eval_bind(b, string_lit("detach"),             eval_detach);
    eval_bind(b, string_lit("damage"),             eval_damage);
    eval_bind(b, string_lit("attack"),             eval_attack);
    eval_bind(b, string_lit("bark"),               eval_bark);
    eval_bind(b, string_lit("status"),             eval_status);
    eval_bind(b, string_lit("emit"),               eval_emit);
    eval_bind(b, string_lit("vfx_system"),         eval_vfx_system);
    eval_bind(b, string_lit("vfx_decal"),          eval_vfx_decal);
    eval_bind(b, string_lit("vfx_param"),          eval_vfx_param);
    eval_bind(b, string_lit("sound_play"),         eval_sound_play);
    eval_bind(b, string_lit("sound_param"),        eval_sound_param);
    eval_bind(b, string_lit("random_of"),          eval_random_of);
    eval_bind(b, string_lit("debug_log"),          eval_debug_log);
    eval_bind(b, string_lit("debug_line"),         eval_debug_line);
    eval_bind(b, string_lit("debug_sphere"),       eval_debug_sphere);
    eval_bind(b, string_lit("debug_arrow"),        eval_debug_arrow);
    eval_bind(b, string_lit("debug_orientation"),  eval_debug_orientation);
    eval_bind(b, string_lit("debug_text"),         eval_debug_text);
    eval_bind(b, string_lit("debug_trace"),        eval_debug_trace);
    eval_bind(b, string_lit("debug_break"),        eval_debug_break);
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

ecs_comp_define(SceneScriptComp) {
  SceneScriptFlags flags : 8;
  u8               resVersion;
  EcsEntityId      scriptAsset;
  SceneScriptStats stats;
  ScriptPanic      lastPanic;
  Allocator*       allocTransient;
  DynArray         actions; // ScriptAction[].
  DynArray         debug;   // SceneScriptDebug[].
};

ecs_comp_define(SceneScriptResourceComp) {
  SceneScriptResFlags flags : 8;
  u8                  resVersion; // NOTE: Allowed to wrap around.
};

static void ecs_destruct_script_instance(void* data) {
  SceneScriptComp* scriptInstance = data;
  if (scriptInstance->allocTransient) {
    alloc_chunked_destroy(scriptInstance->allocTransient);
  }
  dynarray_destroy(&scriptInstance->actions);
  dynarray_destroy(&scriptInstance->debug);
}

static void ecs_combine_script_resource(void* dataA, void* dataB) {
  SceneScriptResourceComp* a = dataA;
  SceneScriptResourceComp* b = dataB;
  a->flags |= b->flags;
}

ecs_view_define(ScriptUpdateView) {
  ecs_access_write(SceneScriptComp);
  ecs_access_write(SceneKnowledgeComp);
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
    EcsEntityId              entity = ecs_view_entity(itr);
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
  if (UNLIKELY(ctx->scriptInstance->flags & SceneScriptFlags_PauseEvaluation)) {
    ctx->scriptInstance->stats     = (SceneScriptStats){0};
    ctx->scriptInstance->lastPanic = (ScriptPanic){0};
    return;
  }

  const ScriptDoc* doc  = ctx->scriptAsset->doc;
  const ScriptExpr expr = ctx->scriptAsset->expr;
  ScriptMem*       mem  = scene_knowledge_memory_mut(ctx->scriptKnowledge);

  const TimeSteady startTime = time_steady_clock();

  // Eval.
  const ScriptEvalResult evalRes = script_eval(doc, mem, expr, g_scriptBinder, ctx);

  // Handle panics.
  if (UNLIKELY(script_panic_valid(&evalRes.panic))) {
    const String msg = script_panic_pretty_scratch(ctx->scriptAsset->sourceText, &evalRes.panic);
    log_e(
        "Script panic",
        log_param("panic", fmt_text(msg)),
        log_param("script", fmt_text(ctx->scriptId)),
        log_param("entity", fmt_int(ctx->instigator, .base = 16)));

    ctx->scriptInstance->flags |= SceneScriptFlags_DidPanic;
    ctx->scriptInstance->lastPanic = evalRes.panic;
  } else {
    ctx->scriptInstance->lastPanic = (ScriptPanic){0};
  }

  // Update stats.
  ctx->scriptInstance->stats.executedExprs = evalRes.executedExprs;
  ctx->scriptInstance->stats.executedDur   = time_steady_duration(startTime, time_steady_clock());
}

static Mem scene_script_transient_dup(SceneScriptComp* inst, const Mem mem, const usize align) {
  if (!inst->allocTransient) {
    const usize chunkSize = 4 * usize_kibibyte;
    inst->allocTransient  = alloc_chunked_create(g_alloc_page, alloc_bump_create, chunkSize);
  }
  return alloc_dup(inst->allocTransient, mem, align);
}

ecs_system_define(SceneScriptUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, EvalGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependency not yet initialized.
  }

  EcsView* scriptView        = ecs_world_view_t(world, ScriptUpdateView);
  EcsView* resourceAssetView = ecs_world_view_t(world, ResourceAssetView);

  EcsIterator* resourceAssetItr = ecs_view_itr(resourceAssetView);

  EvalQuery   query;
  EvalContext ctx = {
      .world          = world,
      .globalItr      = globalItr,
      .transformItr   = ecs_view_itr(ecs_world_view_t(world, EvalTransformView)),
      .veloItr        = ecs_view_itr(ecs_world_view_t(world, EvalVelocityView)),
      .scaleItr       = ecs_view_itr(ecs_world_view_t(world, EvalScaleView)),
      .nameItr        = ecs_view_itr(ecs_world_view_t(world, EvalNameView)),
      .factionItr     = ecs_view_itr(ecs_world_view_t(world, EvalFactionView)),
      .healthItr      = ecs_view_itr(ecs_world_view_t(world, EvalHealthView)),
      .statusItr      = ecs_view_itr(ecs_world_view_t(world, EvalStatusView)),
      .tagItr         = ecs_view_itr(ecs_world_view_t(world, EvalTagView)),
      .vfxSysItr      = ecs_view_itr(ecs_world_view_t(world, EvalVfxSysView)),
      .vfxDecalItr    = ecs_view_itr(ecs_world_view_t(world, EvalVfxDecalView)),
      .soundItr       = ecs_view_itr(ecs_world_view_t(world, EvalSoundView)),
      .navAgentItr    = ecs_view_itr(ecs_world_view_t(world, EvalNavAgentView)),
      .locoItr        = ecs_view_itr(ecs_world_view_t(world, EvalLocoView)),
      .attackItr      = ecs_view_itr(ecs_world_view_t(world, EvalAttackView)),
      .targetItr      = ecs_view_itr(ecs_world_view_t(world, EvalTargetView)),
      .lineOfSightItr = ecs_view_itr(ecs_world_view_t(world, EvalLineOfSightView)),
      .query          = &query,
  };

  u32 startedAssetLoads = 0;
  for (EcsIterator* itr = ecs_view_itr_step(scriptView, parCount, parIndex); ecs_view_walk(itr);) {
    ctx.instigator      = ecs_view_entity(itr);
    ctx.scriptInstance  = ecs_view_write_t(itr, SceneScriptComp);
    ctx.scriptKnowledge = ecs_view_write_t(itr, SceneKnowledgeComp);
    ctx.actions         = &ctx.scriptInstance->actions;
    ctx.debug           = &ctx.scriptInstance->debug;
    ctx.transientDup    = &scene_script_transient_dup;

    // Clear the previous frame transient data.
    if (ctx.scriptInstance->allocTransient) {
      alloc_reset(ctx.scriptInstance->allocTransient);
    }
    dynarray_clear(ctx.debug);

    // Evaluate the script if the asset is loaded.
    if (ecs_view_maybe_jump(resourceAssetItr, ctx.scriptInstance->scriptAsset)) {
      ctx.scriptAsset = ecs_view_read_t(resourceAssetItr, AssetScriptComp);
      ctx.scriptId    = asset_id(ecs_view_read_t(resourceAssetItr, AssetComp));

      const u8 resVersion = ecs_view_read_t(resourceAssetItr, SceneScriptResourceComp)->resVersion;
      if (UNLIKELY(ctx.scriptInstance->resVersion != resVersion)) {
        ctx.scriptInstance->flags &= ~SceneScriptFlags_DidPanic;
        ctx.scriptInstance->resVersion = resVersion;
      }
      scene_script_eval(&ctx);
    } else {
      // Script asset not loaded; clear any previous stats and start loading it.
      ctx.scriptInstance->stats     = (SceneScriptStats){0};
      ctx.scriptInstance->lastPanic = (ScriptPanic){0};
      if (!ecs_world_has_t(world, ctx.scriptInstance->scriptAsset, SceneScriptResourceComp)) {
        if (++startedAssetLoads < scene_script_max_asset_loads) {
          ecs_world_add_t(world, ctx.scriptInstance->scriptAsset, SceneScriptResourceComp);
        }
      }
    }
  }
}

ecs_view_define(ActionGlobalView) {
  ecs_access_write(ScenePrefabEnvComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(ActionKnowledgeView) { ecs_access_write(SceneKnowledgeComp); }
ecs_view_define(ActionTransformView) { ecs_access_write(SceneTransformComp); }
ecs_view_define(ActionNavAgentView) { ecs_access_write(SceneNavAgentComp); }
ecs_view_define(ActionAttachmentView) { ecs_access_write(SceneAttachmentComp); }
ecs_view_define(ActionDamageView) { ecs_access_write(SceneDamageComp); }
ecs_view_define(ActionAttackView) { ecs_access_write(SceneAttackComp); }
ecs_view_define(ActionBarkView) { ecs_access_write(SceneBarkComp); }
ecs_view_define(ActionTagView) { ecs_access_write(SceneTagComp); }
ecs_view_define(ActionVfxSysView) { ecs_access_write(SceneVfxSystemComp); }
ecs_view_define(ActionVfxDecalView) { ecs_access_write(SceneVfxDecalComp); }
ecs_view_define(ActionSoundView) { ecs_access_write(SceneSoundComp); }

typedef struct {
  EcsWorld*    world;
  EcsEntityId  instigator;
  EcsIterator* globalItr;
  EcsIterator* knowledgeItr;
  EcsIterator* transItr;
  EcsIterator* navAgentItr;
  EcsIterator* attachItr;
  EcsIterator* damageItr;
  EcsIterator* attackItr;
  EcsIterator* barkItr;
  EcsIterator* tagItr;
  EcsIterator* vfxSysItr;
  EcsIterator* vfxDecalItr;
  EcsIterator* soundItr;
} ActionContext;

static void action_tell(ActionContext* ctx, const ScriptActionTell* a) {
  if (ecs_view_maybe_jump(ctx->knowledgeItr, a->entity)) {
    SceneKnowledgeComp* knowledge = ecs_view_write_t(ctx->knowledgeItr, SceneKnowledgeComp);
    scene_knowledge_store(knowledge, a->memKey, a->value);
  }
}

static void action_ask(ActionContext* ctx, const ScriptActionAsk* a) {
  if (ecs_view_maybe_jump(ctx->knowledgeItr, a->entity)) {
    SceneKnowledgeComp* knowledge = ecs_view_write_t(ctx->knowledgeItr, SceneKnowledgeComp);
    if (ecs_view_maybe_jump(ctx->knowledgeItr, a->target)) {
      const SceneKnowledgeComp* target = ecs_view_read_t(ctx->knowledgeItr, SceneKnowledgeComp);
      scene_knowledge_store(knowledge, a->memKey, scene_knowledge_load(target, a->memKey));
    }
  }
}

static void action_spawn(ActionContext* ctx, const ScriptActionSpawn* a) {
  ScenePrefabEnvComp* prefabEnv = ecs_view_write_t(ctx->globalItr, ScenePrefabEnvComp);

  const ScenePrefabSpec spec = {
      .flags    = ScenePrefabFlags_Volatile, // Do not persist script-spawned prefabs.
      .prefabId = a->prefabId,
      .faction  = a->faction,
      .position = a->position,
      .rotation = a->rotation,
      .scale    = a->scale,
  };
  scene_prefab_spawn_onto(prefabEnv, &spec, a->entity);
}

static void action_teleport(ActionContext* ctx, const ScriptActionTeleport* a) {
  if (ecs_view_maybe_jump(ctx->transItr, a->entity)) {
    SceneTransformComp* trans = ecs_view_write_t(ctx->transItr, SceneTransformComp);
    trans->position           = a->position;
    trans->rotation           = a->rotation;
  }
}

static void action_nav_travel(ActionContext* ctx, const ScriptActionNavTravel* a) {
  if (ecs_view_maybe_jump(ctx->navAgentItr, a->entity)) {
    SceneNavAgentComp* agent = ecs_view_write_t(ctx->navAgentItr, SceneNavAgentComp);
    if (a->targetEntity) {
      scene_nav_travel_to_entity(agent, a->targetEntity);
    } else {
      scene_nav_travel_to(agent, a->targetPosition);
    }
  }
}

static void action_nav_stop(ActionContext* ctx, const ScriptActionNavStop* a) {
  if (ecs_view_maybe_jump(ctx->navAgentItr, a->entity)) {
    SceneNavAgentComp* agent = ecs_view_write_t(ctx->navAgentItr, SceneNavAgentComp);
    scene_nav_stop(agent);
  }
}

static void action_attach(ActionContext* ctx, const ScriptActionAttach* a) {
  SceneAttachmentComp* attach;
  if (ecs_view_maybe_jump(ctx->attachItr, a->entity)) {
    attach = ecs_view_write_t(ctx->attachItr, SceneAttachmentComp);
  } else {
    if (ecs_world_exists(ctx->world, a->entity)) {
      // TODO: Crashes if there's two attachments for the same entity in the same frame.
      attach = ecs_world_add_t(ctx->world, a->entity, SceneAttachmentComp);
    } else {
      return; // Entity does not exist.
    }
  }
  attach->target     = a->target;
  attach->jointIndex = sentinel_u32;
  if (a->jointName) {
    attach->jointName = a->jointName;
  }
}

static void action_detach(ActionContext* ctx, const ScriptActionDetach* a) {
  if (ecs_view_maybe_jump(ctx->attachItr, a->entity)) {
    ecs_view_write_t(ctx->attachItr, SceneAttachmentComp)->target = 0;
  }
}

static void action_damage(ActionContext* ctx, const ScriptActionDamage* a) {
  if (ecs_view_maybe_jump(ctx->damageItr, a->entity)) {
    SceneDamageComp* damageComp = ecs_view_write_t(ctx->damageItr, SceneDamageComp);
    scene_health_damage_add(
        damageComp,
        &(SceneDamageInfo){
            .instigator = ctx->instigator,
            .amount     = a->amount,
        });
  }
}

static void action_attack(ActionContext* ctx, const ScriptActionAttack* a) {
  if (ecs_view_maybe_jump(ctx->attackItr, a->entity)) {
    SceneAttackComp* attackComp = ecs_view_write_t(ctx->attackItr, SceneAttackComp);
    // TODO: Instead of dropping the request if we are already firing we should queue it up.
    if (!(attackComp->flags & SceneAttackFlags_Firing)) {
      attackComp->targetEntity = a->target;
    }
  }
}

static void action_bark(ActionContext* ctx, const ScriptActionBark* a) {
  if (ecs_view_maybe_jump(ctx->barkItr, a->entity)) {
    SceneBarkComp* barkComp = ecs_view_write_t(ctx->barkItr, SceneBarkComp);
    scene_bark_request(barkComp, a->type);
  }
}

static void action_update_set(ActionContext* ctx, const ScriptActionUpdateSet* a) {
  SceneSetEnvComp* setEnv = ecs_view_write_t(ctx->globalItr, SceneSetEnvComp);
  if (a->add) {
    scene_set_add(setEnv, a->set, a->entity);
  } else {
    scene_set_remove(setEnv, a->set, a->entity);
  }
}

static void action_update_tags(ActionContext* ctx, const ScriptActionUpdateTags* a) {
  if (ecs_view_maybe_jump(ctx->tagItr, a->entity)) {
    SceneTagComp* tagComp = ecs_view_write_t(ctx->tagItr, SceneTagComp);
    tagComp->tags |= a->toEnable;
    tagComp->tags &= ~a->toDisable;
  }
}

static void action_update_vfx_param(ActionContext* ctx, const ScriptActionUpdateVfxParam* a) {
  if (ecs_view_maybe_jump(ctx->vfxSysItr, a->entity)) {
    SceneVfxSystemComp* vfxSysComp = ecs_view_write_t(ctx->vfxSysItr, SceneVfxSystemComp);
    switch (a->param) {
    case 0 /* Alpha */:
      vfxSysComp->alpha = a->value;
      break;
    }
  }
  if (ecs_view_maybe_jump(ctx->vfxDecalItr, a->entity)) {
    SceneVfxDecalComp* vfxDecalComp = ecs_view_write_t(ctx->vfxDecalItr, SceneVfxDecalComp);
    switch (a->param) {
    case 0 /* Alpha */:
      vfxDecalComp->alpha = a->value;
      break;
    }
  }
}

static void action_update_sound_param(ActionContext* ctx, const ScriptActionUpdateSoundParam* a) {
  if (ecs_view_maybe_jump(ctx->soundItr, a->entity)) {
    SceneSoundComp* soundComp = ecs_view_write_t(ctx->soundItr, SceneSoundComp);
    switch (a->param) {
    case 0 /* Gain */:
      soundComp->gain = a->value;
      break;
    case 1 /* Pitch */:
      soundComp->pitch = a->value;
      break;
    }
  }
}

ecs_view_define(ScriptActionApplyView) { ecs_access_write(SceneScriptComp); }

ecs_system_define(ScriptActionApplySys) {
  EcsView*     globalView = ecs_world_view_t(world, ActionGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependency not yet initialized.
  }

  ActionContext ctx = {
      .world        = world,
      .globalItr    = globalItr,
      .knowledgeItr = ecs_view_itr(ecs_world_view_t(world, ActionKnowledgeView)),
      .transItr     = ecs_view_itr(ecs_world_view_t(world, ActionTransformView)),
      .navAgentItr  = ecs_view_itr(ecs_world_view_t(world, ActionNavAgentView)),
      .attachItr    = ecs_view_itr(ecs_world_view_t(world, ActionAttachmentView)),
      .damageItr    = ecs_view_itr(ecs_world_view_t(world, ActionDamageView)),
      .attackItr    = ecs_view_itr(ecs_world_view_t(world, ActionAttackView)),
      .barkItr      = ecs_view_itr(ecs_world_view_t(world, ActionBarkView)),
      .tagItr       = ecs_view_itr(ecs_world_view_t(world, ActionTagView)),
      .vfxSysItr    = ecs_view_itr(ecs_world_view_t(world, ActionVfxSysView)),
      .vfxDecalItr  = ecs_view_itr(ecs_world_view_t(world, ActionVfxDecalView)),
      .soundItr     = ecs_view_itr(ecs_world_view_t(world, ActionSoundView)),
  };

  EcsView* entityView = ecs_world_view_t(world, ScriptActionApplyView);
  for (EcsIterator* itr = ecs_view_itr(entityView); ecs_view_walk(itr);) {
    ctx.instigator                  = ecs_view_entity(itr);
    SceneScriptComp* scriptInstance = ecs_view_write_t(itr, SceneScriptComp);
    dynarray_for_t(&scriptInstance->actions, ScriptAction, action) {
      switch (action->type) {
      case ScriptActionType_Tell:
        action_tell(&ctx, &action->data_tell);
        break;
      case ScriptActionType_Ask:
        action_ask(&ctx, &action->data_ask);
        break;
      case ScriptActionType_Spawn:
        action_spawn(&ctx, &action->data_spawn);
        break;
      case ScriptActionType_Teleport:
        action_teleport(&ctx, &action->data_teleport);
        break;
      case ScriptActionType_NavTravel:
        action_nav_travel(&ctx, &action->data_navTravel);
        break;
      case ScriptActionType_NavStop:
        action_nav_stop(&ctx, &action->data_navStop);
        break;
      case ScriptActionType_Attach:
        action_attach(&ctx, &action->data_attach);
        break;
      case ScriptActionType_Detach:
        action_detach(&ctx, &action->data_detach);
        break;
      case ScriptActionType_Damage:
        action_damage(&ctx, &action->data_damage);
        break;
      case ScriptActionType_Attack:
        action_attack(&ctx, &action->data_attack);
        break;
      case ScriptActionType_Bark:
        action_bark(&ctx, &action->data_bark);
        break;
      case ScriptActionType_UpdateSet:
        action_update_set(&ctx, &action->data_updateSet);
        break;
      case ScriptActionType_UpdateTags:
        action_update_tags(&ctx, &action->data_updateTags);
        break;
      case ScriptActionType_UpdateVfxParam:
        action_update_vfx_param(&ctx, &action->data_updateVfxParam);
        break;
      case ScriptActionType_UpdateSoundParam:
        action_update_sound_param(&ctx, &action->data_updateSoundParam);
        break;
      }
    }
    dynarray_clear(&scriptInstance->actions);
  }
}

ecs_module_init(scene_script_module) {
  eval_binder_init();

  ecs_register_comp(SceneScriptComp, .destructor = ecs_destruct_script_instance);
  ecs_register_comp(SceneScriptResourceComp, .combinator = ecs_combine_script_resource);

  ecs_register_view(ResourceAssetView);
  ecs_register_view(ResourceLoadView);
  ecs_register_view(ScriptActionApplyView);
  ecs_register_view(ScriptUpdateView);

  ecs_register_system(SceneScriptResourceLoadSys, ecs_view_id(ResourceLoadView));
  ecs_register_system(SceneScriptResourceUnloadChangedSys, ecs_view_id(ResourceLoadView));

  ecs_register_system(
      SceneScriptUpdateSys,
      ecs_view_id(ScriptUpdateView),
      ecs_view_id(ResourceAssetView),
      ecs_register_view(EvalGlobalView),
      ecs_register_view(EvalTransformView),
      ecs_register_view(EvalVelocityView),
      ecs_register_view(EvalScaleView),
      ecs_register_view(EvalNameView),
      ecs_register_view(EvalFactionView),
      ecs_register_view(EvalHealthView),
      ecs_register_view(EvalStatusView),
      ecs_register_view(EvalTagView),
      ecs_register_view(EvalVfxSysView),
      ecs_register_view(EvalVfxDecalView),
      ecs_register_view(EvalSoundView),
      ecs_register_view(EvalNavAgentView),
      ecs_register_view(EvalLocoView),
      ecs_register_view(EvalAttackView),
      ecs_register_view(EvalTargetView),
      ecs_register_view(EvalLineOfSightView));

  ecs_order(SceneScriptUpdateSys, SceneOrder_ScriptUpdate);
  ecs_parallel(SceneScriptUpdateSys, 4);

  ecs_register_system(
      ScriptActionApplySys,
      ecs_view_id(ScriptActionApplyView),
      ecs_register_view(ActionGlobalView),
      ecs_register_view(ActionKnowledgeView),
      ecs_register_view(ActionTransformView),
      ecs_register_view(ActionNavAgentView),
      ecs_register_view(ActionAttachmentView),
      ecs_register_view(ActionDamageView),
      ecs_register_view(ActionAttackView),
      ecs_register_view(ActionBarkView),
      ecs_register_view(ActionTagView),
      ecs_register_view(ActionVfxSysView),
      ecs_register_view(ActionVfxDecalView),
      ecs_register_view(ActionSoundView));

  ecs_order(ScriptActionApplySys, SceneOrder_ScriptActionApply);
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

const ScriptPanic* scene_script_panic(const SceneScriptComp* script) {
  return script_panic_valid(&script->lastPanic) ? &script->lastPanic : null;
}

EcsEntityId scene_script_asset(const SceneScriptComp* script) { return script->scriptAsset; }

const SceneScriptStats* scene_script_stats(const SceneScriptComp* script) { return &script->stats; }

const SceneScriptDebug* scene_script_debug_data(const SceneScriptComp* script) {
  return dynarray_begin_t(&script->debug, SceneScriptDebug);
}

usize scene_script_debug_count(const SceneScriptComp* script) { return script->debug.size; }

SceneScriptComp*
scene_script_add(EcsWorld* world, const EcsEntityId entity, const EcsEntityId scriptAsset) {
  diag_assert(ecs_world_exists(world, scriptAsset));

  return ecs_world_add_t(
      world,
      entity,
      SceneScriptComp,
      .scriptAsset = scriptAsset,
      .actions     = dynarray_create_t(g_alloc_heap, ScriptAction, 0),
      .debug       = dynarray_create_t(g_alloc_heap, SceneScriptDebug, 0));
}
