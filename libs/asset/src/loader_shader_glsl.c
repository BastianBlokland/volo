#include "core_alloc.h"
#include "core_array.h"
#include "core_dynlib.h"
#include "core_env.h"
#include "core_path.h"
#include "ecs_utils.h"
#include "log_logger.h"

#include "loader_shader_internal.h"
#include "manager_internal.h"

/**
 * Glsl (OpenGL Shading Language) loader using libshaderc (https://github.com/google/shaderc/).
 */

#define glsl_shaderc_names_max 4
#define glsl_shaderc_debug_info true
#define glsl_shaderc_optimize true

typedef enum {
  ShadercOptimization_None        = 0,
  ShadercOptimization_Size        = 1,
  ShadercOptimization_Performance = 2,
} ShadercOptimization;

typedef enum {
  ShadercTargetEnv_Vulkan = 0,
} ShadercTargetEnv;

typedef enum {
  ShadercSpvVersion_1_3 = 0x010300u,
} ShadercSpvVersion;

typedef enum {
  ShadercTargetEnvVersion_Vulkan_1_3 = (1u << 22) | (3 << 12),
} ShadercTargetEnvVersion;

typedef enum {
  ShadercShaderKind_Vertex   = 0,
  ShadercShaderKind_Fragment = 1,
} ShadercShaderKind;

typedef struct sShadercCompiler          ShadercCompiler;
typedef struct sShadercCompileOptions    ShadercCompileOptions;
typedef struct sShadercCompilationResult ShadercCompilationResult;

ecs_comp_define(AssetGlslEnvComp) {
  DynLib*                shaderc;
  ShadercCompiler*       compiler;
  ShadercCompileOptions* options;

  // clang-format off
  ShadercCompiler*       (SYS_DECL* compiler_initialize)(void);
  void                   (SYS_DECL* compiler_release)(ShadercCompiler*);
  ShadercCompileOptions* (SYS_DECL* compile_options_initialize)(void);
  void                   (SYS_DECL* compile_options_release)(ShadercCompileOptions*);
  void                   (SYS_DECL* compile_options_set_target_env)(ShadercCompileOptions*, ShadercTargetEnv, ShadercTargetEnvVersion);
  void                   (SYS_DECL* compile_options_set_target_spirv)(ShadercCompileOptions*, ShadercSpvVersion);
  void                   (SYS_DECL* compile_options_set_warnings_as_errors)(ShadercCompileOptions*);
  void                   (SYS_DECL* compile_options_set_preserve_bindings)(ShadercCompileOptions*, bool);
  void                   (SYS_DECL* compile_options_set_generate_debug_info)(ShadercCompileOptions*);
  void                   (SYS_DECL* compile_options_set_optimization_level)(ShadercCompileOptions*, ShadercOptimization);
  void                   (SYS_DECL* compile_into_spv)(ShadercCompilationResult*, const ShadercCompiler*, const char* sourceText, size_t sourceTextSize, ShadercShaderKind, const char* inputFileName, const char* entryPointName, const ShadercCompileOptions*);
  void                   (SYS_DECL* result_release)(ShadercCompilationResult*);
  // clang-format on
};

ecs_comp_define(AssetGlslLoadComp) {
  ShadercShaderKind kind;
  AssetSource*      src;
};

static void ecs_destruct_glsl_env_comp(void* data) {
  AssetGlslEnvComp* comp = data;
  if (comp->shaderc) {
    if (comp->options) {
      comp->compile_options_release(comp->options);
    }
    if (comp->compiler) {
      comp->compiler_release(comp->compiler);
    }
    dynlib_destroy(comp->shaderc);
  }
}

static void ecs_destruct_glsl_load_comp(void* data) {
  AssetGlslLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

typedef enum {
  GlslError_None = 0,
  GlslError_CompilerNotAvailable,

  GlslError_Count,
} GlslError;

static String glsl_error_str(const GlslError res) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("No Glsl compiler available"),
  };
  ASSERT(array_elems(g_msgs) == GlslError_Count, "Incorrect number of glsl-error messages");
  return g_msgs[res];
}

static void glsl_load_fail(EcsWorld* world, const EcsEntityId entity, const GlslError err) {
  log_e("Failed to parse Glsl shader", log_param("error", fmt_text(glsl_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

static u32 glsl_shaderc_lib_names(String outPaths[PARAM_ARRAY_SIZE(glsl_shaderc_names_max)]) {
  const String vulkanSdkPath = env_var_scratch(string_lit("VULKAN_SDK"));

  u32 count = 0;
#ifdef VOLO_WIN32
  outPaths[count++] = string_lit("shaderc_shared.dll");
  if (!string_is_empty(vulkanSdkPath)) {
    outPaths[count++] = path_build_scratch(vulkanSdkPath, string_lit("Bin/shaderc_shared.dll"));
  }
#elif VOLO_LINUX
  outPaths[count++] = string_lit("libshaderc_shared.so.1");
  if (!string_is_empty(vulkanSdkPath)) {
    outPaths[count++] = path_build_scratch(vulkanSdkPath, string_lit("lib/libshaderc_shared.so.1"));
  }
#endif

  return count;
}

static AssetGlslEnvComp* glsl_env_init(EcsWorld* world, const EcsEntityId entity) {
  AssetGlslEnvComp* env = ecs_world_add_t(world, entity, AssetGlslEnvComp);

  String    libNames[glsl_shaderc_names_max];
  const u32 libNameCount = glsl_shaderc_lib_names(libNames);

  DynLibResult loadRes = dynlib_load_first(g_allocHeap, libNames, libNameCount, &env->shaderc);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load 'libshaderc' Glsl compiler", log_param("err", fmt_text(err)));
    goto Done;
  }
  log_i("Glsl compiler loaded", log_param("path", fmt_path(dynlib_path(env->shaderc))));

#define SHADERC_LOAD_SYM(_NAME_)                                                                   \
  do {                                                                                             \
    const String symName = string_lit("shaderc_" #_NAME_);                                         \
    env->_NAME_          = dynlib_symbol(env->shaderc, symName);                                   \
    if (!env->_NAME_) {                                                                            \
      log_w("Shaderc symbol '{}' missing", log_param("sym", fmt_text(symName)));                   \
      goto Done;                                                                                   \
    }                                                                                              \
  } while (false)

  SHADERC_LOAD_SYM(compiler_initialize);
  SHADERC_LOAD_SYM(compiler_release);
  SHADERC_LOAD_SYM(compile_options_initialize);
  SHADERC_LOAD_SYM(compile_options_release);
  SHADERC_LOAD_SYM(compile_options_set_target_env);
  SHADERC_LOAD_SYM(compile_options_set_target_spirv);
  SHADERC_LOAD_SYM(compile_options_set_warnings_as_errors);
  SHADERC_LOAD_SYM(compile_options_set_preserve_bindings);
  SHADERC_LOAD_SYM(compile_options_set_generate_debug_info);
  SHADERC_LOAD_SYM(compile_options_set_optimization_level);
  SHADERC_LOAD_SYM(compile_into_spv);
  SHADERC_LOAD_SYM(result_release);

  env->compiler = env->compiler_initialize();
  if (!env->compiler) {
    log_e("Failed to initialize Shaderc compiler");
    goto Done;
  }
  env->options = env->compile_options_initialize();
  if (!env->options) {
    log_e("Failed to initialize Shaderc compile-options");
    goto Done;
  }
  env->compile_options_set_target_env(
      env->options, ShadercTargetEnv_Vulkan, ShadercTargetEnvVersion_Vulkan_1_3);
  env->compile_options_set_target_spirv(env->options, ShadercSpvVersion_1_3);
  env->compile_options_set_warnings_as_errors(env->options);
  env->compile_options_set_preserve_bindings(env->options, true);
#if glsl_shaderc_debug_info
  env->compile_options_set_generate_debug_info(env->options);
#endif
#if glsl_shaderc_optimize
  env->compile_options_set_optimization_level(env->options, ShadercOptimization_Performance);
#endif

Done:
  return env;
}

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(AssetGlslEnvComp);
}

ecs_view_define(LoadView) { ecs_access_read(AssetGlslLoadComp); }

/**
 * Load glsl-shader assets.
 */
ecs_system_define(LoadGlslAssetSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  AssetManagerComp* manager = ecs_view_write_t(globalItr, AssetManagerComp);
  AssetGlslEnvComp* glslEnv = ecs_view_write_t(globalItr, AssetGlslEnvComp);
  if (!glslEnv) {
    glslEnv = glsl_env_init(world, ecs_world_global(world));
  }
  (void)manager;

  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);

    if (!glslEnv->compiler || !glslEnv->options) {
      glsl_load_fail(world, entity, GlslError_CompilerNotAvailable);
      goto Error;
    }

    // TODO: Call into the spv loader to produce the shader meta.
    ecs_world_remove_t(world, entity, AssetGlslLoadComp);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    continue;

  Error:
    // NOTE: 'AssetShaderComp' will be cleaned up by 'UnloadShaderAssetSys'.
    ecs_world_remove_t(world, entity, AssetGlslLoadComp);
  }
}

ecs_module_init(asset_shader_glsl_module) {
  ecs_register_comp(AssetGlslEnvComp, .destructor = ecs_destruct_glsl_env_comp);
  ecs_register_comp(AssetGlslLoadComp, .destructor = ecs_destruct_glsl_load_comp);

  ecs_register_view(GlobalView);
  ecs_register_view(LoadView);

  ecs_register_system(LoadGlslAssetSys, ecs_view_id(GlobalView), ecs_view_id(LoadView));
}

void asset_load_glsl_vert(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGlslLoadComp, .kind = ShadercShaderKind_Vertex, .src = src);
}

void asset_load_glsl_frag(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGlslLoadComp, .kind = ShadercShaderKind_Fragment, .src = src);
}
