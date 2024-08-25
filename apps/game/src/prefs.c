#include "core_alloc.h"
#include "core_array.h"
#include "core_file.h"
#include "core_path.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "prefs_internal.h"

#define prefs_file_size_max (usize_kibibyte * 64)

const String g_gameQualityLabels[] = {
    string_static("VeryLow"),
    string_static("Low"),
    string_static("Medium"),
    string_static("High"),
};
ASSERT(array_elems(g_gameQualityLabels) == GameQuality_Count, "Incorrect number of quality labels");

static DataMeta g_gamePrefsMeta;

static void prefs_data_init(void) {
  if (!g_gamePrefsMeta.type) {
    data_reg_enum_t(g_dataReg, GameQuality);
    data_reg_const_t(g_dataReg, GameQuality, VeryLow);
    data_reg_const_t(g_dataReg, GameQuality, Low);
    data_reg_const_t(g_dataReg, GameQuality, Medium);
    data_reg_const_t(g_dataReg, GameQuality, High);

    data_reg_struct_t(g_dataReg, GamePrefsComp);
    data_reg_field_t(g_dataReg, GamePrefsComp, volume, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GamePrefsComp, powerSaving, data_prim_t(bool));
    data_reg_field_t(g_dataReg, GamePrefsComp, fullscreen, data_prim_t(bool));
    data_reg_field_t(g_dataReg, GamePrefsComp, windowWidth, data_prim_t(u16));
    data_reg_field_t(g_dataReg, GamePrefsComp, windowHeight, data_prim_t(u16));
    data_reg_field_t(g_dataReg, GamePrefsComp, quality, t_GameQuality);

    g_gamePrefsMeta = data_meta_t(t_GamePrefsComp);
  }
}

ecs_comp_define_public(GamePrefsComp);

static void ecs_destruct_prefs_comp(void* data) {
  GamePrefsComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_gamePrefsMeta, mem_create(comp, sizeof(GamePrefsComp)));
}

static String prefs_path_scratch(void) {
  const String fileName = fmt_write_scratch("{}.prefs", fmt_text(path_stem(g_pathExecutable)));
  return path_build_scratch(path_parent(g_pathExecutable), fileName);
}

static void prefs_to_default(GamePrefsComp* prefs) {
  prefs->volume       = 100.0f;
  prefs->powerSaving  = false;
  prefs->fullscreen   = true;
  prefs->windowWidth  = 1920;
  prefs->windowHeight = 1080;
  prefs->quality      = GameQuality_Medium;
}

static void prefs_save(const GamePrefsComp* prefs) {
  DynString dataBuffer = dynstring_create(g_allocScratch, prefs_file_size_max);

  // Serialize the preferences to json.
  const DataWriteJsonOpts writeOpts = data_write_json_opts();
  data_write_json(g_dataReg, &dataBuffer, g_gamePrefsMeta, mem_var(*prefs), &writeOpts);

  // Save the data to disk.
  const String     filePath = prefs_path_scratch();
  const FileResult fileRes  = file_write_to_path_sync(filePath, dynstring_view(&dataBuffer));
  if (UNLIKELY(fileRes)) {
    log_e("Failed to write preference file", log_param("err", fmt_text(file_result_str(fileRes))));
  }
}

ecs_view_define(PrefsView) { ecs_access_write(GamePrefsComp); }

ecs_system_define(GamePrefsSaveSys) {
  EcsView* prefsView = ecs_world_view_t(world, PrefsView);
  for (EcsIterator* itr = ecs_view_itr(prefsView); ecs_view_walk(itr);) {
    GamePrefsComp* prefs = ecs_view_write_t(itr, GamePrefsComp);
    if (prefs->dirty) {
      prefs_save(prefs);
      prefs->dirty = false;
    }
  }
}

ecs_module_init(game_prefs_module) {
  ecs_register_comp(GamePrefsComp, .destructor = ecs_destruct_prefs_comp);

  ecs_register_view(PrefsView);

  ecs_register_system(GamePrefsSaveSys, ecs_view_id(PrefsView));
}

GamePrefsComp* prefs_init(EcsWorld* world) {
  prefs_data_init();

  GamePrefsComp* prefs = ecs_world_add_t(world, ecs_world_global(world), GamePrefsComp);

  // Open the file handle.
  const String filePath = prefs_path_scratch();
  File*        file     = null;
  FileResult   fileRes;
  if ((fileRes = file_create(g_allocScratch, filePath, FileMode_Open, FileAccess_Read, &file))) {
    if (fileRes != FileResult_NotFound) {
      log_e("Failed to read preference file", log_param("err", fmt_text(file_result_str(fileRes))));
    }
    goto RetDefault;
  }

  // Map the file data.
  String fileData;
  if (UNLIKELY(fileRes = file_map(file, &fileData, FileHints_Prefetch))) {
    log_e("Failed to map preference file", log_param("err", fmt_text(file_result_str(fileRes))));
    goto RetDefault;
  }
  if (UNLIKELY(fileData.size > prefs_file_size_max)) {
    log_e("Preference file size exceeds maximum");
    goto RetDefault;
  }

  // Parse the json.
  DataReadResult result;
  const Mem      outMem = mem_create(prefs, sizeof(GamePrefsComp));
  data_read_json(g_dataReg, fileData, g_allocHeap, g_gamePrefsMeta, outMem, &result);
  if (UNLIKELY(result.error)) {
    log_e("Failed to parse preference file", log_param("err", fmt_text(result.errorMsg)));
    goto RetDefault;
  }

  log_i(
      "Preference file loaded",
      log_param("path", fmt_path(filePath)),
      log_param("size", fmt_size(fileData.size)));
  goto Ret;

RetDefault:
  prefs_to_default(prefs);
Ret:
  if (file) {
    file_destroy(file);
  }
  return prefs;
}
