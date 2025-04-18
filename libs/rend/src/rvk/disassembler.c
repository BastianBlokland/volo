#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_env.h"
#include "core_path.h"
#include "core_thread.h"
#include "log_logger.h"

#include "disassembler_internal.h"

#define spirvtools_names_max 4

typedef enum {
  SpirvToolsState_Idle,
  SpirvToolsState_Ready,
  SpirvToolsState_Failed,
} SpirvToolsState;

typedef enum {
  SpvTargetEnv_Vulkan_1_1 = 18,
} SpvTargetEnv;

typedef enum {
  SpvResult_Success,
} SpvResult;

typedef struct sSpvContext SpvContext;

typedef struct {
  SpirvToolsState state;
  DynLib*         lib;
  SpvContext*     ctx;

  // clang-format off
  SpvContext* (SYS_DECL* spvContextCreate)(SpvTargetEnv);
  void        (SYS_DECL* spvContextDestroy)(SpvContext*);
  // clang-format on
} SpirvTools;

typedef struct sRvkDisassembler {
  Allocator*  alloc;
  ThreadMutex initMutex;
  SpirvTools  spirvTools;
} RvkDisassembler;

static u32 spirvtools_lib_names(String outNames[PARAM_ARRAY_SIZE(spirvtools_names_max)]) {
  const String vkSdkPath = env_var_scratch(string_lit("VULKAN_SDK"));

  u32 count = 0;
#ifdef VOLO_WIN32
  outNames[count++] = string_lit("SPIRV-Tools-shared.dll");
  if (!string_is_empty(vulkanSdkPath)) {
    outNames[count++] = path_build_scratch(vulkanSdkPath, string_lit("Bin/SPIRV-Tools-shared.dll"));
  }
#elif VOLO_LINUX
  outNames[count++] = string_lit("libSPIRV-Tools-shared.so");
  if (!string_is_empty(vkSdkPath)) {
    outNames[count++] = path_build_scratch(vkSdkPath, string_lit("lib/libSPIRV-Tools-shared.so"));
  }
#endif

  return count;
}

static bool spirvtools_init(SpirvTools* spirvTools) {
  diag_assert(spirvTools->state == SpirvToolsState_Idle && !spirvTools->lib);

  String    libNames[spirvtools_names_max];
  const u32 libNameCount = spirvtools_lib_names(libNames);

  DynLibResult loadRes = dynlib_load_first(g_allocHeap, libNames, libNameCount, &spirvTools->lib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load 'SPIRV-Tools' library", log_param("err", fmt_text(err)));
    goto Failure;
  }

#define SPVTOOLS_LOAD_SYM(_NAME_)                                                                  \
  do {                                                                                             \
    spirvTools->_NAME_ = dynlib_symbol(spirvTools->lib, string_lit(#_NAME_));                      \
    if (!spirvTools->_NAME_) {                                                                     \
      log_w("SpirvTools symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));    \
      goto Failure;                                                                                \
    }                                                                                              \
  } while (false)

  SPVTOOLS_LOAD_SYM(spvContextCreate);
  SPVTOOLS_LOAD_SYM(spvContextDestroy);

#undef SPVTOOLS_LOAD_SYM

  spirvTools->ctx = spirvTools->spvContextCreate(SpvTargetEnv_Vulkan_1_1);

  log_i("Loaded 'SPIRV-Tools' library", log_param("path", fmt_path(dynlib_path(spirvTools->lib))));
  spirvTools->state = SpirvToolsState_Ready;
  return true;

Failure:
  spirvTools->state = SpirvToolsState_Failed;
  return false;
}

RvkDisassembler* rvk_disassembler_create(Allocator* alloc) {
  RvkDisassembler* disassembler = alloc_alloc_t(alloc, RvkDisassembler);

  *disassembler = (RvkDisassembler){
      .alloc     = alloc,
      .initMutex = thread_mutex_create(alloc),
  };

  return disassembler;
}

void rvk_disassembler_destroy(RvkDisassembler* dis) {
  if (dis->spirvTools.ctx) {
    dis->spirvTools.spvContextDestroy(dis->spirvTools.ctx);
  }
  if (dis->spirvTools.lib) {
    dynlib_destroy(dis->spirvTools.lib);
  }
  thread_mutex_destroy(dis->initMutex);
  alloc_free_t(dis->alloc, dis);
}

RvkDisassemblerResult
rvk_disassembler_spirv_to_text(const RvkDisassembler* dis, const String in, DynString* out) {
  /**
   * Lazily initialize the SpirvTools library.
   */
  if (dis->spirvTools.state == SpirvToolsState_Idle) {
    thread_mutex_lock(dis->initMutex);
    if (dis->spirvTools.state == SpirvToolsState_Idle) {
      spirvtools_init((SpirvTools*)&dis->spirvTools);
    }
    thread_mutex_unlock(dis->initMutex);
  }
  if (dis->spirvTools.state == SpirvToolsState_Failed) {
    return RvkDisassembler_Unavailable;
  }
  (void)in;
  (void)out;
  return RvkDisassembler_InvalidAssembly;
}
