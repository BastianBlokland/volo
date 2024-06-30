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

#define glsl_shaderc_glsl_version 450
#define glsl_shaderc_debug_info true
#define glsl_shaderc_optimize true
#define glsl_shaderc_names_max 4

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

typedef enum {
  ShadercCompilationStatus_Success = 0,
} ShadercCompilationStatus;

typedef enum {
  ShadercIncludeType_Relative, // #include "other"
  ShadercIncludeType_Standard, // #include <other>
} ShadercIncludeType;

typedef struct {
  const char* sourceName; // Resolved absolute path.
  usize       sourceNameLength;
  const char* content; // Contains the error message in-case of inclusion error.=
  usize       contentLength;
  void*       userContext;
} ShadercIncludeResult;

typedef struct {
  u32 dummy;
} ShadercIncludeContext;

typedef struct sShadercCompiler          ShadercCompiler;
typedef struct sShadercCompileOptions    ShadercCompileOptions;
typedef struct sShadercCompilationResult ShadercCompilationResult;

ecs_comp_define(AssetGlslEnvComp) {
  DynLib*                shaderc;
  ShadercCompiler*       compiler;
  ShadercCompileOptions* options;
  ShadercIncludeContext* includeCtx;

  // clang-format off
  ShadercCompiler*          (SYS_DECL* compiler_initialize)(void);
  void                      (SYS_DECL* compiler_release)(ShadercCompiler*);
  ShadercCompileOptions*    (SYS_DECL* compile_options_initialize)(void);
  void                      (SYS_DECL* compile_options_release)(ShadercCompileOptions*);
  void                      (SYS_DECL* compile_options_set_target_env)(ShadercCompileOptions*, ShadercTargetEnv, ShadercTargetEnvVersion);
  void                      (SYS_DECL* compile_options_set_target_spirv)(ShadercCompileOptions*, ShadercSpvVersion);
  void                      (SYS_DECL* compile_options_set_forced_version_profile)(ShadercCompileOptions*, i32 version, i32 profile);
  void                      (SYS_DECL* compile_options_set_warnings_as_errors)(ShadercCompileOptions*);
  void                      (SYS_DECL* compile_options_set_preserve_bindings)(ShadercCompileOptions*, bool);
  void                      (SYS_DECL* compile_options_set_generate_debug_info)(ShadercCompileOptions*);
  void                      (SYS_DECL* compile_options_set_optimization_level)(ShadercCompileOptions*, ShadercOptimization);
  void                      (SYS_DECL* compile_options_set_include_callbacks)(ShadercCompileOptions*, void* resolver, void* releaser, void* userContext);
  ShadercCompilationResult* (SYS_DECL* compile_into_spv)(const ShadercCompiler*, const char* sourceText, usize sourceTextSize, ShadercShaderKind, const char* inputFileName, const char* entryPointName, const ShadercCompileOptions*);
  void                      (SYS_DECL* result_release)(ShadercCompilationResult*);
  ShadercCompilationStatus  (SYS_DECL* result_get_compilation_status)(const ShadercCompilationResult*);
  const char*               (SYS_DECL* result_get_error_message)(const ShadercCompilationResult*);
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
  alloc_free_t(g_allocHeap, comp->includeCtx);
}

static void ecs_destruct_glsl_load_comp(void* data) {
  AssetGlslLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

typedef enum {
  GlslError_None = 0,
  GlslError_CompilerNotAvailable,
  GlslError_CompilationFailed,

  GlslError_Count,
} GlslError;

static String glsl_error_str(const GlslError res) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("No Glsl compiler available"),
      string_static("Glsl compilation failed"),
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

static ShadercIncludeResult* SYS_DECL glsl_include_resolve(
    void*                    userContext,
    const char*              requestedSource,
    const ShadercIncludeType type,
    const char*              requestingSource,
    const usize              includeDepth) {
  (void)userContext;
  (void)requestedSource;
  (void)type;
  (void)requestingSource;
  (void)includeDepth;
  return null;
}

static void SYS_DECL glsl_include_release(void* userContext, ShadercIncludeResult* result) {
  (void)userContext;
  (void)result;
}

static AssetGlslEnvComp* glsl_env_init(EcsWorld* world, const EcsEntityId entity) {
  AssetGlslEnvComp* env = ecs_world_add_t(
      world,
      entity,
      AssetGlslEnvComp,
      .includeCtx = alloc_alloc_t(g_allocHeap, ShadercIncludeContext));

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
  SHADERC_LOAD_SYM(compile_options_set_forced_version_profile);
  SHADERC_LOAD_SYM(compile_options_set_warnings_as_errors);
  SHADERC_LOAD_SYM(compile_options_set_preserve_bindings);
  SHADERC_LOAD_SYM(compile_options_set_generate_debug_info);
  SHADERC_LOAD_SYM(compile_options_set_optimization_level);
  SHADERC_LOAD_SYM(compile_options_set_include_callbacks);
  SHADERC_LOAD_SYM(compile_into_spv);
  SHADERC_LOAD_SYM(result_release);
  SHADERC_LOAD_SYM(result_get_compilation_status);
  SHADERC_LOAD_SYM(result_get_error_message);

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
  env->compile_options_set_forced_version_profile(env->options, glsl_shaderc_glsl_version, 0);
  env->compile_options_set_warnings_as_errors(env->options);
  env->compile_options_set_preserve_bindings(env->options, true);
  env->compile_options_set_include_callbacks(
      env->options, glsl_include_resolve, glsl_include_release, env->includeCtx);
#if glsl_shaderc_debug_info
  env->compile_options_set_generate_debug_info(env->options);
#endif
#if glsl_shaderc_optimize
  env->compile_options_set_optimization_level(env->options, ShadercOptimization_Performance);
#endif

Done:
  return env;
}

static bool glsl_compile(
    const AssetGlslEnvComp* glslEnv,
    const String            input,
    const String            inputId,
    const ShadercShaderKind inputKind) {
  bool success = true;

  ShadercCompilationResult* res = glslEnv->compile_into_spv(
      glslEnv->compiler,
      input.ptr,
      input.size,
      inputKind,
      to_null_term_scratch(inputId),
      "main" /* entry-point*/,
      glslEnv->options);

  if (glslEnv->result_get_compilation_status(res) != ShadercCompilationStatus_Success) {
    const String err = string_from_null_term(glslEnv->result_get_error_message(res));
    log_e(
        "Glsl compilation failed",
        log_param("input", fmt_text(inputId)),
        log_param("err", fmt_text(err)));
    success = false;
    goto Done;
  }

Done:
  glslEnv->result_release(res);
  return success;
}

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(AssetGlslEnvComp);
}

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetGlslLoadComp);
}

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
    const AssetGlslLoadComp* load   = ecs_view_read_t(itr, AssetGlslLoadComp);
    const EcsEntityId        entity = ecs_view_entity(itr);
    const String             id     = asset_id(ecs_view_read_t(itr, AssetComp));

    if (!glslEnv->compiler || !glslEnv->options) {
      glsl_load_fail(world, entity, GlslError_CompilerNotAvailable);
      goto Error;
    }
    if (!glsl_compile(glslEnv, load->src->data, id, load->kind)) {
      glsl_load_fail(world, entity, GlslError_CompilationFailed);
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
