#include "asset_manager.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_health.h"
#include "scene_knowledge.h"
#include "scene_lifetime.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_prefab.h"
#include "scene_register.h"
#include "scene_script.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "script_binder.h"
#include "script_enum.h"
#include "script_eval.h"
#include "script_mem.h"

#define scene_script_max_asset_loads 8

typedef enum {
  ScriptActionType_Spawn,
  ScriptActionType_Destroy,
  ScriptActionType_DestroyAfter,
  ScriptActionType_Teleport,
  ScriptActionType_NavTravel,
  ScriptActionType_NavStop,
  ScriptActionType_Attach,
  ScriptActionType_Detach,
  ScriptActionType_Damage,
  ScriptActionType_Attack,
} ScriptActionType;

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
} ScriptActionDestroy;

typedef struct {
  EcsEntityId  entity;
  EcsEntityId  owner; // If zero: The delay is used instead.
  TimeDuration delay;
} ScriptActionDestroyAfter;

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
  ScriptActionType type;
  union {
    ScriptActionSpawn        data_spawn;
    ScriptActionDestroy      data_destroy;
    ScriptActionDestroyAfter data_destroyAfter;
    ScriptActionTeleport     data_teleport;
    ScriptActionNavTravel    data_navTravel;
    ScriptActionNavStop      data_navStop;
    ScriptActionAttach       data_attach;
    ScriptActionDetach       data_detach;
    ScriptActionDamage       data_damage;
    ScriptActionAttack       data_attack;
  };
} ScriptAction;

typedef struct {
  EcsWorld*    world;
  EcsIterator* globalItr;
  EcsEntityId  entity;
  String       scriptId;
  DynArray*    actions; // ScriptAction[].
} UpdateContext;

static void action_push_spawn(UpdateContext* ctx, const ScriptActionSpawn* data) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Spawn;
  a->data_spawn   = *data;
}

static void action_push_destroy(UpdateContext* ctx, const ScriptActionDestroy* data) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Destroy;
  a->data_destroy = *data;
}

static void action_push_destroy_after(UpdateContext* ctx, const ScriptActionDestroyAfter* data) {
  ScriptAction* a      = dynarray_push_t(ctx->actions, ScriptAction);
  a->type              = ScriptActionType_DestroyAfter;
  a->data_destroyAfter = *data;
}

static void action_push_teleport(UpdateContext* ctx, const ScriptActionTeleport* data) {
  ScriptAction* a  = dynarray_push_t(ctx->actions, ScriptAction);
  a->type          = ScriptActionType_Teleport;
  a->data_teleport = *data;
}

static void action_push_nav_travel(UpdateContext* ctx, const ScriptActionNavTravel* data) {
  ScriptAction* a   = dynarray_push_t(ctx->actions, ScriptAction);
  a->type           = ScriptActionType_NavTravel;
  a->data_navTravel = *data;
}

static void action_push_nav_stop(UpdateContext* ctx, const ScriptActionNavStop* data) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_NavStop;
  a->data_navStop = *data;
}

static void action_push_attach(UpdateContext* ctx, const ScriptActionAttach* data) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Attach;
  a->data_attach  = *data;
}

static void action_push_detach(UpdateContext* ctx, const ScriptActionDetach* data) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Detach;
  a->data_detach  = *data;
}

static void action_push_damage(UpdateContext* ctx, const ScriptActionDamage* data) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Damage;
  a->data_damage  = *data;
}

static void action_push_attack(UpdateContext* ctx, const ScriptActionAttack* data) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Attack;
  a->data_attack  = *data;
}

ecs_view_define(GlobalReadView) {
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(TransformReadView) { ecs_access_read(SceneTransformComp); }
ecs_view_define(ScaleReadView) { ecs_access_read(SceneScaleComp); }
ecs_view_define(NameReadView) { ecs_access_read(SceneNameComp); }
ecs_view_define(FactionReadView) { ecs_access_read(SceneFactionComp); }
ecs_view_define(HealthReadView) { ecs_access_read(SceneHealthComp); }
ecs_view_define(NavAgentReadView) { ecs_access_read(SceneNavAgentComp); }
ecs_view_define(LocoReadView) { ecs_access_read(SceneLocomotionComp); }
ecs_view_define(AttackReadView) { ecs_access_read(SceneAttackComp); }
ecs_view_define(TargetReadView) { ecs_access_read(SceneTargetFinderComp); }

// clang-format off

static ScriptEnum g_scriptEnumFaction,
                  g_scriptEnumClock,
                  g_scriptEnumNavQuery,
                  g_scriptEnumCapability,
                  g_scriptEnumActivity;

// clang-format on

static void script_enum_init_faction() {
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionA"), SceneFaction_A);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionB"), SceneFaction_B);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionC"), SceneFaction_C);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionD"), SceneFaction_D);
  script_enum_push(&g_scriptEnumFaction, string_lit("FactionNone"), SceneFaction_None);
}

static void script_enum_init_clock() {
  script_enum_push(&g_scriptEnumClock, string_lit("Time"), 0);
  script_enum_push(&g_scriptEnumClock, string_lit("RealTime"), 1);
  script_enum_push(&g_scriptEnumClock, string_lit("Delta"), 2);
  script_enum_push(&g_scriptEnumClock, string_lit("RealDelta"), 3);
  script_enum_push(&g_scriptEnumClock, string_lit("Ticks"), 4);
}

static void script_enum_init_nav_query() {
  script_enum_push(&g_scriptEnumNavQuery, string_lit("ClosestCell"), 0);
  script_enum_push(&g_scriptEnumNavQuery, string_lit("UnblockedCell"), 1);
  script_enum_push(&g_scriptEnumNavQuery, string_lit("FreeCell"), 2);
}

static void script_enum_init_capability() {
  script_enum_push(&g_scriptEnumCapability, string_lit("NavTravel"), 0);
  script_enum_push(&g_scriptEnumCapability, string_lit("Attack"), 1);
}

static void script_enum_init_activity() {
  script_enum_push(&g_scriptEnumActivity, string_lit("Moving"), 0);
  script_enum_push(&g_scriptEnumActivity, string_lit("Traveling"), 0);
  script_enum_push(&g_scriptEnumActivity, string_lit("Attacking"), 1);
  script_enum_push(&g_scriptEnumActivity, string_lit("Firing"), 2);
}

static ScriptVal scene_script_self(UpdateContext* ctx, const ScriptArgs args) {
  (void)args;
  return script_entity(ctx->entity);
}

static ScriptVal scene_script_exists(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId e = script_arg_entity(args, 0, ecs_entity_invalid);
  return script_bool(e && ecs_world_exists(ctx->world, e));
}

static ScriptVal scene_script_position(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TransformReadView), e);
  return itr ? script_vector3(ecs_view_read_t(itr, SceneTransformComp)->position) : script_null();
}

static ScriptVal scene_script_rotation(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TransformReadView), e);
  return itr ? script_quat(ecs_view_read_t(itr, SceneTransformComp)->rotation) : script_null();
}

static ScriptVal scene_script_scale(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, ScaleReadView), e);
  return itr ? script_number(ecs_view_read_t(itr, SceneScaleComp)->scale) : script_null();
}

static ScriptVal scene_script_name(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, NameReadView), e);
  return itr ? script_string(ecs_view_read_t(itr, SceneNameComp)->name) : script_null();
}

static ScriptVal scene_script_faction(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, FactionReadView), e);
  if (itr) {
    const SceneFactionComp* factionComp = ecs_view_read_t(itr, SceneFactionComp);
    const StringHash factionName = script_enum_lookup_name(&g_scriptEnumFaction, factionComp->id);
    return factionName ? script_string(factionName) : script_null();
  }
  return script_null();
}

static ScriptVal scene_script_health(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, HealthReadView), e);
  if (itr) {
    const SceneHealthComp* healthComp = ecs_view_read_t(itr, SceneHealthComp);
    return script_number(scene_health_points(healthComp));
  }
  return script_null();
}

static ScriptVal scene_script_time(UpdateContext* ctx, const ScriptArgs args) {
  const SceneTimeComp* time = ecs_view_read_t(ctx->globalItr, SceneTimeComp);
  if (!args.count) {
    return script_time(time->time);
  }
  switch (script_arg_enum(args, 0, &g_scriptEnumClock, sentinel_i32)) {
  case 0:
    return script_time(time->time);
  case 1:
    return script_time(time->realTime);
  case 2:
    return script_time(time->delta);
  case 3:
    return script_time(time->realDelta);
  case 4:
    return script_number(time->ticks);
  }
  return script_null();
}

static ScriptVal scene_script_nav_query(UpdateContext* ctx, const ScriptArgs args) {
  const SceneNavEnvComp*    navEnv        = ecs_view_read_t(ctx->globalItr, SceneNavEnvComp);
  const GeoVector           pos           = script_arg_vector3(args, 0, geo_vector(0));
  GeoNavCell                cell          = scene_nav_at_position(navEnv, pos);
  const GeoNavCellContainer cellContainer = {.cells = &cell, .capacity = 1};
  if (args.count == 1) {
    return script_vector3(scene_nav_position(navEnv, cell));
  }
  switch (script_arg_enum(args, 1, &g_scriptEnumNavQuery, sentinel_i32)) {
  case 0:
    return script_vector3(scene_nav_position(navEnv, cell));
  case 1:
    scene_nav_closest_unblocked_n(navEnv, cell, cellContainer);
    return script_vector3(scene_nav_position(navEnv, cell));
  case 2:
    scene_nav_closest_free_n(navEnv, cell, cellContainer);
    return script_vector3(scene_nav_position(navEnv, cell));
  }
  return script_null();
}

static ScriptVal scene_script_capable(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId e = script_arg_entity(args, 0, ecs_entity_invalid);
  if (!e || !ecs_world_exists(ctx->world, e)) {
    return script_bool(false);
  }
  switch (script_arg_enum(args, 1, &g_scriptEnumCapability, sentinel_i32)) {
  case 0:
    return script_bool(ecs_world_has_t(ctx->world, e, SceneNavAgentComp));
  case 1:
    return script_bool(ecs_world_has_t(ctx->world, e, SceneAttackComp));
  }
  return script_null();
}

static ScriptVal scene_script_active(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId e = script_arg_entity(args, 0, ecs_entity_invalid);
  switch (script_arg_enum(args, 1, &g_scriptEnumActivity, sentinel_i32)) {
  case 0: {
    const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, LocoReadView), e);
    const SceneLocomotionComp* loco = itr ? ecs_view_read_t(itr, SceneLocomotionComp) : null;
    return script_bool(loco && (loco->flags & SceneLocomotion_Moving) != 0);
  }
  case 1: {
    const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, NavAgentReadView), e);
    const SceneNavAgentComp* agent = itr ? ecs_view_read_t(itr, SceneNavAgentComp) : null;
    return script_bool(agent && (agent->flags & SceneNavAgent_Traveling) != 0);
  }
  case 2: {
    const EcsIterator*     itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, AttackReadView), e);
    const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
    return script_bool(attack && ecs_entity_valid(attack->targetEntity));
  }
  case 3: {
    const EcsIterator*     itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, AttackReadView), e);
    const SceneAttackComp* attack = itr ? ecs_view_read_t(itr, SceneAttackComp) : null;
    return script_bool(attack && (attack->flags & SceneAttackFlags_Firing) != 0);
  }
  }
  return script_null();
}

static ScriptVal scene_script_target_primary(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TargetReadView), e);
  if (itr) {
    return script_entity(scene_target_primary(ecs_view_read_t(itr, SceneTargetFinderComp)));
  }
  return script_null();
}

static ScriptVal scene_script_target_range_min(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TargetReadView), e);
  if (itr) {
    return script_number(ecs_view_read_t(itr, SceneTargetFinderComp)->rangeMin);
  }
  return script_null();
}

static ScriptVal scene_script_target_range_max(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TargetReadView), e);
  if (itr) {
    return script_number(ecs_view_read_t(itr, SceneTargetFinderComp)->rangeMax);
  }
  return script_null();
}

static ScriptVal scene_script_spawn(UpdateContext* ctx, const ScriptArgs args) {
  const StringHash prefabId = script_arg_string(args, 0, string_hash_invalid);
  if (UNLIKELY(!prefabId)) {
    return script_null(); // Invalid prefab-id.
  }
  const EcsEntityId result = ecs_world_entity_create(ctx->world);
  action_push_spawn(
      ctx,
      &(ScriptActionSpawn){
          .entity   = result,
          .prefabId = prefabId,
          .position = script_arg_vector3(args, 1, geo_vector(0)),
          .rotation = script_arg_quat(args, 2, geo_quat_ident),
          .scale    = (f32)script_arg_number(args, 3, 1.0),
          .faction  = script_arg_enum(args, 4, &g_scriptEnumFaction, SceneFaction_None),
      });
  return script_entity(result);
}

static ScriptVal scene_script_destroy(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  if (entity) {
    action_push_destroy(ctx, &(ScriptActionDestroy){.entity = entity});
  }
  return script_null();
}

static ScriptVal scene_script_destroy_after(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  if (entity) {
    action_push_destroy_after(
        ctx,
        &(ScriptActionDestroyAfter){
            .entity = entity,
            .owner  = script_arg_entity(args, 1, 0),
            .delay  = script_arg_time(args, 1, 0),
        });
  }
  return script_null();
}

static ScriptVal scene_script_teleport(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  if (entity) {
    action_push_teleport(
        ctx,
        &(ScriptActionTeleport){
            .entity   = entity,
            .position = script_arg_vector3(args, 1, geo_vector(0)),
            .rotation = script_arg_quat(args, 2, geo_quat_ident),
        });
  }
  return script_null();
}

static ScriptVal scene_script_nav_travel(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  if (entity) {
    action_push_nav_travel(
        ctx,
        &(ScriptActionNavTravel){
            .entity         = entity,
            .targetEntity   = script_arg_entity(args, 1, ecs_entity_invalid),
            .targetPosition = script_arg_vector3(args, 1, geo_vector(0)),
        });
  }
  return script_null();
}

static ScriptVal scene_script_nav_stop(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  if (entity) {
    action_push_nav_stop(ctx, &(ScriptActionNavStop){.entity = entity});
  }
  return script_null();
}

static ScriptVal scene_script_attach(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsEntityId target = script_arg_entity(args, 1, ecs_entity_invalid);
  if (entity && target) {
    action_push_attach(
        ctx,
        &(ScriptActionAttach){
            .entity    = entity,
            .target    = target,
            .jointName = script_arg_string(args, 2, 0),
        });
  }
  return script_null();
}

static ScriptVal scene_script_detach(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  if (entity) {
    action_push_detach(ctx, &(ScriptActionDetach){.entity = entity});
  }
  return script_null();
}

static ScriptVal scene_script_damage(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  const f32         amount = (f32)script_arg_number(args, 1, 0.0f);
  if (entity && amount > f32_epsilon) {
    action_push_damage(
        ctx,
        &(ScriptActionDamage){
            .entity = entity,
            .amount = amount,
        });
  }
  return script_null();
}

static ScriptVal scene_script_attack(UpdateContext* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsEntityId target = script_arg_entity(args, 1, ecs_entity_invalid);
  if (entity) {
    action_push_attack(ctx, &(ScriptActionAttack){.entity = entity, .target = target});
  }
  return script_null();
}

static ScriptVal scene_script_debug_log(UpdateContext* ctx, const ScriptArgs args) {
  DynString buffer = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte, 1));
  for (usize i = 0; i != args.count; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_str_write(args.values[i], &buffer);
  }

  log_i(
      "script: {}",
      log_param("message", fmt_text(dynstring_view(&buffer))),
      log_param("entity", fmt_int(ctx->entity, .base = 16)),
      log_param("script", fmt_text(ctx->scriptId)));

  return script_arg_last_or_null(args);
}

static ScriptBinder* g_scriptBinder;

typedef ScriptVal (*SceneScriptBinderFunc)(UpdateContext* ctx, ScriptArgs);

static void scene_script_bind(ScriptBinder* b, const StringHash name, SceneScriptBinderFunc f) {
  // NOTE: Func pointer cast is needed to type-erase the context type.
  script_binder_declare(b, name, (ScriptBinderFunc)f);
}

static void script_binder_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_scriptBinder)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_scriptBinder) {
    ScriptBinder* b = script_binder_create(g_alloc_persist);

    script_enum_init_faction();
    script_enum_init_clock();
    script_enum_init_nav_query();
    script_enum_init_capability();
    script_enum_init_activity();

    // clang-format off
    scene_script_bind(b, string_hash_lit("self"),             scene_script_self);
    scene_script_bind(b, string_hash_lit("exists"),           scene_script_exists);
    scene_script_bind(b, string_hash_lit("position"),         scene_script_position);
    scene_script_bind(b, string_hash_lit("rotation"),         scene_script_rotation);
    scene_script_bind(b, string_hash_lit("scale"),            scene_script_scale);
    scene_script_bind(b, string_hash_lit("name"),             scene_script_name);
    scene_script_bind(b, string_hash_lit("faction"),          scene_script_faction);
    scene_script_bind(b, string_hash_lit("health"),           scene_script_health);
    scene_script_bind(b, string_hash_lit("time"),             scene_script_time);
    scene_script_bind(b, string_hash_lit("nav_query"),        scene_script_nav_query);
    scene_script_bind(b, string_hash_lit("capable"),          scene_script_capable);
    scene_script_bind(b, string_hash_lit("active"),           scene_script_active);
    scene_script_bind(b, string_hash_lit("target_primary"),   scene_script_target_primary);
    scene_script_bind(b, string_hash_lit("target_range_min"), scene_script_target_range_min);
    scene_script_bind(b, string_hash_lit("target_range_max"), scene_script_target_range_max);
    scene_script_bind(b, string_hash_lit("spawn"),            scene_script_spawn);
    scene_script_bind(b, string_hash_lit("destroy"),          scene_script_destroy);
    scene_script_bind(b, string_hash_lit("destroy_after"),    scene_script_destroy_after);
    scene_script_bind(b, string_hash_lit("teleport"),         scene_script_teleport);
    scene_script_bind(b, string_hash_lit("nav_travel"),       scene_script_nav_travel);
    scene_script_bind(b, string_hash_lit("nav_stop"),         scene_script_nav_stop);
    scene_script_bind(b, string_hash_lit("attach"),           scene_script_attach);
    scene_script_bind(b, string_hash_lit("detach"),           scene_script_detach);
    scene_script_bind(b, string_hash_lit("damage"),           scene_script_damage);
    scene_script_bind(b, string_hash_lit("attack"),           scene_script_attack);
    scene_script_bind(b, string_hash_lit("debug_log"),        scene_script_debug_log);
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
  SceneScriptFlags flags;
  EcsEntityId      scriptAsset;
  DynArray         actions; // ScriptAction[].
};

ecs_comp_define(SceneScriptResourceComp) { SceneScriptResFlags flags; };

static void ecs_destruct_script_instance(void* data) {
  SceneScriptComp* scriptInstance = data;
  dynarray_destroy(&scriptInstance->actions);
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
}

ecs_view_define(ResourceLoadView) { ecs_access_write(SceneScriptResourceComp); }

ecs_system_define(SceneScriptResourceLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, ResourceLoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    SceneScriptResourceComp* res = ecs_view_write_t(itr, SceneScriptResourceComp);

    if (!(res->flags & (SceneScriptRes_ResourceAcquired | SceneScriptRes_ResourceUnloading))) {
      asset_acquire(world, ecs_view_entity(itr));
      res->flags |= SceneScriptRes_ResourceAcquired;
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

static void scene_script_eval(
    EcsWorld*              world,
    EcsIterator*           globalItr,
    const EcsEntityId      entity,
    SceneScriptComp*       scriptInstance,
    SceneKnowledgeComp*    knowledge,
    const AssetScriptComp* scriptAsset,
    const AssetComp*       scriptAssetComp) {

  if (UNLIKELY(scriptInstance->flags & SceneScriptFlags_PauseEvaluation)) {
    return;
  }

  const ScriptDoc* doc  = scriptAsset->doc;
  const ScriptExpr expr = scriptAsset->expr;
  ScriptMem*       mem  = scene_knowledge_memory_mut(knowledge);

  UpdateContext ctx = {
      .world     = world,
      .globalItr = globalItr,
      .entity    = entity,
      .scriptId  = asset_id(scriptAssetComp),
      .actions   = &scriptInstance->actions,
  };

  const ScriptEvalResult evalRes = script_eval(doc, mem, expr, g_scriptBinder, &ctx);

  if (UNLIKELY(evalRes.error != ScriptError_None)) {
    const String err = script_error_str(evalRes.error);
    log_w(
        "Script execution failed",
        log_param("error", fmt_text(err)),
        log_param("entity", fmt_int(entity, .base = 16)),
        log_param("script", fmt_text(asset_id(scriptAssetComp))));
  }
}

ecs_system_define(SceneScriptUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalReadView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependency not yet initialized.
  }

  EcsView* scriptView        = ecs_world_view_t(world, ScriptUpdateView);
  EcsView* resourceAssetView = ecs_world_view_t(world, ResourceAssetView);

  EcsIterator* resourceAssetItr = ecs_view_itr(resourceAssetView);

  u32 startedAssetLoads = 0;
  for (EcsIterator* itr = ecs_view_itr_step(scriptView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId   entity     = ecs_view_entity(itr);
    SceneScriptComp*    scriptInst = ecs_view_write_t(itr, SceneScriptComp);
    SceneKnowledgeComp* knowledge  = ecs_view_write_t(itr, SceneKnowledgeComp);

    // Evaluate the script if the asset is loaded.
    if (ecs_view_maybe_jump(resourceAssetItr, scriptInst->scriptAsset)) {
      const AssetScriptComp* scriptAsset     = ecs_view_read_t(resourceAssetItr, AssetScriptComp);
      const AssetComp*       scriptAssetComp = ecs_view_read_t(resourceAssetItr, AssetComp);
      scene_script_eval(
          world, globalItr, entity, scriptInst, knowledge, scriptAsset, scriptAssetComp);
      continue;
    }

    // Otherwise start loading the asset.
    if (!ecs_world_has_t(world, scriptInst->scriptAsset, SceneScriptResourceComp)) {
      if (++startedAssetLoads < scene_script_max_asset_loads) {
        ecs_world_add_t(world, scriptInst->scriptAsset, SceneScriptResourceComp);
      }
    }
  }
}

ecs_view_define(ScriptActionApplyView) { ecs_access_write(SceneScriptComp); }

ecs_view_define(TransformWriteView) { ecs_access_write(SceneTransformComp); }
ecs_view_define(NavAgentWriteView) { ecs_access_write(SceneNavAgentComp); }
ecs_view_define(AttachmentWriteView) { ecs_access_write(SceneAttachmentComp); }
ecs_view_define(DamageWriteView) { ecs_access_write(SceneDamageComp); }
ecs_view_define(AttackWriteView) { ecs_access_write(SceneAttackComp); }

typedef struct {
  EcsWorld*    world;
  EcsEntityId  instigator;
  EcsIterator* transItr;
  EcsIterator* navAgentItr;
  EcsIterator* attachItr;
  EcsIterator* damageItr;
  EcsIterator* attackItr;
} ActionContext;

static void script_action_spawn(ActionContext* ctx, const ScriptActionSpawn* a) {
  const ScenePrefabSpec spec = {
      .prefabId = a->prefabId,
      .faction  = a->faction,
      .position = a->position,
      .rotation = a->rotation,
      .scale    = a->scale,
  };
  scene_prefab_spawn_onto(ctx->world, &spec, a->entity);
}

static void script_action_destroy(ActionContext* ctx, const ScriptActionDestroy* a) {
  if (ecs_world_exists(ctx->world, a->entity)) {
    ecs_world_entity_destroy(ctx->world, a->entity);
  }
}

static void script_action_destroy_after(ActionContext* ctx, const ScriptActionDestroyAfter* a) {
  if (ecs_world_exists(ctx->world, a->entity)) {
    if (a->owner) {
      ecs_world_add_t(ctx->world, a->entity, SceneLifetimeOwnerComp, .owners[0] = a->owner);
    } else {
      ecs_world_add_t(ctx->world, a->entity, SceneLifetimeDurationComp, .duration = a->delay);
    }
  }
}

static void script_action_teleport(ActionContext* ctx, const ScriptActionTeleport* a) {
  if (ecs_view_maybe_jump(ctx->transItr, a->entity)) {
    SceneTransformComp* trans = ecs_view_write_t(ctx->transItr, SceneTransformComp);
    trans->position           = a->position;
    trans->rotation           = a->rotation;
  }
}

static void script_action_nav_travel(ActionContext* ctx, const ScriptActionNavTravel* a) {
  if (ecs_view_maybe_jump(ctx->navAgentItr, a->entity)) {
    SceneNavAgentComp* agent = ecs_view_write_t(ctx->navAgentItr, SceneNavAgentComp);
    if (a->targetEntity) {
      scene_nav_travel_to_entity(agent, a->targetEntity);
    } else {
      scene_nav_travel_to(agent, a->targetPosition);
    }
  }
}

static void script_action_nav_stop(ActionContext* ctx, const ScriptActionNavStop* a) {
  if (ecs_view_maybe_jump(ctx->navAgentItr, a->entity)) {
    SceneNavAgentComp* agent = ecs_view_write_t(ctx->navAgentItr, SceneNavAgentComp);
    scene_nav_stop(agent);
  }
}

static void script_action_attach(ActionContext* ctx, const ScriptActionAttach* a) {
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
  attach->target = a->target;
  if (a->jointName) {
    attach->jointName  = a->jointName;
    attach->jointIndex = sentinel_u32;
  } else {
    attach->jointIndex = 0;
  }
}

static void script_action_detach(ActionContext* ctx, const ScriptActionDetach* a) {
  if (ecs_view_maybe_jump(ctx->attachItr, a->entity)) {
    ecs_view_write_t(ctx->attachItr, SceneAttachmentComp)->target = 0;
  }
}

static void script_action_damage(ActionContext* ctx, const ScriptActionDamage* a) {
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

static void script_action_attack(ActionContext* ctx, const ScriptActionAttack* a) {
  if (ecs_view_maybe_jump(ctx->attackItr, a->entity)) {
    SceneAttackComp* attackComp = ecs_view_write_t(ctx->attackItr, SceneAttackComp);
    // TODO: Instead of dropping the request if we are already firing we should queue it up.
    if (!(attackComp->flags & SceneAttackFlags_Firing)) {
      attackComp->targetEntity = a->target;
    }
  }
}

ecs_system_define(ScriptActionApplySys) {
  ActionContext ctx = {
      .world       = world,
      .transItr    = ecs_view_itr(ecs_world_view_t(world, TransformWriteView)),
      .navAgentItr = ecs_view_itr(ecs_world_view_t(world, NavAgentWriteView)),
      .attachItr   = ecs_view_itr(ecs_world_view_t(world, AttachmentWriteView)),
      .damageItr   = ecs_view_itr(ecs_world_view_t(world, DamageWriteView)),
      .attackItr   = ecs_view_itr(ecs_world_view_t(world, AttackWriteView)),
  };

  EcsView* entityView = ecs_world_view_t(world, ScriptActionApplyView);
  for (EcsIterator* itr = ecs_view_itr(entityView); ecs_view_walk(itr);) {
    ctx.instigator                  = ecs_view_entity(itr);
    SceneScriptComp* scriptInstance = ecs_view_write_t(itr, SceneScriptComp);
    dynarray_for_t(&scriptInstance->actions, ScriptAction, action) {
      switch (action->type) {
      case ScriptActionType_Spawn:
        script_action_spawn(&ctx, &action->data_spawn);
        break;
      case ScriptActionType_Destroy:
        script_action_destroy(&ctx, &action->data_destroy);
        break;
      case ScriptActionType_DestroyAfter:
        script_action_destroy_after(&ctx, &action->data_destroyAfter);
        break;
      case ScriptActionType_Teleport:
        script_action_teleport(&ctx, &action->data_teleport);
        break;
      case ScriptActionType_NavTravel:
        script_action_nav_travel(&ctx, &action->data_navTravel);
        break;
      case ScriptActionType_NavStop:
        script_action_nav_stop(&ctx, &action->data_navStop);
        break;
      case ScriptActionType_Attach:
        script_action_attach(&ctx, &action->data_attach);
        break;
      case ScriptActionType_Detach:
        script_action_detach(&ctx, &action->data_detach);
        break;
      case ScriptActionType_Damage:
        script_action_damage(&ctx, &action->data_damage);
        break;
      case ScriptActionType_Attack:
        script_action_attack(&ctx, &action->data_attack);
        break;
      }
    }
    dynarray_clear(&scriptInstance->actions);
  }
}

ecs_module_init(scene_script_module) {
  script_binder_init();

  ecs_register_comp(SceneScriptComp, .destructor = ecs_destruct_script_instance);
  ecs_register_comp(SceneScriptResourceComp, .combinator = ecs_combine_script_resource);

  ecs_register_view(ResourceAssetView);
  ecs_register_view(ResourceLoadView);

  ecs_register_system(SceneScriptResourceLoadSys, ecs_view_id(ResourceLoadView));
  ecs_register_system(SceneScriptResourceUnloadChangedSys, ecs_view_id(ResourceLoadView));

  ecs_register_system(
      SceneScriptUpdateSys,
      ecs_register_view(GlobalReadView),
      ecs_register_view(ScriptUpdateView),
      ecs_register_view(TransformReadView),
      ecs_register_view(ScaleReadView),
      ecs_register_view(NameReadView),
      ecs_register_view(FactionReadView),
      ecs_register_view(HealthReadView),
      ecs_register_view(NavAgentReadView),
      ecs_register_view(LocoReadView),
      ecs_register_view(AttackReadView),
      ecs_register_view(TargetReadView),
      ecs_view_id(ResourceAssetView));

  ecs_order(SceneScriptUpdateSys, SceneOrder_ScriptUpdate);
  ecs_parallel(SceneScriptUpdateSys, 4);

  ecs_register_system(
      ScriptActionApplySys,
      ecs_register_view(ScriptActionApplyView),
      ecs_register_view(TransformWriteView),
      ecs_register_view(NavAgentWriteView),
      ecs_register_view(AttachmentWriteView),
      ecs_register_view(DamageWriteView),
      ecs_register_view(AttackWriteView));

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

EcsEntityId scene_script_asset(const SceneScriptComp* script) { return script->scriptAsset; }

SceneScriptComp*
scene_script_add(EcsWorld* world, const EcsEntityId entity, const EcsEntityId scriptAsset) {
  diag_assert(ecs_world_exists(world, scriptAsset));

  return ecs_world_add_t(
      world,
      entity,
      SceneScriptComp,
      .scriptAsset = scriptAsset,
      .actions     = dynarray_create_t(g_alloc_heap, ScriptAction, 0));
}
