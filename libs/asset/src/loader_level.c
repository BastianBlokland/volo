#include "asset_level.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataLevelDefMeta;

typedef struct {
  f32 x, y, z;
} AssetLevelVec3Def;

typedef struct {
  String            prefab;
  AssetLevelFaction faction;
  AssetLevelVec3Def position;
  AssetLevelVec3Def rotation;
} AssetLevelEntryDef;

typedef struct {
  struct {
    AssetLevelEntryDef* values;
    usize               count;
  } entries;
} AssetLevelDef;

static void level_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(reg, AssetLevelVec3Def);
    data_reg_field_t(reg, AssetLevelVec3Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetLevelVec3Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetLevelVec3Def, z, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_enum_t(reg, AssetLevelFaction);
    data_reg_const_t(reg, AssetLevelFaction, A);
    data_reg_const_t(reg, AssetLevelFaction, B);
    data_reg_const_t(reg, AssetLevelFaction, C);
    data_reg_const_t(reg, AssetLevelFaction, D);
    data_reg_const_t(reg, AssetLevelFaction, None);

    data_reg_struct_t(reg, AssetLevelEntryDef);
    data_reg_field_t(reg, AssetLevelEntryDef, prefab, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetLevelEntryDef, faction, t_AssetLevelVec3Def);
    data_reg_field_t(reg, AssetLevelEntryDef, position, t_AssetLevelVec3Def);
    data_reg_field_t(reg, AssetLevelEntryDef, rotation, t_AssetLevelVec3Def);

    data_reg_struct_t(reg, AssetLevelDef);
    data_reg_field_t(reg, AssetLevelDef, entries, t_AssetLevelEntryDef, .container = DataContainer_Array);
    // clang-format on

    g_dataLevelDefMeta = data_meta_t(t_AssetLevelDef);
    g_dataReg          = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  LevelError_None                     = 0,
  LevelError_EntryPositionOutOfBounds = 1,

  LevelError_Count,
} LevelError;

static String level_error_str(const LevelError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Position of level entry is out of bounds"),
  };
  ASSERT(array_elems(g_msgs) == LevelError_Count, "Incorrect number of level-error messages");
  return g_msgs[err];
}

static GeoVector asset_level_build_position(const AssetLevelVec3Def* def) {
  return geo_vector(def->x, def->y, def->z);
}

static GeoQuat asset_level_build_rotation(const AssetLevelVec3Def* def) {
  const GeoVector eulerAnglesDeg = geo_vector(def->x, def->y, def->z);
  const GeoVector eulerAnglesRad = geo_vector_mul(eulerAnglesDeg, math_deg_to_rad);
  return geo_quat_from_euler(eulerAnglesRad);
}

static void asset_level_build(
    const AssetLevelDef* def,
    DynArray*            outEntries, // AssetLevelEntry[], needs to be already initialized.
    LevelError*          err) {

  array_ptr_for_t(def->entries, AssetLevelEntryDef, entryDef) {
    const StringHash prefabId = stringtable_add(g_stringtable, entryDef->prefab);
    const GeoVector  position = asset_level_build_position(&entryDef->position);
    const GeoQuat    rotation = asset_level_build_rotation(&entryDef->rotation);

    if (geo_vector_mag_sqr(position) > (1e4f * 1e4f)) {
      *err = LevelError_EntryPositionOutOfBounds;
      return;
    }

    *dynarray_push_t(outEntries, AssetLevelEntry) = (AssetLevelEntry){
        .prefabId = prefabId,
        .faction  = entryDef->faction,
        .position = position,
        .rotation = rotation,
    };
  }
  *err = LevelError_None;
}

ecs_comp_define_public(AssetLevelComp);

static void ecs_destruct_level_comp(void* data) {
  AssetLevelComp* comp = data;
  if (comp->entries) {
    alloc_free_array_t(g_alloc_heap, comp->entries, comp->entryCount);
  }
}

ecs_view_define(LevelUnloadView) {
  ecs_access_with(AssetLevelComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any level-asset component for unloaded assets.
 */
ecs_system_define(LevelUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, LevelUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetLevelComp);
  }
}

ecs_module_init(asset_level_module) {
  level_datareg_init();

  ecs_register_comp(AssetLevelComp, .destructor = ecs_destruct_level_comp);

  ecs_register_view(LevelUnloadView);

  ecs_register_system(LevelUnloadAssetSys, ecs_view_id(LevelUnloadView));
}

void asset_load_lvl(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  DynArray entries = dynarray_create_t(g_alloc_heap, AssetLevelEntryDef, 1024);

  AssetLevelDef  def;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataLevelDefMeta, mem_var(def), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  LevelError buildErr;
  asset_level_build(&def, &entries, &buildErr);
  data_destroy(g_dataReg, g_alloc_heap, g_dataLevelDefMeta, mem_var(def));
  if (buildErr) {
    errMsg = level_error_str(buildErr);
    goto Error;
  }

  ecs_world_add_t(
      world,
      entity,
      AssetLevelComp,
      .entries    = dynarray_copy_as_new(&entries, g_alloc_heap),
      .entryCount = (u32)entries.size);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Level", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
  dynarray_destroy(&entries);
}
