#include "asset_inputmap.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_search.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataInputMapDefMeta;

typedef struct {
  AssetInputType type;
  u32            key;
  struct {
    u32*  values;
    usize count;
  } requiredModifiers;
} AssetInputBindingDef;

typedef struct {
  String name;
  struct {
    u32*  values;
    usize count;
  } blockers;
  struct {
    AssetInputBindingDef* values;
    usize                 count;
  } bindings;
} AssetInputActionDef;

typedef struct {
  struct {
    AssetInputActionDef* values;
    usize                count;
  } actions;
} AssetInputMapDef;

static void inputmap_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    /**
     * Key bindings correspond to the 'GapKey' values as defined in 'gap_input.h'.
     * NOTE: Unfortunately we cannot reference the GapKey enum directly as that would require an
     * undesired dependency on the gap library.
     * NOTE: This is a virtual data type, meaning there is no matching AssetInputKey C type.
     */
    data_reg_enum_t(g_dataReg, AssetInputKey);
    data_reg_const_custom(g_dataReg, AssetInputKey, MouseLeft,    0);
    data_reg_const_custom(g_dataReg, AssetInputKey, MouseRight,   1);
    data_reg_const_custom(g_dataReg, AssetInputKey, MouseMiddle,  2);
    data_reg_const_custom(g_dataReg, AssetInputKey, Shift,        3);
    data_reg_const_custom(g_dataReg, AssetInputKey, Control,      4);
    data_reg_const_custom(g_dataReg, AssetInputKey, Backspace,    5);
    data_reg_const_custom(g_dataReg, AssetInputKey, Delete,       6);
    data_reg_const_custom(g_dataReg, AssetInputKey, Tab,          7);
    data_reg_const_custom(g_dataReg, AssetInputKey, Tilde,        8);
    data_reg_const_custom(g_dataReg, AssetInputKey, Return,       9);
    data_reg_const_custom(g_dataReg, AssetInputKey, Escape,       10);
    data_reg_const_custom(g_dataReg, AssetInputKey, Space,        11);
    data_reg_const_custom(g_dataReg, AssetInputKey, Plus,         12);
    data_reg_const_custom(g_dataReg, AssetInputKey, Minus,        13);
    data_reg_const_custom(g_dataReg, AssetInputKey, Home,         14);
    data_reg_const_custom(g_dataReg, AssetInputKey, End,          15);
    data_reg_const_custom(g_dataReg, AssetInputKey, PageUp,       16);
    data_reg_const_custom(g_dataReg, AssetInputKey, PageDown,     17);
    data_reg_const_custom(g_dataReg, AssetInputKey, ArrowUp,      18);
    data_reg_const_custom(g_dataReg, AssetInputKey, ArrowDown,    19);
    data_reg_const_custom(g_dataReg, AssetInputKey, ArrowRight,   20);
    data_reg_const_custom(g_dataReg, AssetInputKey, ArrowLeft,    21);
    data_reg_const_custom(g_dataReg, AssetInputKey, A,            22);
    data_reg_const_custom(g_dataReg, AssetInputKey, B,            23);
    data_reg_const_custom(g_dataReg, AssetInputKey, C,            24);
    data_reg_const_custom(g_dataReg, AssetInputKey, D,            25);
    data_reg_const_custom(g_dataReg, AssetInputKey, E,            26);
    data_reg_const_custom(g_dataReg, AssetInputKey, F,            27);
    data_reg_const_custom(g_dataReg, AssetInputKey, G,            28);
    data_reg_const_custom(g_dataReg, AssetInputKey, H,            29);
    data_reg_const_custom(g_dataReg, AssetInputKey, I,            30);
    data_reg_const_custom(g_dataReg, AssetInputKey, J,            31);
    data_reg_const_custom(g_dataReg, AssetInputKey, K,            32);
    data_reg_const_custom(g_dataReg, AssetInputKey, L,            33);
    data_reg_const_custom(g_dataReg, AssetInputKey, M,            34);
    data_reg_const_custom(g_dataReg, AssetInputKey, N,            35);
    data_reg_const_custom(g_dataReg, AssetInputKey, O,            36);
    data_reg_const_custom(g_dataReg, AssetInputKey, P,            37);
    data_reg_const_custom(g_dataReg, AssetInputKey, Q,            38);
    data_reg_const_custom(g_dataReg, AssetInputKey, R,            39);
    data_reg_const_custom(g_dataReg, AssetInputKey, S,            40);
    data_reg_const_custom(g_dataReg, AssetInputKey, T,            41);
    data_reg_const_custom(g_dataReg, AssetInputKey, U,            42);
    data_reg_const_custom(g_dataReg, AssetInputKey, V,            43);
    data_reg_const_custom(g_dataReg, AssetInputKey, W,            44);
    data_reg_const_custom(g_dataReg, AssetInputKey, X,            45);
    data_reg_const_custom(g_dataReg, AssetInputKey, Y,            46);
    data_reg_const_custom(g_dataReg, AssetInputKey, Z,            47);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha0,       48);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha1,       49);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha2,       50);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha3,       51);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha4,       52);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha5,       53);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha6,       54);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha7,       55);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha8,       56);
    data_reg_const_custom(g_dataReg, AssetInputKey, Alpha9,       57);
    data_reg_const_custom(g_dataReg, AssetInputKey, F1,           58);
    data_reg_const_custom(g_dataReg, AssetInputKey, F2,           59);
    data_reg_const_custom(g_dataReg, AssetInputKey, F3,           60);
    data_reg_const_custom(g_dataReg, AssetInputKey, F4,           61);
    data_reg_const_custom(g_dataReg, AssetInputKey, F5,           62);
    data_reg_const_custom(g_dataReg, AssetInputKey, F6,           63);
    data_reg_const_custom(g_dataReg, AssetInputKey, F7,           64);
    data_reg_const_custom(g_dataReg, AssetInputKey, F8,           65);
    data_reg_const_custom(g_dataReg, AssetInputKey, F9,           66);
    data_reg_const_custom(g_dataReg, AssetInputKey, F10,          67);
    data_reg_const_custom(g_dataReg, AssetInputKey, F11,          68);
    data_reg_const_custom(g_dataReg, AssetInputKey, F12,          69);

    /**
     * Blockers correspond to the 'InputBlocker' values as defined in 'input_manager.h'.
     * NOTE: This is a virtual data type, meaning there is no matching AssetInputBlocker C type.
     */
    data_reg_enum_t(g_dataReg, AssetInputBlocker);
    data_reg_const_custom(g_dataReg, AssetInputBlocker, TextInput, 0);
    data_reg_const_custom(g_dataReg, AssetInputBlocker, HoveringUi, 1);
    data_reg_const_custom(g_dataReg, AssetInputBlocker, CursorLocked, 2);

    /**
     * Modifiers correspond to the 'InputModifier' values as defined in 'input_manager.h'.
     * NOTE: This is a virtual data type, meaning there is no matching AssetInputModifier C type.
     */
    data_reg_enum_t(g_dataReg, AssetInputModifier);
    data_reg_const_custom(g_dataReg, AssetInputModifier, Shift, 0);
    data_reg_const_custom(g_dataReg, AssetInputModifier, Control, 1);

    data_reg_enum_t(g_dataReg, AssetInputType);
    data_reg_const_t(g_dataReg, AssetInputType, Pressed);
    data_reg_const_t(g_dataReg, AssetInputType, Released);
    data_reg_const_t(g_dataReg, AssetInputType, Down);

    data_reg_struct_t(g_dataReg, AssetInputBindingDef);
    data_reg_field_t(g_dataReg, AssetInputBindingDef, type, t_AssetInputType);
    data_reg_field_t(g_dataReg, AssetInputBindingDef, key, t_AssetInputKey);
    data_reg_field_t(g_dataReg, AssetInputBindingDef, requiredModifiers, t_AssetInputModifier, .container = DataContainer_Array, .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, AssetInputActionDef);
    data_reg_field_t(g_dataReg, AssetInputActionDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetInputActionDef, blockers, t_AssetInputBlocker, .container = DataContainer_Array, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetInputActionDef, bindings, t_AssetInputBindingDef, .container = DataContainer_Array, .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetInputMapDef);
    data_reg_field_t(g_dataReg, AssetInputMapDef, actions, t_AssetInputActionDef, .container = DataContainer_Array);
    // clang-format on

    g_dataInputMapDefMeta = data_meta_t(t_AssetInputMapDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

static i8 asset_inputmap_compare_action(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetInputAction, nameHash), field_ptr(b, AssetInputAction, nameHash));
}

typedef enum {
  InputMapError_None            = 0,
  InputMapError_DuplicateAction = 1,

  InputMapError_Count,
} InputMapError;

static String inputmap_error_str(const InputMapError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple actions with the same name"),
  };
  ASSERT(array_elems(g_msgs) == InputMapError_Count, "Incorrect number of inputmap-error messages");
  return g_msgs[err];
}

static u32 asset_inputmap_blocker_bits(const AssetInputActionDef* def) {
  u32 bits = 0;
  array_ptr_for_t(def->blockers, u32, blockerIndex) { bits |= 1 << *blockerIndex; }
  return bits;
}

static u32 asset_inputmap_required_modifier_bits(const AssetInputBindingDef* def) {
  u32 bits = 0;
  array_ptr_for_t(def->requiredModifiers, u32, modifierIndex) { bits |= 1 << *modifierIndex; }
  return bits;
}

static void asset_inputmap_build(
    const AssetInputMapDef* def,
    DynArray*               outActions,  // AssetInputAction[], needs to be already initialized.
    DynArray*               outBindings, // AssetInputBinding[], needs to be already initialized.
    InputMapError*          err) {

  array_ptr_for_t(def->actions, AssetInputActionDef, actionDef) {
    const usize            bindingCount = actionDef->bindings.count;
    const AssetInputAction action       = {
              .nameHash     = stringtable_add(g_stringtable, actionDef->name),
              .blockerBits  = asset_inputmap_blocker_bits(actionDef),
              .bindingIndex = (u16)outBindings->size,
              .bindingCount = (u16)bindingCount,
    };
    if (dynarray_search_binary(outActions, asset_inputmap_compare_action, &action)) {
      *err = InputMapError_DuplicateAction;
      return;
    }
    *dynarray_insert_sorted_t(
        outActions, AssetInputAction, asset_inputmap_compare_action, &action) = action;

    array_ptr_for_t(actionDef->bindings, AssetInputBindingDef, bindingDef) {
      *dynarray_push_t(outBindings, AssetInputBinding) = (AssetInputBinding){
          .type                 = bindingDef->type,
          .key                  = bindingDef->key,
          .requiredModifierBits = asset_inputmap_required_modifier_bits(bindingDef),
      };
    }
  }
  *err = InputMapError_None;
}

ecs_comp_define_public(AssetInputMapComp);

static void ecs_destruct_inputmap_comp(void* data) {
  AssetInputMapComp* comp = data;
  if (comp->actions) {
    alloc_free_array_t(g_alloc_heap, comp->actions, comp->actionCount);
  }
  if (comp->bindings) {
    alloc_free_array_t(g_alloc_heap, comp->bindings, comp->bindingCount);
  }
}

ecs_view_define(InputMapUnloadView) {
  ecs_access_with(AssetInputMapComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any inputmap-asset component for unloaded assets.
 */
ecs_system_define(InputMapUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, InputMapUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetInputMapComp);
  }
}

ecs_module_init(asset_inputmap_module) {
  inputmap_datareg_init();

  ecs_register_comp(AssetInputMapComp, .destructor = ecs_destruct_inputmap_comp);

  ecs_register_view(InputMapUnloadView);

  ecs_register_system(InputMapUnloadAssetSys, ecs_view_id(InputMapUnloadView));
}

void asset_load_imp(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  DynArray actions  = dynarray_create_t(g_alloc_heap, AssetInputAction, 64);
  DynArray bindings = dynarray_create_t(g_alloc_heap, AssetInputBinding, 128);

  AssetInputMapDef def;
  String           errMsg;
  DataReadResult   readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataInputMapDefMeta, mem_var(def), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  InputMapError buildErr;
  asset_inputmap_build(&def, &actions, &bindings, &buildErr);
  data_destroy(g_dataReg, g_alloc_heap, g_dataInputMapDefMeta, mem_var(def));
  if (buildErr) {
    errMsg = inputmap_error_str(buildErr);
    goto Error;
  }

  ecs_world_add_t(
      world,
      entity,
      AssetInputMapComp,
      .actions      = dynarray_copy_as_new(&actions, g_alloc_heap),
      .actionCount  = actions.size,
      .bindings     = dynarray_copy_as_new(&bindings, g_alloc_heap),
      .bindingCount = bindings.size);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load InputMap", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
  dynarray_destroy(&actions);
  dynarray_destroy(&bindings);
}

const AssetInputAction*
asset_inputmap_get(const AssetInputMapComp* inputMap, const StringHash nameHash) {
  return search_binary_t(
      inputMap->actions,
      inputMap->actions + inputMap->actionCount,
      AssetInputAction,
      asset_inputmap_compare_action,
      mem_struct(AssetInputAction, .nameHash = nameHash).ptr);
}
