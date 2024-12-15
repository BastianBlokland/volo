#include "asset_inputmap.h"
#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_search.h"
#include "core_stringtable.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

DataMeta g_assetInputDefMeta;

typedef struct {
  StringHash name;
  u32        blockers;
  HeapArray_t(AssetInputBinding) bindings;
} AssetInputActionDef;

typedef struct {
  StringHash layer;
  HeapArray_t(AssetInputActionDef) actions;
} AssetInputMapDef;

static i8 asset_inputmap_compare_action(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetInputAction, name), field_ptr(b, AssetInputAction, name));
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

static void asset_inputmap_build(
    const AssetInputMapDef* def,
    DynArray*               outActions,  // AssetInputAction[], needs to be already initialized.
    DynArray*               outBindings, // AssetInputBinding[], needs to be already initialized.
    InputMapError*          err) {

  heap_array_for_t(def->actions, AssetInputActionDef, actionDef) {
    const AssetInputBinding* bindings     = actionDef->bindings.values;
    const usize              bindingCount = actionDef->bindings.count;
    const AssetInputAction   action       = {
                .name         = actionDef->name,
                .blockers     = actionDef->blockers,
                .bindingIndex = (u16)outBindings->size,
                .bindingCount = (u16)bindingCount,
    };
    if (dynarray_search_binary(outActions, asset_inputmap_compare_action, &action)) {
      *err = InputMapError_DuplicateAction;
      return;
    }
    *dynarray_insert_sorted_t(
        outActions, AssetInputAction, asset_inputmap_compare_action, &action) = action;

    mem_cpy(
        dynarray_push(outBindings, bindingCount),
        mem_create(bindings, sizeof(AssetInputBinding) * bindingCount));
  }
  *err = InputMapError_None;
}

ecs_comp_define_public(AssetInputMapComp);

static void ecs_destruct_inputmap_comp(void* data) {
  AssetInputMapComp* comp = data;
  if (comp->actions.values) {
    alloc_free_array_t(g_allocHeap, comp->actions.values, comp->actions.count);
  }
  if (comp->bindings.values) {
    alloc_free_array_t(g_allocHeap, comp->bindings.values, comp->bindings.count);
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
  ecs_register_comp(AssetInputMapComp, .destructor = ecs_destruct_inputmap_comp);

  ecs_register_view(InputMapUnloadView);

  ecs_register_system(InputMapUnloadAssetSys, ecs_view_id(InputMapUnloadView));
}

void asset_data_init_inputmap(void) {
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
  data_reg_const_custom(g_dataReg, AssetInputKey, MouseExtra1,  3);
  data_reg_const_custom(g_dataReg, AssetInputKey, MouseExtra2,  4);
  data_reg_const_custom(g_dataReg, AssetInputKey, MouseExtra3,  5);
  data_reg_const_custom(g_dataReg, AssetInputKey, Shift,        6);
  data_reg_const_custom(g_dataReg, AssetInputKey, Control,      7);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alt,          8);
  data_reg_const_custom(g_dataReg, AssetInputKey, Backspace,    9);
  data_reg_const_custom(g_dataReg, AssetInputKey, Delete,       10);
  data_reg_const_custom(g_dataReg, AssetInputKey, Tab,          11);
  data_reg_const_custom(g_dataReg, AssetInputKey, Tilde,        12);
  data_reg_const_custom(g_dataReg, AssetInputKey, Return,       13);
  data_reg_const_custom(g_dataReg, AssetInputKey, Escape,       14);
  data_reg_const_custom(g_dataReg, AssetInputKey, Space,        15);
  data_reg_const_custom(g_dataReg, AssetInputKey, Plus,         16);
  data_reg_const_custom(g_dataReg, AssetInputKey, Minus,        17);
  data_reg_const_custom(g_dataReg, AssetInputKey, Home,         18);
  data_reg_const_custom(g_dataReg, AssetInputKey, End,          19);
  data_reg_const_custom(g_dataReg, AssetInputKey, PageUp,       20);
  data_reg_const_custom(g_dataReg, AssetInputKey, PageDown,     21);
  data_reg_const_custom(g_dataReg, AssetInputKey, ArrowUp,      22);
  data_reg_const_custom(g_dataReg, AssetInputKey, ArrowDown,    23);
  data_reg_const_custom(g_dataReg, AssetInputKey, ArrowRight,   24);
  data_reg_const_custom(g_dataReg, AssetInputKey, ArrowLeft,    25);
  data_reg_const_custom(g_dataReg, AssetInputKey, BracketLeft,  26);
  data_reg_const_custom(g_dataReg, AssetInputKey, BracketRight, 27);
  data_reg_const_custom(g_dataReg, AssetInputKey, A,            28);
  data_reg_const_custom(g_dataReg, AssetInputKey, B,            29);
  data_reg_const_custom(g_dataReg, AssetInputKey, C,            30);
  data_reg_const_custom(g_dataReg, AssetInputKey, D,            31);
  data_reg_const_custom(g_dataReg, AssetInputKey, E,            32);
  data_reg_const_custom(g_dataReg, AssetInputKey, F,            33);
  data_reg_const_custom(g_dataReg, AssetInputKey, G,            34);
  data_reg_const_custom(g_dataReg, AssetInputKey, H,            35);
  data_reg_const_custom(g_dataReg, AssetInputKey, I,            36);
  data_reg_const_custom(g_dataReg, AssetInputKey, J,            37);
  data_reg_const_custom(g_dataReg, AssetInputKey, K,            38);
  data_reg_const_custom(g_dataReg, AssetInputKey, L,            39);
  data_reg_const_custom(g_dataReg, AssetInputKey, M,            40);
  data_reg_const_custom(g_dataReg, AssetInputKey, N,            41);
  data_reg_const_custom(g_dataReg, AssetInputKey, O,            42);
  data_reg_const_custom(g_dataReg, AssetInputKey, P,            43);
  data_reg_const_custom(g_dataReg, AssetInputKey, Q,            44);
  data_reg_const_custom(g_dataReg, AssetInputKey, R,            45);
  data_reg_const_custom(g_dataReg, AssetInputKey, S,            46);
  data_reg_const_custom(g_dataReg, AssetInputKey, T,            47);
  data_reg_const_custom(g_dataReg, AssetInputKey, U,            48);
  data_reg_const_custom(g_dataReg, AssetInputKey, V,            49);
  data_reg_const_custom(g_dataReg, AssetInputKey, W,            50);
  data_reg_const_custom(g_dataReg, AssetInputKey, X,            51);
  data_reg_const_custom(g_dataReg, AssetInputKey, Y,            52);
  data_reg_const_custom(g_dataReg, AssetInputKey, Z,            53);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha0,       54);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha1,       55);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha2,       56);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha3,       57);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha4,       58);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha5,       59);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha6,       60);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha7,       61);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha8,       62);
  data_reg_const_custom(g_dataReg, AssetInputKey, Alpha9,       63);
  data_reg_const_custom(g_dataReg, AssetInputKey, F1,           64);
  data_reg_const_custom(g_dataReg, AssetInputKey, F2,           65);
  data_reg_const_custom(g_dataReg, AssetInputKey, F3,           66);
  data_reg_const_custom(g_dataReg, AssetInputKey, F4,           67);
  data_reg_const_custom(g_dataReg, AssetInputKey, F5,           68);
  data_reg_const_custom(g_dataReg, AssetInputKey, F6,           69);
  data_reg_const_custom(g_dataReg, AssetInputKey, F7,           70);
  data_reg_const_custom(g_dataReg, AssetInputKey, F8,           71);
  data_reg_const_custom(g_dataReg, AssetInputKey, F9,           72);
  data_reg_const_custom(g_dataReg, AssetInputKey, F10,          73);
  data_reg_const_custom(g_dataReg, AssetInputKey, F11,          74);
  data_reg_const_custom(g_dataReg, AssetInputKey, F12,          75);

  /**
    * Blockers correspond to the 'InputBlocker' values as defined in 'input_manager.h'.
    * NOTE: This is a virtual data type, meaning there is no matching AssetInputBlocker C type.
    */
  data_reg_enum_multi_t(g_dataReg, AssetInputBlocker);
  data_reg_const_custom(g_dataReg, AssetInputBlocker, TextInput, 1 << 0);
  data_reg_const_custom(g_dataReg, AssetInputBlocker, HoveringUi, 1 << 1);
  data_reg_const_custom(g_dataReg, AssetInputBlocker, HoveringGizmo, 1 << 2);
  data_reg_const_custom(g_dataReg, AssetInputBlocker, PrefabCreateMode, 1 << 3);
  data_reg_const_custom(g_dataReg, AssetInputBlocker, CursorLocked, 1 << 4);
  data_reg_const_custom(g_dataReg, AssetInputBlocker, CursorConfined, 1 << 5);
  data_reg_const_custom(g_dataReg, AssetInputBlocker, WindowFullscreen, 1 << 6);

  /**
    * Modifiers correspond to the 'InputModifier' values as defined in 'input_manager.h'.
    * NOTE: This is a virtual data type, meaning there is no matching AssetInputModifier C type.
    */
  data_reg_enum_multi_t(g_dataReg, AssetInputModifier);
  data_reg_const_custom(g_dataReg, AssetInputModifier, Shift, 1 << 0);
  data_reg_const_custom(g_dataReg, AssetInputModifier, Control, 1 << 1);
  data_reg_const_custom(g_dataReg, AssetInputModifier, Alt, 1 <<  2);

  data_reg_enum_t(g_dataReg, AssetInputType);
  data_reg_const_t(g_dataReg, AssetInputType, Pressed);
  data_reg_const_t(g_dataReg, AssetInputType, Released);
  data_reg_const_t(g_dataReg, AssetInputType, Down);

  data_reg_struct_t(g_dataReg, AssetInputBinding);
  data_reg_field_t(g_dataReg, AssetInputBinding, type, t_AssetInputType);
  data_reg_field_t(g_dataReg, AssetInputBinding, key, t_AssetInputKey);
  data_reg_field_t(g_dataReg, AssetInputBinding, requiredModifiers, t_AssetInputModifier, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetInputBinding, illegalModifiers, t_AssetInputModifier, .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetInputActionDef);
  data_reg_field_t(g_dataReg, AssetInputActionDef, name, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetInputActionDef, blockers, t_AssetInputBlocker, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetInputActionDef, bindings, t_AssetInputBinding, .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetInputMapDef);
  data_reg_field_t(g_dataReg, AssetInputMapDef, layer, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetInputMapDef, actions, t_AssetInputActionDef, .container = DataContainer_HeapArray);
  // clang-format on

  g_assetInputDefMeta = data_meta_t(t_AssetInputMapDef);
}

void asset_load_inputs(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  DynArray actions  = dynarray_create_t(g_allocHeap, AssetInputAction, 64);
  DynArray bindings = dynarray_create_t(g_allocHeap, AssetInputBinding, 128);

  AssetInputMapDef def;
  String           errMsg;
  DataReadResult   readRes;
  if (src->format == AssetFormat_InputsBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetInputDefMeta, mem_var(def), &readRes);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetInputDefMeta, mem_var(def), &readRes);
  }
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  if (src->format != AssetFormat_InputsBin) {
    // TODO: Instead of caching the definition it would be more optional to cache the resulting map.
    asset_cache(world, entity, g_assetInputDefMeta, mem_var(def));
  }

  InputMapError buildErr;
  asset_inputmap_build(&def, &actions, &bindings, &buildErr);
  data_destroy(g_dataReg, g_allocHeap, g_assetInputDefMeta, mem_var(def));
  if (buildErr) {
    errMsg = inputmap_error_str(buildErr);
    goto Error;
  }

  ecs_world_add_t(
      world,
      entity,
      AssetInputMapComp,
      .layer           = def.layer,
      .actions.values  = dynarray_copy_as_new(&actions, g_allocHeap),
      .actions.count   = actions.size,
      .bindings.values = dynarray_copy_as_new(&bindings, g_allocHeap),
      .bindings.count  = bindings.size);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e(
      "Failed to load InputMap",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(entity)),
      log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
  dynarray_destroy(&actions);
  dynarray_destroy(&bindings);
}

const AssetInputAction*
asset_inputmap_get(const AssetInputMapComp* inputMap, const StringHash name) {
  return search_binary_t(
      inputMap->actions.values,
      inputMap->actions.values + inputMap->actions.count,
      AssetInputAction,
      asset_inputmap_compare_action,
      mem_struct(AssetInputAction, .name = name).ptr);
}
