#include "core/alloc.h"
#include "core/array.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/math.h"
#include "core/path.h"
#include "data/read.h"
#include "data/utils.h"
#include "data/write.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "log/logger.h"

#include "prefs.h"

static const usize g_prefsMaxSize = 32 * usize_kibibyte;

const String g_gameQualityLabels[] = {
    string_static("MENU_QUALITY_VERY_LOW"),
    string_static("MENU_QUALITY_LOW"),
    string_static("MENU_QUALITY_MEDIUM"),
    string_static("MENU_QUALITY_HIGH"),
};
ASSERT(array_elems(g_gameQualityLabels) == GameQuality_Count, "Incorrect number of quality labels");

const String g_gameLimiterLabels[] = {
    string_static("MENU_LIMITER_OFF"),
    string_static("MENU_LIMITER_30"),
    string_static("MENU_LIMITER_60"),
};
ASSERT(array_elems(g_gameLimiterLabels) == GameLimiter_Count, "Incorrect number of limiter labels");

const String g_gameUiScaleLabels[] = {
    string_static("MENU_UI_SCALE_SMALL"),
    string_static("MENU_UI_SCALE_NORMAL"),
    string_static("MENU_UI_SCALE_BIG"),
    string_static("MENU_UI_SCALE_VERY_BIG"),
};
ASSERT(array_elems(g_gameUiScaleLabels) == GameUiScale_Count, "Incorrect number of scale labels");

static DataMeta g_gamePrefsMeta;

static void prefs_data_init(void) {
  if (!g_gamePrefsMeta.type) {
    data_reg_enum_t(g_dataReg, GameQuality);
    data_reg_const_t(g_dataReg, GameQuality, VeryLow);
    data_reg_const_t(g_dataReg, GameQuality, Low);
    data_reg_const_t(g_dataReg, GameQuality, Medium);
    data_reg_const_t(g_dataReg, GameQuality, High);

    data_reg_enum_t(g_dataReg, GameLimiter);
    data_reg_const_t(g_dataReg, GameLimiter, Off);
    data_reg_const_t(g_dataReg, GameLimiter, 30);
    data_reg_const_t(g_dataReg, GameLimiter, 60);

    data_reg_enum_t(g_dataReg, GameUiScale);
    data_reg_const_t(g_dataReg, GameUiScale, Small);
    data_reg_const_t(g_dataReg, GameUiScale, Normal);
    data_reg_const_t(g_dataReg, GameUiScale, Big);
    data_reg_const_t(g_dataReg, GameUiScale, VeryBig);

    data_reg_struct_t(g_dataReg, GamePrefsComp);
    data_reg_field_t(g_dataReg, GamePrefsComp, volume, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GamePrefsComp, exposure, data_prim_t(f32));
    data_reg_field_t(g_dataReg, GamePrefsComp, limiter, t_GameLimiter);
    data_reg_field_t(g_dataReg, GamePrefsComp, vsync, data_prim_t(bool));
    data_reg_field_t(g_dataReg, GamePrefsComp, fullscreen, data_prim_t(bool));
    data_reg_field_t(g_dataReg, GamePrefsComp, windowWidth, data_prim_t(u16));
    data_reg_field_t(g_dataReg, GamePrefsComp, windowHeight, data_prim_t(u16));
    data_reg_field_t(g_dataReg, GamePrefsComp, quality, t_GameQuality);
    data_reg_field_t(g_dataReg, GamePrefsComp, uiScale, t_GameUiScale);
    data_reg_field_t(g_dataReg, GamePrefsComp, locale, data_prim_t(String), .flags = DataFlags_Opt);

    g_gamePrefsMeta = data_meta_t(t_GamePrefsComp);
  }
}

ecs_comp_define(GamePrefsComp);

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
  prefs->exposure     = 0.5f;
  prefs->limiter      = GameLimiter_Off;
  prefs->vsync        = true;
  prefs->fullscreen   = true;
  prefs->windowWidth  = 1920;
  prefs->windowHeight = 1080;
  prefs->quality      = GameQuality_Medium;
  prefs->uiScale      = GameUiScale_Normal;
  prefs->locale       = string_empty;
}

static void prefs_save(const GamePrefsComp* prefs) {
  DynString dataBuffer = dynstring_create(g_allocScratch, g_prefsMaxSize);

  // Serialize the preferences to json.
  const DataWriteJsonOpts writeOpts = data_write_json_opts();
  data_write_json(g_dataReg, &dataBuffer, g_gamePrefsMeta, mem_var(*prefs), &writeOpts);
  dynstring_append_char(&dataBuffer, '\n'); // End the file with a new-line.

  // Save the data to disk.
  const String     filePath = prefs_path_scratch();
  const FileResult fileRes  = file_write_to_path_atomic(filePath, dynstring_view(&dataBuffer));
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

GamePrefsComp* game_prefs_init(EcsWorld* world) {
  prefs_data_init();

  GamePrefsComp* prefs = ecs_world_add_t(world, ecs_world_global(world), GamePrefsComp);

  // Open the file handle.
  const FileMode        fileMode   = FileMode_Open;
  const FileAccessFlags fileAccess = FileAccess_Read;
  File*                 file       = null;
  FileResult            fileRes;
  if ((fileRes = file_create(g_allocHeap, prefs_path_scratch(), fileMode, fileAccess, &file))) {
    if (fileRes != FileResult_NotFound) {
      log_e("Failed to read preference file", log_param("err", fmt_text(file_result_str(fileRes))));
    }
    goto RetDefault;
  }

  // Map the file data.
  String fileData;
  if (UNLIKELY(fileRes = file_map(file, 0, 0, FileHints_Prefetch, &fileData))) {
    log_e("Failed to map preference file", log_param("err", fmt_text(file_result_str(fileRes))));
    goto RetDefault;
  }
  if (UNLIKELY(fileData.size > g_prefsMaxSize)) {
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

  // NOTE: Consider making specialized data-types with associated normalizers.
  prefs->volume   = math_clamp_f32(prefs->volume, 0.0f, 1e2f);
  prefs->exposure = math_clamp_f32(prefs->exposure, 0.0f, 1.0f);

  log_i(
      "Preference file loaded",
      log_param("path", fmt_text(prefs_path_scratch())),
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

void game_prefs_locale_set(GamePrefsComp* prefs, const String locale) {
  if (!string_eq(prefs->locale, locale)) {
    string_maybe_free(g_allocHeap, prefs->locale);
    prefs->locale = string_maybe_dup(g_allocHeap, locale);
    prefs->dirty  = true;
  }
}
