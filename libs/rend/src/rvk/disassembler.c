#include "core/alloc.h"
#include "core/bits.h"
#include "core/diag.h"
#include "core/dynlib.h"
#include "core/dynstring.h"
#include "core/env.h"
#include "core/path.h"
#include "core/thread.h"
#include "log/logger.h"

#include "disassembler.h"

#define spirvtools_names_max 4

typedef enum {
  SpvToolsState_Idle,
  SpvToolsState_Ready,
  SpvToolsState_Failed,
} SpvToolsState;

typedef enum {
  SpvTargetEnv_Vulkan_1_1 = 18,
} SpvTargetEnv;

typedef enum {
  SpvBinaryToTextOpts_None           = 1 << 0,
  SpvBinaryToTextOpts_Print          = 1 << 1,
  SpvBinaryToTextOpts_Color          = 1 << 2,
  SpvBinaryToTextOpts_Indent         = 1 << 3,
  SpvBinaryToTextOpts_ShowByteOffset = 1 << 4,
  SpvBinaryToTextOpts_NoHeader       = 1 << 5,
  SpvBinaryToTextOpts_FriendlyNames  = 1 << 6,
  SpvBinaryToTextOpts_Comment        = 1 << 7,
  SpvBinaryToTextOpts_NestedIndent   = 1 << 8,
  SpvBinaryToTextOpts_ReorderBlocks  = 1 << 9,
} SpvBinaryToTextOpts;

typedef enum {
  SpvResult_Success,
} SpvResult;

typedef struct {
  const char* str;
  usize       length;
} SpvText;

typedef struct sSpvContext SpvContext;

typedef struct {
  SpvToolsState state;
  DynLib*       lib;
  SpvContext*   ctx;

  // clang-format off
  SpvContext* (SYS_DECL* spvContextCreate)(SpvTargetEnv);
  void        (SYS_DECL* spvContextDestroy)(SpvContext*);
  SpvResult   (SYS_DECL* spvBinaryToText)(const SpvContext*, const u32* data, usize wordCount, u32 options, SpvText** out, void* diagnostic);
  void        (SYS_DECL* spvTextDestroy)(SpvText*);
  // clang-format on
} SpvTools;

typedef struct sRvkDisassembler {
  Allocator*  alloc;
  ThreadMutex initMutex;
  SpvTools    spvTools;
} RvkDisassembler;

static u32 spvtools_lib_names(String outNames[PARAM_ARRAY_SIZE(spirvtools_names_max)]) {
  const String vkSdkPath = env_var_scratch(string_lit("VULKAN_SDK"));

  u32 count = 0;
#ifdef VOLO_WIN32
  outNames[count++] = string_lit("SPIRV-Tools-shared.dll");
  if (!string_is_empty(vkSdkPath)) {
    outNames[count++] = path_build_scratch(vkSdkPath, string_lit("Bin/SPIRV-Tools-shared.dll"));
  }
#elif VOLO_LINUX
  outNames[count++] = string_lit("libSPIRV-Tools-shared.so");
  if (!string_is_empty(vkSdkPath)) {
    outNames[count++] = path_build_scratch(vkSdkPath, string_lit("lib/libSPIRV-Tools-shared.so"));
  }
#endif

  return count;
}

static bool spvtools_init(SpvTools* spvTools) {
  diag_assert(spvTools->state == SpvToolsState_Idle && !spvTools->lib);

  String    libNames[spirvtools_names_max];
  const u32 libNameCount = spvtools_lib_names(libNames);

  DynLibResult loadRes = dynlib_load_first(g_allocHeap, libNames, libNameCount, &spvTools->lib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load 'SPIRV-Tools' library", log_param("err", fmt_text(err)));
    goto Failure;
  }

#define SPVTOOLS_LOAD_SYM(_NAME_)                                                                  \
  do {                                                                                             \
    spvTools->_NAME_ = dynlib_symbol(spvTools->lib, string_lit(#_NAME_));                          \
    if (!spvTools->_NAME_) {                                                                       \
      log_e("SpirvTools symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));    \
      goto Failure;                                                                                \
    }                                                                                              \
  } while (false)

  SPVTOOLS_LOAD_SYM(spvContextCreate);
  SPVTOOLS_LOAD_SYM(spvContextDestroy);
  SPVTOOLS_LOAD_SYM(spvBinaryToText);
  SPVTOOLS_LOAD_SYM(spvTextDestroy);

#undef SPVTOOLS_LOAD_SYM

  spvTools->ctx = spvTools->spvContextCreate(SpvTargetEnv_Vulkan_1_1);
  if (!spvTools->ctx) {
    log_e("Failed to create SpirvTools context");
    goto Failure;
  }

  log_i("Loaded 'SPIRV-Tools' library", log_param("path", fmt_path(dynlib_path(spvTools->lib))));
  spvTools->state = SpvToolsState_Ready;
  return true;

Failure:
  spvTools->state = SpvToolsState_Failed;
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
  if (dis->spvTools.ctx) {
    dis->spvTools.spvContextDestroy(dis->spvTools.ctx);
  }
  if (dis->spvTools.lib) {
    dynlib_destroy(dis->spvTools.lib);
  }
  thread_mutex_destroy(dis->initMutex);
  alloc_free_t(dis->alloc, dis);
}

RvkDisassemblerResult
rvk_disassembler_spv(const RvkDisassembler* dis, const String in, DynString* out) {
  // Lazily initialize the SpirvTools library.
  if (dis->spvTools.state == SpvToolsState_Idle) {
    thread_mutex_lock(dis->initMutex);
    if (dis->spvTools.state == SpvToolsState_Idle) {
      spvtools_init((SpvTools*)&dis->spvTools);
    }
    thread_mutex_unlock(dis->initMutex);
  }
  if (dis->spvTools.state == SpvToolsState_Failed) {
    return RvkDisassembler_Unavailable;
  }
  const SpvBinaryToTextOpts options =
      SpvBinaryToTextOpts_NoHeader | SpvBinaryToTextOpts_Indent |
      SpvBinaryToTextOpts_FriendlyNames | SpvBinaryToTextOpts_Comment |
      SpvBinaryToTextOpts_NestedIndent | SpvBinaryToTextOpts_ReorderBlocks;

  SpvText*        resValue;
  const SpvResult res = dis->spvTools.spvBinaryToText(
      dis->spvTools.ctx,
      in.ptr,
      bytes_to_words(in.size),
      options,
      &resValue,
      null /* diagnostic */);

  if (res != SpvResult_Success) {
    return RvkDisassembler_InvalidAssembly;
  }

  dynstring_append(out, mem_create(resValue->str, resValue->length));
  dis->spvTools.spvTextDestroy(resValue);

  return RvkDisassembler_Success;
}
