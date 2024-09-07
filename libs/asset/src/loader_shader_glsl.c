#include "core_alloc.h"
#include "core_dynlib.h"
#include "core_env.h"
#include "core_path.h"
#include "ecs_utils.h"
#include "log_logger.h"
#include "trace_tracer.h"

#include "loader_shader_internal.h"
#include "manager_internal.h"

/**
 * Glsl (OpenGL Shading Language) loader using libshaderc (https://github.com/google/shaderc/).
 */

#define glsl_version 450
#define glsl_debug_info true
#define glsl_optimize true
#define glsl_shaderc_names_max 4
#define glsl_id_chunk_size (4 * usize_kibibyte)
#define glsl_track_dependencies true

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
  ShadercTargetEnvVersion_Vulkan_1_1 = (1u << 22) | (1 << 12),
} ShadercTargetEnvVersion;

typedef enum {
  ShadercShaderKind_Vertex   = 0,
  ShadercShaderKind_Fragment = 1,
} ShadercShaderKind;

typedef enum {
  ShadercCompilationStatus_Success = 0,
} ShadercCompilationStatus;

typedef enum {
  ShadercIncludeType_Relative,
  ShadercIncludeType_Standard,
} ShadercIncludeType;

typedef struct {
  const char* sourceName;
  usize       sourceNameLength;
  const char* content; // Contains the error message in-case of inclusion error.
  usize       contentLength;
  void*       userData; // AssetSource*
} ShadercIncludeResult;

typedef struct sShadercCompiler          ShadercCompiler;
typedef struct sShadercCompileOptions    ShadercCompileOptions;
typedef struct sShadercCompilationResult ShadercCompilationResult;

typedef struct {
  EcsWorld*         world;
  EcsEntityId       assetEntity;
  AssetManagerComp* assetManager;
} GlslIncludeInvocation;

typedef struct {
  const GlslIncludeInvocation* invoc;
  Allocator*                   idAlloc;     // (chunked) bump allocator for include ids.
  Allocator*                   resultAlloc; // Allocator for ShadercIncludeResult objects.
} GlslIncludeCtx;

ecs_comp_define(AssetGlslEnvComp) {
  DynLib*                shaderc;
  ShadercCompiler*       compiler;
  ShadercCompileOptions* options;
  GlslIncludeCtx*        includeCtx;

  // clang-format off
  ShadercCompiler*          (SYS_DECL* compiler_initialize)(void);
  void                      (SYS_DECL* compiler_release)(ShadercCompiler*);
  ShadercCompileOptions*    (SYS_DECL* compile_options_initialize)(void);
  void                      (SYS_DECL* compile_options_release)(ShadercCompileOptions*);
  void                      (SYS_DECL* compile_options_set_target_env)(ShadercCompileOptions*, ShadercTargetEnv, ShadercTargetEnvVersion);
  void                      (SYS_DECL* compile_options_set_target_spirv)(ShadercCompileOptions*, ShadercSpvVersion);
  void                      (SYS_DECL* compile_options_set_include_callbacks)(ShadercCompileOptions*, void* resolver, void* releaser, void* userContext);
  void                      (SYS_DECL* compile_options_set_forced_version_profile)(ShadercCompileOptions*, i32 version, i32 profile);
  void                      (SYS_DECL* compile_options_set_warnings_as_errors)(ShadercCompileOptions*);
  void                      (SYS_DECL* compile_options_set_preserve_bindings)(ShadercCompileOptions*, bool);
  void                      (SYS_DECL* compile_options_set_generate_debug_info)(ShadercCompileOptions*);
  void                      (SYS_DECL* compile_options_set_optimization_level)(ShadercCompileOptions*, ShadercOptimization);
  ShadercCompilationResult* (SYS_DECL* compile_into_spv)(const ShadercCompiler*, const char* sourceText, usize sourceTextSize, ShadercShaderKind, const char* inputFileName, const char* entryPointName, const ShadercCompileOptions*);
  void                      (SYS_DECL* result_release)(ShadercCompilationResult*);
  ShadercCompilationStatus  (SYS_DECL* result_get_compilation_status)(const ShadercCompilationResult*);
  const char*               (SYS_DECL* result_get_error_message)(const ShadercCompilationResult*);
  usize                     (SYS_DECL* result_get_length)(const ShadercCompilationResult*);
  const char*               (SYS_DECL* result_get_bytes)(const ShadercCompilationResult*);
  // clang-format on
};

ecs_comp_define(AssetGlslLoadComp) {
  ShadercShaderKind kind;
  AssetSource*      src;
};

static GlslIncludeCtx* glsl_include_ctx_init(void) {
  GlslIncludeCtx* ctx = alloc_alloc_t(g_allocHeap, GlslIncludeCtx);

  ctx->idAlloc = alloc_chunked_create(g_allocHeap, alloc_bump_create, glsl_id_chunk_size);

  const usize resultSize  = sizeof(ShadercIncludeResult);
  const usize resultAlign = alignof(ShadercIncludeResult);
  ctx->resultAlloc        = alloc_block_create(g_allocHeap, resultSize, resultAlign);
  return ctx;
}

static void glsl_include_ctx_prepare(GlslIncludeCtx* ctx, const GlslIncludeInvocation* invoc) {
  ctx->invoc = invoc;
}

static void glsl_include_ctx_clear(GlslIncludeCtx* ctx) {
  alloc_reset(ctx->idAlloc);
  alloc_reset(ctx->resultAlloc);
  ctx->invoc = null;
}

static void glsl_include_ctx_destroy(GlslIncludeCtx* ctx) {
  alloc_chunked_destroy(ctx->idAlloc);
  alloc_block_destroy(ctx->resultAlloc);
  alloc_free_t(g_allocHeap, ctx);
}

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
  glsl_include_ctx_destroy(comp->includeCtx);
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
  GlslError_InvalidSpv,

  GlslError_Count,
} GlslError;

static String glsl_error_str(const GlslError res) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("No Glsl compiler available"),
      string_static("Glsl compilation failed"),
      string_static("Glsl compilation resulted in invalid SpirV"),
  };
  ASSERT(array_elems(g_msgs) == GlslError_Count, "Incorrect number of glsl-error messages");
  return g_msgs[res];
}

static void
glsl_load_fail(EcsWorld* world, const EcsEntityId entity, const String id, const GlslError err) {
  log_e(
      "Failed to load Glsl shader",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(entity)),
      log_param("error", fmt_text(glsl_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

static void glsl_load_fail_msg(
    EcsWorld*         world,
    const EcsEntityId entity,
    const String      id,
    const GlslError   err,
    const String      msg) {
  log_e(
      "Failed to load Glsl shader",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(entity)),
      log_param("error", fmt_text(glsl_error_str(err))),
      log_param("text", fmt_text(msg)));
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

static void glsl_include_error(ShadercIncludeResult* res, const String msg) {
  *res = (ShadercIncludeResult){.content = msg.ptr, .contentLength = msg.size};
}

static ShadercIncludeResult* SYS_DECL glsl_include_resolve(
    void*                    userContext,
    const char*              requestedSource,
    const ShadercIncludeType type,
    const char*              requestingSource,
    const usize              includeDepth) {
  GlslIncludeCtx* ctx = userContext;

  (void)requestingSource;
  (void)includeDepth;

  ShadercIncludeResult* res = alloc_alloc_t(ctx->resultAlloc, ShadercIncludeResult);
  if (UNLIKELY(type != ShadercIncludeType_Standard)) {
    glsl_include_error(res, string_lit("Relative includes are not supported"));
    return res;
  }

  const Mem idBuffer  = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString idBuilder = dynstring_create_over(idBuffer);

  path_append(&idBuilder, string_lit("shaders"));
  path_append(&idBuilder, string_lit("include"));
  path_append(&idBuilder, path_canonize_scratch(string_from_null_term(requestedSource)));

  const String id = string_dup(ctx->idAlloc, dynstring_view(&idBuilder));

  AssetSource* src = asset_source_open(ctx->invoc->assetManager, id);
  if (UNLIKELY(!src)) {
    glsl_include_error(res, string_lit("File not found"));
    return res;
  }
  if (UNLIKELY(src->format != AssetFormat_ShaderGlsl)) {
    asset_repo_source_close(src);
    glsl_include_error(res, string_lit("File has an invalid format"));
    return res;
  }

  res->sourceName       = id.ptr;
  res->sourceNameLength = id.size;
  res->content          = src->data.ptr;
  res->contentLength    = src->data.size;
  res->userData         = src;

#if glsl_track_dependencies
  {
    const EcsEntityId depEntity = asset_watch(ctx->invoc->world, ctx->invoc->assetManager, id);
    asset_mark_external_load(ctx->invoc->world, depEntity, AssetFormat_ShaderGlsl, src->modTime);
    asset_register_dep(ctx->invoc->world, ctx->invoc->assetEntity, depEntity);
  }
#endif

  return res;
}

static void SYS_DECL glsl_include_release(void* userContext, ShadercIncludeResult* result) {
  GlslIncludeCtx* ctx = userContext;
  if (result->userData) {
    asset_repo_source_close(result->userData);
  }
  alloc_free_t(ctx->resultAlloc, result);
}

static AssetGlslEnvComp* glsl_env_init(EcsWorld* world, const EcsEntityId entity) {
  AssetGlslEnvComp* env = ecs_world_add_t(world, entity, AssetGlslEnvComp);
  env->includeCtx       = glsl_include_ctx_init();

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

#define SHADERC_LOAD_SYM_OPT(_NAME_)                                                               \
  do {                                                                                             \
    const String symName = string_lit("shaderc_" #_NAME_);                                         \
    env->_NAME_          = dynlib_symbol(env->shaderc, symName);                                   \
  } while (false)

  SHADERC_LOAD_SYM(compiler_initialize);
  SHADERC_LOAD_SYM(compiler_release);
  SHADERC_LOAD_SYM(compile_options_initialize);
  SHADERC_LOAD_SYM(compile_options_release);
  SHADERC_LOAD_SYM(compile_options_set_target_env);
  SHADERC_LOAD_SYM(compile_options_set_target_spirv);
  SHADERC_LOAD_SYM(compile_options_set_include_callbacks);
  SHADERC_LOAD_SYM_OPT(compile_options_set_forced_version_profile);
  SHADERC_LOAD_SYM_OPT(compile_options_set_warnings_as_errors);
  SHADERC_LOAD_SYM_OPT(compile_options_set_preserve_bindings);
  SHADERC_LOAD_SYM_OPT(compile_options_set_generate_debug_info);
  SHADERC_LOAD_SYM_OPT(compile_options_set_optimization_level);
  SHADERC_LOAD_SYM(compile_into_spv);
  SHADERC_LOAD_SYM(result_release);
  SHADERC_LOAD_SYM(result_get_compilation_status);
  SHADERC_LOAD_SYM(result_get_error_message);
  SHADERC_LOAD_SYM(result_get_length);
  SHADERC_LOAD_SYM(result_get_bytes);

#undef SHADERC_LOAD_SYM
#undef SHADERC_LOAD_SYM_OPT

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
      env->options, ShadercTargetEnv_Vulkan, ShadercTargetEnvVersion_Vulkan_1_1);
  env->compile_options_set_target_spirv(env->options, ShadercSpvVersion_1_3);
  env->compile_options_set_include_callbacks(
      env->options, glsl_include_resolve, glsl_include_release, env->includeCtx);

  if (env->compile_options_set_forced_version_profile) {
    env->compile_options_set_forced_version_profile(env->options, glsl_version, 0);
  }
  if (env->compile_options_set_warnings_as_errors) {
    env->compile_options_set_warnings_as_errors(env->options);
  }
  if (env->compile_options_set_preserve_bindings) {
    env->compile_options_set_preserve_bindings(env->options, true);
  }
#if glsl_debug_info
  if (env->compile_options_set_generate_debug_info) {
    env->compile_options_set_generate_debug_info(env->options);
  }
#endif
#if glsl_optimize
  if (env->compile_options_set_optimization_level) {
    env->compile_options_set_optimization_level(env->options, ShadercOptimization_Performance);
  }
#endif

Done:
  return env;
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

  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    if (!glslEnv) {
      /**
       * Lazily construct the GLSL compilation environment.
       * Reason is often its not needed due to only loading (cached) binary shader blobs.
       */
      glslEnv = glsl_env_init(world, ecs_world_global(world));
    }
    const AssetGlslLoadComp* load   = ecs_view_read_t(itr, AssetGlslLoadComp);
    const EcsEntityId        entity = ecs_view_entity(itr);
    const String             id     = asset_id(ecs_view_read_t(itr, AssetComp));

    trace_begin_msg("asset_glsl_build", TraceColor_Blue, "{}", fmt_text(path_filename(id)));

    const GlslIncludeInvocation includeInvoc = {
        .world        = world,
        .assetEntity  = entity,
        .assetManager = manager,
    };
    glsl_include_ctx_prepare(glslEnv->includeCtx, &includeInvoc);

    if (!glslEnv->compiler || !glslEnv->options) {
      glsl_load_fail(world, entity, id, GlslError_CompilerNotAvailable);
      goto Done;
    }

    ShadercCompilationResult* res = glslEnv->compile_into_spv(
        glslEnv->compiler,
        load->src->data.ptr,
        load->src->data.size,
        load->kind,
        to_null_term_scratch(id),
        "main" /* entry-point*/,
        glslEnv->options);

    if (glslEnv->result_get_compilation_status(res) != ShadercCompilationStatus_Success) {
      const String msg = string_from_null_term(glslEnv->result_get_error_message(res));
      glsl_load_fail_msg(
          world, entity, id, GlslError_CompilationFailed, string_trim_whitespace(msg));
      glslEnv->result_release(res);
      goto Done;
    }

    const Mem resMem  = mem_create(glslEnv->result_get_bytes(res), glslEnv->result_get_length(res));
    const Mem spvData = alloc_dup(g_allocHeap, resMem, alignof(u32));

    glslEnv->result_release(res);

    const SpvError spvErr = spv_init(world, entity, data_mem_create(spvData));
    if (spvErr) {
      const String msg = spv_err_str(spvErr);
      glsl_load_fail_msg(world, entity, id, GlslError_InvalidSpv, msg);
      goto Done;
    }

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);

  Done:
    glsl_include_ctx_clear(glslEnv->includeCtx);
    ecs_world_remove_t(world, entity, AssetGlslLoadComp);

    trace_end();
  }
}

ecs_module_init(asset_shader_glsl_module) {
  ecs_register_comp(AssetGlslEnvComp, .destructor = ecs_destruct_glsl_env_comp);
  ecs_register_comp(AssetGlslLoadComp, .destructor = ecs_destruct_glsl_load_comp);

  ecs_register_view(GlobalView);
  ecs_register_view(LoadView);

  ecs_register_system(LoadGlslAssetSys, ecs_view_id(GlobalView), ecs_view_id(LoadView));
}

void asset_load_shader_glsl_vert(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGlslLoadComp, .kind = ShadercShaderKind_Vertex, .src = src);
}

void asset_load_shader_glsl_frag(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGlslLoadComp, .kind = ShadercShaderKind_Fragment, .src = src);
}
