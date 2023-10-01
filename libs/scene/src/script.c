#include "asset_manager.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_knowledge.h"
#include "scene_lifetime.h"
#include "scene_name.h"
#include "scene_prefab.h"
#include "scene_register.h"
#include "scene_script.h"
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
  ScriptActionType_Attach,
  ScriptActionType_Detach,
} ScriptActionType;

typedef struct {
  EcsEntityId entity;
  StringHash  prefabId;
  f32         scale;
  GeoVector   position;
  GeoQuat     rotation;
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
  EcsEntityId target;
  StringHash  jointName;
} ScriptActionAttach;

typedef struct {
  EcsEntityId entity;
} ScriptActionDetach;

typedef struct {
  ScriptActionType type;
  union {
    ScriptActionSpawn        data_spawn;
    ScriptActionDestroy      data_destroy;
    ScriptActionDestroyAfter data_destroyAfter;
    ScriptActionTeleport     data_teleport;
    ScriptActionAttach       data_attach;
    ScriptActionDetach       data_detach;
  };
} ScriptAction;

typedef struct {
  EcsWorld*   world;
  EcsEntityId entity;
  String      scriptId;
  DynArray*   actions; // ScriptAction[].
} SceneScriptBindCtx;

static void action_push_spawn(SceneScriptBindCtx* ctx, const ScriptActionSpawn* d) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Spawn;
  a->data_spawn   = *d;
}

static void action_push_destroy(SceneScriptBindCtx* ctx, const ScriptActionDestroy* d) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Destroy;
  a->data_destroy = *d;
}

static void action_push_destroy_after(SceneScriptBindCtx* ctx, const ScriptActionDestroyAfter* d) {
  ScriptAction* a      = dynarray_push_t(ctx->actions, ScriptAction);
  a->type              = ScriptActionType_DestroyAfter;
  a->data_destroyAfter = *d;
}

static void action_push_teleport(SceneScriptBindCtx* ctx, const ScriptActionTeleport* d) {
  ScriptAction* a  = dynarray_push_t(ctx->actions, ScriptAction);
  a->type          = ScriptActionType_Teleport;
  a->data_teleport = *d;
}

static void action_push_attach(SceneScriptBindCtx* ctx, const ScriptActionAttach* d) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Attach;
  a->data_attach  = *d;
}

static void action_push_detach(SceneScriptBindCtx* ctx, const ScriptActionDetach* d) {
  ScriptAction* a = dynarray_push_t(ctx->actions, ScriptAction);
  a->type         = ScriptActionType_Detach;
  a->data_detach  = *d;
}

ecs_view_define(TransformReadView) { ecs_access_read(SceneTransformComp); }
ecs_view_define(ScaleReadView) { ecs_access_read(SceneScaleComp); }
ecs_view_define(NameReadView) { ecs_access_read(SceneNameComp); }
ecs_view_define(TimeReadView) { ecs_access_read(SceneTimeComp); }

static ScriptVal scene_script_self(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  (void)args;
  return script_entity(ctx->entity);
}

static ScriptVal scene_script_print(SceneScriptBindCtx* ctx, const ScriptArgs args) {
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

static ScriptVal scene_script_exists(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  const EcsEntityId e = script_arg_entity(args, 0, ecs_entity_invalid);
  return script_bool(e && ecs_world_exists(ctx->world, e));
}

static ScriptVal scene_script_position(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TransformReadView), e);
  return itr ? script_vector3(ecs_view_read_t(itr, SceneTransformComp)->position) : script_null();
}

static ScriptVal scene_script_rotation(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TransformReadView), e);
  return itr ? script_quat(ecs_view_read_t(itr, SceneTransformComp)->rotation) : script_null();
}

static ScriptVal scene_script_scale(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, ScaleReadView), e);
  return itr ? script_number(ecs_view_read_t(itr, SceneScaleComp)->scale) : script_null();
}

static ScriptVal scene_script_name(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  const EcsEntityId  e   = script_arg_entity(args, 0, ecs_entity_invalid);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, NameReadView), e);
  return itr ? script_string(ecs_view_read_t(itr, SceneNameComp)->name) : script_null();
}

static ScriptEnum g_scriptClockEnum;

static void scene_script_clock_enum_init() {
  script_enum_push(&g_scriptClockEnum, string_lit("Time"));
  script_enum_push(&g_scriptClockEnum, string_lit("RealTime"));
  script_enum_push(&g_scriptClockEnum, string_lit("Delta"));
  script_enum_push(&g_scriptClockEnum, string_lit("RealDelta"));
  script_enum_push(&g_scriptClockEnum, string_lit("Ticks"));
}

static ScriptVal scene_script_time(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  const EcsEntityId  g   = ecs_world_global(ctx->world);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TimeReadView), g);
  if (UNLIKELY(!itr)) {
    return script_null(); // No global time comp found.
  }
  const SceneTimeComp* time = ecs_view_read_t(itr, SceneTimeComp);
  if (!args.count) {
    return script_time(time->time);
  }
  switch (script_arg_enum(args, 0, &g_scriptClockEnum)) {
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

static ScriptVal scene_script_spawn(SceneScriptBindCtx* ctx, const ScriptArgs args) {
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
      });
  return script_entity(result);
}

static ScriptVal scene_script_destroy(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  if (entity) {
    action_push_destroy(ctx, &(ScriptActionDestroy){.entity = entity});
  }
  return script_null();
}

static ScriptVal scene_script_destroy_after(SceneScriptBindCtx* ctx, const ScriptArgs args) {
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

static ScriptVal scene_script_teleport(SceneScriptBindCtx* ctx, const ScriptArgs args) {
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

static ScriptVal scene_script_attach(SceneScriptBindCtx* ctx, const ScriptArgs args) {
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

static ScriptVal scene_script_detach(SceneScriptBindCtx* ctx, const ScriptArgs args) {
  const EcsEntityId entity = script_arg_entity(args, 0, ecs_entity_invalid);
  if (entity) {
    action_push_detach(ctx, &(ScriptActionDetach){.entity = entity});
  }
  return script_null();
}

static ScriptBinder* g_scriptBinder;

typedef ScriptVal (*SceneScriptBinderFunc)(SceneScriptBindCtx* ctx, ScriptArgs);

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

    scene_script_clock_enum_init();

    // clang-format off
    scene_script_bind(b, string_hash_lit("self"),          scene_script_self);
    scene_script_bind(b, string_hash_lit("print"),         scene_script_print);
    scene_script_bind(b, string_hash_lit("exists"),        scene_script_exists);
    scene_script_bind(b, string_hash_lit("position"),      scene_script_position);
    scene_script_bind(b, string_hash_lit("rotation"),      scene_script_rotation);
    scene_script_bind(b, string_hash_lit("scale"),         scene_script_scale);
    scene_script_bind(b, string_hash_lit("name"),          scene_script_name);
    scene_script_bind(b, string_hash_lit("time"),          scene_script_time);
    scene_script_bind(b, string_hash_lit("spawn"),         scene_script_spawn);
    scene_script_bind(b, string_hash_lit("destroy"),       scene_script_destroy);
    scene_script_bind(b, string_hash_lit("destroy_after"), scene_script_destroy_after);
    scene_script_bind(b, string_hash_lit("teleport"),      scene_script_teleport);
    scene_script_bind(b, string_hash_lit("attach"),        scene_script_attach);
    scene_script_bind(b, string_hash_lit("detach"),        scene_script_detach);
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

  SceneScriptBindCtx ctx = {
      .world    = world,
      .entity   = entity,
      .scriptId = asset_id(scriptAssetComp),
      .actions  = &scriptInstance->actions,
  };

  const ScriptEvalResult evalRes = script_eval(doc, mem, expr, g_scriptBinder, &ctx);

  if (UNLIKELY(evalRes.type != ScriptResult_Success)) {
    const String err = script_result_str(evalRes.type);
    log_w(
        "Script execution failed",
        log_param("error", fmt_text(err)),
        log_param("entity", fmt_int(entity, .base = 16)),
        log_param("script", fmt_text(asset_id(scriptAssetComp))));
  }
}

ecs_system_define(SceneScriptUpdateSys) {
  EcsView* scriptView        = ecs_world_view_t(world, ScriptUpdateView);
  EcsView* resourceAssetView = ecs_world_view_t(world, ResourceAssetView);

  EcsIterator* resourceAssetItr = ecs_view_itr(resourceAssetView);

  u32 startedAssetLoads = 0;
  for (EcsIterator* itr = ecs_view_itr_step(scriptView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId   entity         = ecs_view_entity(itr);
    SceneScriptComp*    scriptInstance = ecs_view_write_t(itr, SceneScriptComp);
    SceneKnowledgeComp* knowledge      = ecs_view_write_t(itr, SceneKnowledgeComp);

    // Evaluate the script if the asset is loaded.
    if (ecs_view_maybe_jump(resourceAssetItr, scriptInstance->scriptAsset)) {
      const AssetScriptComp* scriptAsset     = ecs_view_read_t(resourceAssetItr, AssetScriptComp);
      const AssetComp*       scriptAssetComp = ecs_view_read_t(resourceAssetItr, AssetComp);
      scene_script_eval(world, entity, scriptInstance, knowledge, scriptAsset, scriptAssetComp);
      continue;
    }

    // Otherwise start loading the asset.
    if (!ecs_world_has_t(world, scriptInstance->scriptAsset, SceneScriptResourceComp)) {
      if (++startedAssetLoads < scene_script_max_asset_loads) {
        ecs_world_add_t(world, scriptInstance->scriptAsset, SceneScriptResourceComp);
      }
    }
  }
}

ecs_view_define(ScriptActionApplyView) { ecs_access_write(SceneScriptComp); }

ecs_view_define(TransformWriteView) { ecs_access_write(SceneTransformComp); }
ecs_view_define(AttachmentWriteView) { ecs_access_write(SceneAttachmentComp); }

typedef struct {
  EcsWorld*    world;
  EcsIterator* transItr;
  EcsIterator* attachItr;
} ActionContext;

static void script_action_spawn(ActionContext* ctx, const ScriptActionSpawn* a) {
  const ScenePrefabSpec spec = {
      .prefabId = a->prefabId,
      .faction  = SceneFaction_None,
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

ecs_system_define(ScriptActionApplySys) {
  EcsIterator* transItr  = ecs_view_itr(ecs_world_view_t(world, TransformWriteView));
  EcsIterator* attachItr = ecs_view_itr(ecs_world_view_t(world, AttachmentWriteView));

  ActionContext ctx = {
      .world     = world,
      .transItr  = transItr,
      .attachItr = attachItr,
  };

  EcsView* entityView = ecs_world_view_t(world, ScriptActionApplyView);
  for (EcsIterator* itr = ecs_view_itr(entityView); ecs_view_walk(itr);) {
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
      case ScriptActionType_Attach:
        script_action_attach(&ctx, &action->data_attach);
        break;
      case ScriptActionType_Detach:
        script_action_detach(&ctx, &action->data_detach);
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
      ecs_register_view(ScriptUpdateView),
      ecs_register_view(TransformReadView),
      ecs_register_view(ScaleReadView),
      ecs_register_view(NameReadView),
      ecs_register_view(TimeReadView),
      ecs_view_id(ResourceAssetView));

  ecs_order(SceneScriptUpdateSys, SceneOrder_ScriptUpdate);
  ecs_parallel(SceneScriptUpdateSys, 4);

  ecs_register_system(
      ScriptActionApplySys,
      ecs_register_view(ScriptActionApplyView),
      ecs_register_view(TransformWriteView),
      ecs_register_view(AttachmentWriteView));

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
