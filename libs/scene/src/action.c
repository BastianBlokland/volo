#include "core/alloc.h"
#include "core/bits.h"
#include "core/diag.h"
#include "ecs/entity.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "log/logger.h"
#include "scene/action.h"
#include "scene/attachment.h"
#include "scene/attack.h"
#include "scene/health.h"
#include "scene/level.h"
#include "scene/light.h"
#include "scene/nav.h"
#include "scene/prefab.h"
#include "scene/property.h"
#include "scene/register.h"
#include "scene/renderable.h"
#include "scene/set.h"
#include "scene/skeleton.h"
#include "scene/sound.h"
#include "scene/transform.h"
#include "scene/vfx.h"

typedef u8 ActionTypeStorage;

/**
 * Queue memory layout:
 * - ScriptAction      [capacity]
 * - ActionTypeStorage [capacity]
 * - u8                [padding]
 */
ecs_comp_define(SceneActionQueueComp) {
  void* data;
  u32   count, cap;
};

static usize action_queue_mem_size(const u32 cap) {
  const usize rawSize = sizeof(SceneAction) * cap + sizeof(ActionTypeStorage) * cap;
  return bits_align(rawSize, alignof(SceneAction));
}

static Mem action_queue_types(void* memPtr, const u32 cap) {
  const usize offset = sizeof(SceneAction) * cap;
  return mem_create(bits_ptr_offset(memPtr, offset), sizeof(ActionTypeStorage) * cap);
}

static Mem action_queue_defs(void* memPtr, const u32 cap) {
  return mem_create(memPtr, sizeof(SceneAction) * cap);
}

static ActionTypeStorage* action_entry_type(void* data, const u32 cap, const u32 index) {
  return (ActionTypeStorage*)action_queue_types(data, cap).ptr + index;
}

static SceneAction* action_entry_def(void* data, const u32 capacity, const u32 index) {
  return (SceneAction*)action_queue_defs(data, capacity).ptr + index;
}

static void ecs_destruct_action_queue(void* comp) {
  SceneActionQueueComp* q = comp;
  if (q->cap) {
    alloc_free(g_allocHeap, mem_create(q->data, action_queue_mem_size(q->cap)));
  }
}

static void ecs_combine_action_queue(void* compA, void* compB) {
  SceneActionQueueComp* queueA = compA;
  SceneActionQueueComp* queueB = compB;

  if (queueB->count) {
    ActionTypeStorage* bTypes = action_queue_types(queueB->data, queueB->cap).ptr;
    SceneAction*       bDefs  = action_queue_defs(queueB->data, queueB->cap).ptr;
    for (u32 i = 0; i != queueB->count; ++i) {
      *scene_action_push(queueA, bTypes[i]) = bDefs[i];
    }
    alloc_free(g_allocHeap, mem_create(queueB->data, action_queue_mem_size(queueB->cap)));
  }
}

NO_INLINE_HINT static void action_queue_grow(SceneActionQueueComp* q) {
  const u32   newCap     = bits_nextpow2(q->cap + 1);
  const usize newMemSize = action_queue_mem_size(newCap);
  void*       newData    = alloc_alloc(g_allocHeap, newMemSize, alignof(SceneAction)).ptr;
  diag_assert_msg(newData, "Allocation failed");

  if (q->cap) {
    // Copy the action types and definitions to the new allocation.
    mem_cpy(action_queue_types(newData, newCap), action_queue_types(q->data, q->cap));
    mem_cpy(action_queue_defs(newData, newCap), action_queue_defs(q->data, q->cap));

    // Free the old allocation.
    alloc_free(g_allocHeap, mem_create(q->data, action_queue_mem_size(q->cap)));
  }

  q->data = newData;
  q->cap  = newCap;
}

ecs_view_define(ActionGlobalView) {
  ecs_access_write(ScenePrefabEnvComp);
  ecs_access_write(SceneSetEnvComp);
  ecs_access_read(SceneLevelManagerComp);
}

ecs_view_define(ActionPropertyView) { ecs_access_write(ScenePropertyComp); }
ecs_view_define(ActionTransformView) { ecs_access_write(SceneTransformComp); }
ecs_view_define(ActionNavAgentView) { ecs_access_write(SceneNavAgentComp); }
ecs_view_define(ActionAttachmentView) { ecs_access_write(SceneAttachmentComp); }
ecs_view_define(ActionHealthReqView) { ecs_access_write(SceneHealthRequestComp); }
ecs_view_define(ActionAttackView) { ecs_access_write(SceneAttackComp); }
ecs_view_define(ActionBarkView) { ecs_access_write(SceneBarkComp); }
ecs_view_define(ActionFactionView) { ecs_access_write(SceneFactionComp); }
ecs_view_define(ActionRenderableView) { ecs_access_write(SceneRenderableComp); }
ecs_view_define(ActionVfxSysView) { ecs_access_write(SceneVfxSystemComp); }
ecs_view_define(ActionVfxDecalView) { ecs_access_write(SceneVfxDecalComp); }
ecs_view_define(ActionLightPointView) { ecs_access_write(SceneLightPointComp); }
ecs_view_define(ActionLightSpotView) { ecs_access_write(SceneLightSpotComp); }
ecs_view_define(ActionLightLineView) { ecs_access_write(SceneLightLineComp); }
ecs_view_define(ActionLightDirView) { ecs_access_write(SceneLightDirComp); }
ecs_view_define(ActionSoundView) { ecs_access_write(SceneSoundComp); }
ecs_view_define(ActionAnimView) { ecs_access_write(SceneAnimationComp); }

typedef struct {
  EcsWorld*    world;
  EcsEntityId  instigator;
  EcsIterator* globalItr;
  EcsIterator* propertyItr;
  EcsIterator* transItr;
  EcsIterator* navAgentItr;
  EcsIterator* attachItr;
  EcsIterator* healthReqItr;
  EcsIterator* attackItr;
  EcsIterator* barkItr;
  EcsIterator* factionItr;
  EcsIterator* renderableItr;
  EcsIterator* vfxSysItr;
  EcsIterator* vfxDecalItr;
  EcsIterator* lightPointItr;
  EcsIterator* lightSpotItr;
  EcsIterator* lightLineItr;
  EcsIterator* lightDirItr;
  EcsIterator* soundItr;
  EcsIterator* animItr;
} ActionContext;

static u32 action_update_flag(u32 mask, const u32 flag, const bool enable) {
  if (enable) {
    mask |= flag;
  } else {
    mask &= ~flag;
  }
  return mask;
}

static void action_tell(ActionContext* ctx, const SceneActionTell* a) {
  if (ecs_view_maybe_jump(ctx->propertyItr, a->entity)) {
    ScenePropertyComp* propComp = ecs_view_write_t(ctx->propertyItr, ScenePropertyComp);
    scene_prop_store(propComp, a->prop, a->value);
  }
}

static void action_ask(ActionContext* ctx, const SceneActionAsk* a) {
  if (ecs_view_maybe_jump(ctx->propertyItr, a->entity)) {
    ScenePropertyComp* propComp = ecs_view_write_t(ctx->propertyItr, ScenePropertyComp);
    if (ecs_view_maybe_jump(ctx->propertyItr, a->target)) {
      const ScenePropertyComp* target = ecs_view_read_t(ctx->propertyItr, ScenePropertyComp);
      scene_prop_store(propComp, a->prop, scene_prop_load(target, a->prop));
    }
  }
}

static void action_spawn(ActionContext* ctx, const SceneActionSpawn* a) {
  ScenePrefabEnvComp* prefabEnv = ecs_view_write_t(ctx->globalItr, ScenePrefabEnvComp);

  const ScenePrefabSpec spec = {
      .flags    = ScenePrefabFlags_Volatile, // Do not persist action-spawned prefabs.
      .prefabId = a->prefabId,
      .faction  = a->faction,
      .position = a->position,
      .rotation = a->rotation,
      .scale    = a->scale,
  };
  scene_prefab_spawn_onto(prefabEnv, &spec, a->entity);
}

static void action_teleport(ActionContext* ctx, const SceneActionTeleport* a) {
  if (ecs_view_maybe_jump(ctx->transItr, a->entity)) {
    SceneTransformComp* trans = ecs_view_write_t(ctx->transItr, SceneTransformComp);
    trans->position           = a->position;
    trans->rotation           = a->rotation;
  }
}

static void action_nav_travel(ActionContext* ctx, const SceneActionNavTravel* a) {
  if (ecs_view_maybe_jump(ctx->navAgentItr, a->entity)) {
    SceneNavAgentComp* agent = ecs_view_write_t(ctx->navAgentItr, SceneNavAgentComp);
    if (a->targetEntity) {
      scene_nav_travel_to_entity(agent, a->targetEntity);
    } else {
      scene_nav_travel_to(agent, a->targetPosition);
    }
  }
}

static void action_nav_stop(ActionContext* ctx, const SceneActionNavStop* a) {
  if (ecs_view_maybe_jump(ctx->navAgentItr, a->entity)) {
    SceneNavAgentComp* agent = ecs_view_write_t(ctx->navAgentItr, SceneNavAgentComp);
    scene_nav_stop(agent);
  }
}

static void action_attach(ActionContext* ctx, const SceneActionAttach* a) {
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
  attach->offset = a->offset;
}

static void action_detach(ActionContext* ctx, const SceneActionDetach* a) {
  if (ecs_view_maybe_jump(ctx->attachItr, a->entity)) {
    ecs_view_write_t(ctx->attachItr, SceneAttachmentComp)->target = 0;
  }
}

static void action_health_mod(ActionContext* ctx, const SceneActionHealthMod* a) {
  if (ecs_view_maybe_jump(ctx->healthReqItr, a->entity)) {
    SceneHealthRequestComp* reqComp = ecs_view_write_t(ctx->healthReqItr, SceneHealthRequestComp);

    const SceneHealthMod mod = {.instigator = ctx->instigator, .amount = a->amount};
    scene_health_request_add(reqComp, &mod);
  }
}

static void action_attack(ActionContext* ctx, const SceneActionAttack* a) {
  if (ecs_view_maybe_jump(ctx->attackItr, a->entity)) {
    ecs_view_write_t(ctx->attackItr, SceneAttackComp)->targetDesired = a->target;
  }
}

static void action_bark(ActionContext* ctx, const SceneActionBark* a) {
  if (ecs_view_maybe_jump(ctx->barkItr, a->entity)) {
    SceneBarkComp* barkComp = ecs_view_write_t(ctx->barkItr, SceneBarkComp);
    scene_bark_request(barkComp, a->type);
  }
}

static void action_update_faction(ActionContext* ctx, const SceneActionUpdateFaction* a) {
  if (ecs_view_maybe_jump(ctx->factionItr, a->entity)) {
    ecs_view_write_t(ctx->factionItr, SceneFactionComp)->id = a->faction;
  }
}

static void action_update_set(ActionContext* ctx, const SceneActionUpdateSet* a) {
  SceneSetEnvComp* setEnv = ecs_view_write_t(ctx->globalItr, SceneSetEnvComp);
  if (a->add) {
    scene_set_add(setEnv, a->set, a->entity, SceneSetFlags_None);
  } else {
    scene_set_remove(setEnv, a->set, a->entity);
  }
}

static void
action_update_renderable_param(ActionContext* ctx, const SceneActionUpdateRenderableParam* a) {
  if (ecs_view_maybe_jump(ctx->renderableItr, a->entity)) {
    SceneRenderableComp* rendComp = ecs_view_write_t(ctx->renderableItr, SceneRenderableComp);
    switch (a->param) {
    case SceneActionRenderableParam_Color:
      rendComp->color = a->value_color;
      break;
    case SceneActionRenderableParam_Alpha:
      rendComp->color.a = a->value_num;
      break;
    case SceneActionRenderableParam_Emissive:
      rendComp->emissive = a->value_color;
      break;
    }
  }
}

static void action_update_vfx_param(ActionContext* ctx, const SceneActionUpdateVfxParam* a) {
  if (ecs_view_maybe_jump(ctx->vfxSysItr, a->entity)) {
    SceneVfxSystemComp* vfxSysComp = ecs_view_write_t(ctx->vfxSysItr, SceneVfxSystemComp);
    switch (a->param) {
    case SceneActionVfxParam_Alpha:
      vfxSysComp->alpha = a->value;
      break;
    case SceneActionVfxParam_EmitMultiplier:
      vfxSysComp->emitMultiplier = a->value;
      break;
    }
  }
  if (ecs_view_maybe_jump(ctx->vfxDecalItr, a->entity)) {
    SceneVfxDecalComp* vfxDecalComp = ecs_view_write_t(ctx->vfxDecalItr, SceneVfxDecalComp);
    switch (a->param) {
    case SceneActionVfxParam_Alpha:
      vfxDecalComp->alpha = a->value;
      break;
    default:
      break;
    }
  }
}

static void action_update_light_param(ActionContext* ctx, const SceneActionUpdateLightParam* a) {
  if (ecs_view_maybe_jump(ctx->lightPointItr, a->entity)) {
    SceneLightPointComp* pointComp = ecs_view_write_t(ctx->lightPointItr, SceneLightPointComp);
    switch (a->param) {
    case SceneActionLightParam_Radiance:
      pointComp->radiance = a->value_color;
      break;
    case SceneActionLightParam_Length:
    case SceneActionLightParam_Angle:
      break;
    }
  }
  if (ecs_view_maybe_jump(ctx->lightSpotItr, a->entity)) {
    SceneLightSpotComp* spotComp = ecs_view_write_t(ctx->lightSpotItr, SceneLightSpotComp);
    switch (a->param) {
    case SceneActionLightParam_Radiance:
      spotComp->radiance = a->value_color;
      break;
    case SceneActionLightParam_Length:
      spotComp->length = a->value_f32;
      break;
    case SceneActionLightParam_Angle:
      spotComp->angle = a->value_f32;
      break;
    }
  }
  if (ecs_view_maybe_jump(ctx->lightLineItr, a->entity)) {
    SceneLightLineComp* lineComp = ecs_view_write_t(ctx->lightLineItr, SceneLightLineComp);
    switch (a->param) {
    case SceneActionLightParam_Radiance:
      lineComp->radiance = a->value_color;
      break;
    case SceneActionLightParam_Length:
      lineComp->length = a->value_f32;
      break;
    case SceneActionLightParam_Angle:
      break;
    }
  }
  if (ecs_view_maybe_jump(ctx->lightDirItr, a->entity)) {
    SceneLightDirComp* dirComp = ecs_view_write_t(ctx->lightDirItr, SceneLightDirComp);
    switch (a->param) {
    case SceneActionLightParam_Radiance:
      dirComp->radiance = a->value_color;
      break;
    case SceneActionLightParam_Length:
    case SceneActionLightParam_Angle:
      break;
    }
  }
}

static void action_update_sound_param(ActionContext* ctx, const SceneActionUpdateSoundParam* a) {
  if (ecs_view_maybe_jump(ctx->soundItr, a->entity)) {
    SceneSoundComp* soundComp = ecs_view_write_t(ctx->soundItr, SceneSoundComp);
    switch (a->param) {
    case SceneActionSoundParam_Gain:
      soundComp->gain = a->value;
      break;
    case SceneActionSoundParam_Pitch:
      soundComp->pitch = a->value;
      break;
    }
  }
}

static void action_update_anim_param(ActionContext* ctx, const SceneActionUpdateAnimParam* a) {
  if (ecs_view_maybe_jump(ctx->animItr, a->entity)) {
    SceneAnimationComp* animComp = ecs_view_write_t(ctx->animItr, SceneAnimationComp);
    SceneAnimLayer*     layer    = scene_animation_layer_mut(animComp, a->layerName);
    if (layer) {
      switch (a->param) {
      case SceneActionAnimParam_Time:
        layer->time = a->value_f32;
        break;
      case SceneActionAnimParam_TimeNorm:
        layer->time = a->value_f32 * layer->duration;
        break;
      case SceneActionAnimParam_Speed:
        layer->speed = a->value_f32;
        break;
      case SceneActionAnimParam_Weight:
        layer->weight = a->value_f32;
        break;
      case SceneActionAnimParam_Active:
        layer->flags = action_update_flag(layer->flags, SceneAnimFlags_Active, a->value_bool);
        break;
      case SceneActionAnimParam_Loop:
        layer->flags = action_update_flag(layer->flags, SceneAnimFlags_Loop, a->value_bool);
        break;
      case SceneActionAnimParam_FadeIn:
        layer->flags = action_update_flag(layer->flags, SceneAnimFlags_AutoFadeIn, a->value_bool);
        break;
      case SceneActionAnimParam_FadeOut:
        layer->flags = action_update_flag(layer->flags, SceneAnimFlags_AutoFadeOut, a->value_bool);
        break;
      case SceneActionAnimParam_Duration:
        diag_assert_fail("Animation duration cannot be changed");
        break;
      }
    }
  }
}

ecs_view_define(ActionQueueView) { ecs_access_write(SceneActionQueueComp); }

ecs_system_define(SceneActionUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, ActionGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependency not yet initialized.
  }

  ActionContext ctx = {
      .world         = world,
      .globalItr     = globalItr,
      .propertyItr   = ecs_view_itr(ecs_world_view_t(world, ActionPropertyView)),
      .transItr      = ecs_view_itr(ecs_world_view_t(world, ActionTransformView)),
      .navAgentItr   = ecs_view_itr(ecs_world_view_t(world, ActionNavAgentView)),
      .attachItr     = ecs_view_itr(ecs_world_view_t(world, ActionAttachmentView)),
      .healthReqItr  = ecs_view_itr(ecs_world_view_t(world, ActionHealthReqView)),
      .attackItr     = ecs_view_itr(ecs_world_view_t(world, ActionAttackView)),
      .barkItr       = ecs_view_itr(ecs_world_view_t(world, ActionBarkView)),
      .factionItr    = ecs_view_itr(ecs_world_view_t(world, ActionFactionView)),
      .renderableItr = ecs_view_itr(ecs_world_view_t(world, ActionRenderableView)),
      .vfxSysItr     = ecs_view_itr(ecs_world_view_t(world, ActionVfxSysView)),
      .vfxDecalItr   = ecs_view_itr(ecs_world_view_t(world, ActionVfxDecalView)),
      .lightPointItr = ecs_view_itr(ecs_world_view_t(world, ActionLightPointView)),
      .lightSpotItr  = ecs_view_itr(ecs_world_view_t(world, ActionLightSpotView)),
      .lightLineItr  = ecs_view_itr(ecs_world_view_t(world, ActionLightLineView)),
      .lightDirItr   = ecs_view_itr(ecs_world_view_t(world, ActionLightDirView)),
      .soundItr      = ecs_view_itr(ecs_world_view_t(world, ActionSoundView)),
      .animItr       = ecs_view_itr(ecs_world_view_t(world, ActionAnimView)),
  };

  const SceneLevelManagerComp* levelManager = ecs_view_read_t(globalItr, SceneLevelManagerComp);
  const SceneLevelMode         levelMode    = scene_level_mode(levelManager);

  EcsView* entityView = ecs_world_view_t(world, ActionQueueView);
  for (EcsIterator* itr = ecs_view_itr(entityView); ecs_view_walk(itr);) {
    SceneActionQueueComp* q = ecs_view_write_t(itr, SceneActionQueueComp);
    if (!q->count) {
      continue; // Empty queue.
    }
    ctx.instigator = ecs_view_entity(itr);

    if (UNLIKELY(levelMode != SceneLevelMode_Play)) {
      log_w(
          "Unable to execute action",
          log_param("reason", fmt_text_lit("Level not open in play-mode")),
          log_param("instigator", ecs_entity_fmt(ctx.instigator)));
      continue;
    }

    ActionTypeStorage* types = action_queue_types(q->data, q->cap).ptr;
    SceneAction*       defs  = action_queue_defs(q->data, q->cap).ptr;
    for (u32 i = 0; i != q->count; ++i) {
      switch (types[i]) {
      case SceneActionType_Tell:
        action_tell(&ctx, &defs[i].tell);
        break;
      case SceneActionType_Ask:
        action_ask(&ctx, &defs[i].ask);
        break;
      case SceneActionType_Spawn:
        action_spawn(&ctx, &defs[i].spawn);
        break;
      case SceneActionType_Teleport:
        action_teleport(&ctx, &defs[i].teleport);
        break;
      case SceneActionType_NavTravel:
        action_nav_travel(&ctx, &defs[i].navTravel);
        break;
      case SceneActionType_NavStop:
        action_nav_stop(&ctx, &defs[i].navStop);
        break;
      case SceneActionType_Attach:
        action_attach(&ctx, &defs[i].attach);
        break;
      case SceneActionType_Detach:
        action_detach(&ctx, &defs[i].detach);
        break;
      case SceneActionType_HealthMod:
        action_health_mod(&ctx, &defs[i].healthMod);
        break;
      case SceneActionType_Attack:
        action_attack(&ctx, &defs[i].attack);
        break;
      case SceneActionType_Bark:
        action_bark(&ctx, &defs[i].bark);
        break;
      case SceneActionType_UpdateFaction:
        action_update_faction(&ctx, &defs[i].updateFaction);
        break;
      case SceneActionType_UpdateSet:
        action_update_set(&ctx, &defs[i].updateSet);
        break;
      case SceneActionType_UpdateRenderableParam:
        action_update_renderable_param(&ctx, &defs[i].updateRenderableParam);
        break;
      case SceneActionType_UpdateVfxParam:
        action_update_vfx_param(&ctx, &defs[i].updateVfxParam);
        break;
      case SceneActionType_UpdateLightParam:
        action_update_light_param(&ctx, &defs[i].updateLightParam);
        break;
      case SceneActionType_UpdateSoundParam:
        action_update_sound_param(&ctx, &defs[i].updateSoundParam);
        break;
      case SceneActionType_UpdateAnimParam:
        action_update_anim_param(&ctx, &defs[i].updateAnimParam);
        break;
      }
    }
    q->count = 0; // Clear queue.
  }
}

ecs_module_init(scene_action_module) {
  ecs_register_comp(
      SceneActionQueueComp,
      .destructor = ecs_destruct_action_queue,
      .combinator = ecs_combine_action_queue);

  ecs_register_system(
      SceneActionUpdateSys,
      ecs_register_view(ActionQueueView),
      ecs_register_view(ActionGlobalView),
      ecs_register_view(ActionPropertyView),
      ecs_register_view(ActionTransformView),
      ecs_register_view(ActionNavAgentView),
      ecs_register_view(ActionAttachmentView),
      ecs_register_view(ActionHealthReqView),
      ecs_register_view(ActionAttackView),
      ecs_register_view(ActionBarkView),
      ecs_register_view(ActionFactionView),
      ecs_register_view(ActionRenderableView),
      ecs_register_view(ActionVfxSysView),
      ecs_register_view(ActionVfxDecalView),
      ecs_register_view(ActionLightPointView),
      ecs_register_view(ActionLightSpotView),
      ecs_register_view(ActionLightLineView),
      ecs_register_view(ActionLightDirView),
      ecs_register_view(ActionSoundView),
      ecs_register_view(ActionAnimView));

  ecs_order(SceneActionUpdateSys, SceneOrder_ActionUpdate);
}

SceneActionQueueComp* scene_action_queue_add(EcsWorld* w, const EcsEntityId entity) {
  return ecs_world_add_t(w, entity, SceneActionQueueComp);
}

SceneAction* scene_action_push(SceneActionQueueComp* q, const SceneActionType type) {
  if (q->count == q->cap) {
    action_queue_grow(q);
  }

  ActionTypeStorage* entryType = action_entry_type(q->data, q->cap, q->count);
  SceneAction*       entryDef  = action_entry_def(q->data, q->cap, q->count);
  ++q->count;

  *entryType = (ActionTypeStorage)type;
  return entryDef;
}
