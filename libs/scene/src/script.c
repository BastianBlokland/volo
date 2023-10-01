#include "asset_manager.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_knowledge.h"
#include "scene_name.h"
#include "scene_prefab.h"
#include "scene_register.h"
#include "scene_script.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "script_binder.h"
#include "script_eval.h"
#include "script_mem.h"

#define scene_script_max_asset_loads 8

typedef enum {
  ScriptActionType_Teleport,
  ScriptActionType_Attach,
  ScriptActionType_Detach,
} ScriptActionType;

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
    ScriptActionTeleport data_teleport;
    ScriptActionAttach   data_attach;
    ScriptActionDetach   data_detach;
  };
} ScriptAction;

typedef struct {
  EcsWorld*   world;
  EcsEntityId entity;
  String      scriptId;
  DynArray*   actions; // ScriptAction[].
} SceneScriptBindCtx;

/**
 * The following views are used by script bindings.
 */
ecs_view_define(TransformReadView) { ecs_access_read(SceneTransformComp); }
ecs_view_define(ScaleReadView) { ecs_access_read(SceneScaleComp); }
ecs_view_define(NameReadView) { ecs_access_read(SceneNameComp); }
ecs_view_define(TimeReadView) { ecs_access_read(SceneTimeComp); }

static ScriptVal scene_script_self(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  (void)args;
  (void)argCount;
  return script_entity(ctx->entity);
}

static ScriptVal scene_script_print(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount == 0)) {
    return script_null(); // Invalid overload.
  }

  DynString buffer = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte, 1));
  for (usize i = 0; i != argCount; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_str_write(args[i], &buffer);
  }

  log_i(
      "script: {}",
      log_param("message", fmt_text(dynstring_view(&buffer))),
      log_param("entity", fmt_int(ctx->entity, .base = 16)),
      log_param("script", fmt_text(ctx->scriptId)));

  return args[argCount - 1];
}

static ScriptVal scene_script_exists(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 1)) {
    return script_null(); // Invalid overload.
  }
  const EcsEntityId e = script_get_entity(args[0], 0);
  return script_bool(ecs_world_exists(ctx->world, e));
}

static ScriptVal scene_script_position(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 1)) {
    return script_null(); // Invalid overload.
  }
  const EcsEntityId  e   = script_get_entity(args[0], 0);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TransformReadView), e);
  return itr ? script_vector3(ecs_view_read_t(itr, SceneTransformComp)->position) : script_null();
}

static ScriptVal scene_script_rotation(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 1)) {
    return script_null(); // Invalid overload.
  }
  const EcsEntityId  e   = script_get_entity(args[0], 0);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TransformReadView), e);
  return itr ? script_quat(ecs_view_read_t(itr, SceneTransformComp)->rotation) : script_null();
}

static ScriptVal scene_script_scale(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 1)) {
    return script_null(); // Invalid overload.
  }
  const EcsEntityId  e   = script_get_entity(args[0], 0);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, ScaleReadView), e);
  return itr ? script_number(ecs_view_read_t(itr, SceneScaleComp)->scale) : script_null();
}

static ScriptVal scene_script_name(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 1)) {
    return script_null(); // Invalid overload.
  }
  const EcsEntityId  e   = script_get_entity(args[0], 0);
  const EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, NameReadView), e);
  return itr ? script_string(ecs_view_read_t(itr, SceneNameComp)->name) : script_null();
}

static ScriptVal scene_script_time(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  const EcsEntityId   g   = ecs_world_global(ctx->world);
  const EcsIterator*  itr = ecs_view_maybe_at(ecs_world_view_t(ctx->world, TimeReadView), g);
  if (UNLIKELY(!itr)) {
    return script_null(); // No global time comp found.
  }
  const SceneTimeComp* time = ecs_view_read_t(itr, SceneTimeComp);
  if (argCount == 0) {
    return script_time(time->time); // Overload with 0 args.
  }
  const StringHash clock = script_get_string(args[0], 0);
  // TODO: Precompute these hashes.
  if (clock == string_hash_lit("Time")) {
    return script_time(time->time);
  }
  if (clock == string_hash_lit("RealTime")) {
    return script_time(time->realTime);
  }
  if (clock == string_hash_lit("Delta")) {
    return script_time(time->delta);
  }
  if (clock == string_hash_lit("RealDelta")) {
    return script_time(time->realDelta);
  }
  if (clock == string_hash_lit("Ticks")) {
    return script_number(time->ticks);
  }
  return script_null();
}

static ScriptVal scene_script_spawn(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 1)) {
    return script_null(); // Invalid overload.
  }
  ScenePrefabSpec spec = {.faction = SceneFaction_None};
  spec.prefabId        = script_get_string(args[0], 0);
  if (UNLIKELY(!spec.prefabId)) {
    return script_null(); // Invalid prefab-id.
  }
  if (argCount >= 2) {
    spec.position = script_get_vector3(args[1], geo_vector(0));
  }
  if (argCount >= 3) {
    spec.rotation = script_get_quat(args[2], geo_quat_ident);
  } else {
    spec.rotation = geo_quat_ident;
  }
  if (argCount >= 4) {
    spec.scale = (f32)script_get_number(args[3], 1.0);
  } else {
    spec.scale = 1.0f;
  }
  return script_entity(scene_prefab_spawn(ctx->world, &spec));
}

static ScriptVal scene_script_destroy(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 1)) {
    return script_null(); // Invalid overload.
  }
  const EcsEntityId e = script_get_entity(args[0], 0);
  if (e && ecs_world_exists(ctx->world, e)) {
    ecs_world_entity_destroy(ctx->world, e);
  }
  return script_null();
}

static ScriptVal scene_script_teleport(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 3)) {
    return script_null(); // Invalid overload.
  }
  *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
      .type = ScriptActionType_Teleport,
      .data_teleport =
          {
              .entity   = script_get_entity(args[0], 0),
              .position = script_get_vector3(args[1], geo_vector(0)),
              .rotation = script_get_quat(args[2], geo_quat_ident),
          },
  };
  return script_null();
}

static ScriptVal scene_script_attach(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 2)) {
    return script_null(); // Invalid overload.
  }
  const EcsEntityId entity = script_get_entity(args[0], 0);
  const EcsEntityId target = script_get_entity(args[1], 0);
  if (entity && target) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type = ScriptActionType_Attach,
        .data_attach =
            {
                .entity    = entity,
                .target    = target,
                .jointName = argCount >= 3 ? script_get_string(args[2], 0) : 0,
            },
    };
  }
  return script_null();
}

static ScriptVal scene_script_detach(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;
  if (UNLIKELY(argCount < 1)) {
    return script_null(); // Invalid overload.
  }
  const EcsEntityId entity = script_get_entity(args[0], 0);
  if (entity) {
    *dynarray_push_t(ctx->actions, ScriptAction) = (ScriptAction){
        .type        = ScriptActionType_Detach,
        .data_detach = {.entity = entity},
    };
  }
  return script_null();
}

static ScriptBinder* g_scriptBinder;

static void script_binder_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_scriptBinder)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_scriptBinder) {
    ScriptBinder* binder = script_binder_create(g_alloc_persist);

    script_binder_declare(binder, string_hash_lit("self"), scene_script_self);
    script_binder_declare(binder, string_hash_lit("print"), scene_script_print);
    script_binder_declare(binder, string_hash_lit("exists"), scene_script_exists);
    script_binder_declare(binder, string_hash_lit("position"), scene_script_position);
    script_binder_declare(binder, string_hash_lit("rotation"), scene_script_rotation);
    script_binder_declare(binder, string_hash_lit("scale"), scene_script_scale);
    script_binder_declare(binder, string_hash_lit("name"), scene_script_name);
    script_binder_declare(binder, string_hash_lit("time"), scene_script_time);
    script_binder_declare(binder, string_hash_lit("spawn"), scene_script_spawn);
    script_binder_declare(binder, string_hash_lit("destroy"), scene_script_destroy);
    script_binder_declare(binder, string_hash_lit("teleport"), scene_script_teleport);
    script_binder_declare(binder, string_hash_lit("attach"), scene_script_attach);
    script_binder_declare(binder, string_hash_lit("detach"), scene_script_detach);

    script_binder_finalize(binder);
    g_scriptBinder = binder;
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

ecs_system_define(ScriptActionApplySys) {
  EcsIterator* transItr  = ecs_view_itr(ecs_world_view_t(world, TransformWriteView));
  EcsIterator* attachItr = ecs_view_itr(ecs_world_view_t(world, AttachmentWriteView));

  EcsView* entityView = ecs_world_view_t(world, ScriptActionApplyView);
  for (EcsIterator* itr = ecs_view_itr(entityView); ecs_view_walk(itr);) {
    SceneScriptComp* scriptInstance = ecs_view_write_t(itr, SceneScriptComp);
    dynarray_for_t(&scriptInstance->actions, ScriptAction, action) {
      switch (action->type) {
      case ScriptActionType_Teleport: {
        const ScriptActionTeleport* data = &action->data_teleport;
        if (ecs_view_maybe_jump(transItr, data->entity)) {
          SceneTransformComp* trans = ecs_view_write_t(transItr, SceneTransformComp);
          trans->position           = data->position;
          trans->rotation           = data->rotation;
        }
      } break;
      case ScriptActionType_Attach: {
        const ScriptActionAttach* data = &action->data_attach;
        SceneAttachmentComp*      attach;
        if (ecs_view_maybe_jump(attachItr, data->entity)) {
          attach = ecs_view_write_t(attachItr, SceneAttachmentComp);
        } else {
          // TODO: Crashes if there's two attachments for the same entity in the same frame.
          attach = ecs_world_add_t(world, data->entity, SceneAttachmentComp);
        }
        attach->target = data->target;
        if (data->jointName) {
          attach->jointName  = data->jointName;
          attach->jointIndex = sentinel_u32;
        } else {
          attach->jointIndex = 0;
        }
      } break;
      case ScriptActionType_Detach: {
        const ScriptActionDetach* data = &action->data_detach;
        if (ecs_view_maybe_jump(attachItr, data->entity)) {
          ecs_view_write_t(attachItr, SceneAttachmentComp)->target = 0;
        }
      } break;
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
