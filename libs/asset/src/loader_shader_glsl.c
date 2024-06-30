#include "core_alloc.h"
#include "core_array.h"
#include "core_dynlib.h"
#include "ecs_utils.h"
#include "log_logger.h"

#include "loader_shader_internal.h"
#include "manager_internal.h"

/**
 * Glsl (OpenGL Shading Language) loader using libshaderc (https://github.com/google/shaderc/).
 */

#ifdef VOLO_WIN32
static const String g_glslShadercPath = string_static("shaderc_shared.dll");
#else
static const String g_glslShadercPath = string_static("libshaderc_shared.so.1");
#endif

typedef enum {
  GlslKind_Vertex,
  GlslKind_Fragment,
} GlslKind;

ecs_comp_define(AssetGlslEnvComp) { DynLib* shadercLib; };

ecs_comp_define(AssetGlslLoadComp) {
  GlslKind     kind;
  AssetSource* src;
};

static void ecs_destruct_glsl_env_comp(void* data) {
  AssetGlslEnvComp* comp = data;
  if (comp->shadercLib) {
    dynlib_destroy(comp->shadercLib);
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

static AssetGlslEnvComp* glsl_env_init(EcsWorld* world, const EcsEntityId entity) {
  AssetGlslEnvComp*  env     = ecs_world_add_t(world, entity, AssetGlslEnvComp);
  const DynLibResult loadRes = dynlib_load(g_allocHeap, g_glslShadercPath, &env->shadercLib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load 'libshaderc' Glsl compiler", log_param("err", fmt_text(err)));
    return env;
  }
  log_i("Glsl compiler loaded", log_param("path", fmt_path(dynlib_path(env->shadercLib))));
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

    if (!glslEnv->shadercLib) {
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
  ecs_world_add_t(world, entity, AssetGlslLoadComp, .kind = GlslKind_Vertex, .src = src);
}

void asset_load_glsl_frag(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGlslLoadComp, .kind = GlslKind_Fragment, .src = src);
}
