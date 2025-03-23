#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_sentinel.h"
#include "script_sig.h"
#include "script_val.h"

/**
 * Signature, stores the return type and names and types for arguments.
 *
 * Memory layout is a ScriptSig header followed by the following data per argument:
 * - ScriptMask (2 bytes)
 * - ScriptSigArgFlags (1 byte)
 * - nameLen (1 byte)
 * - Name (nameLen bytes)
 * - Padding (? bytes)
 *
 */

ASSERT(script_sig_arg_name_max <= u8_max, "Argument name length has to be storable in a byte");

static usize sig_arg_data_size(const ScriptSigArg arg) {
  diag_assert_msg(arg.name.size <= script_sig_arg_name_max, "Argument name length exceeds max");
  return sizeof(ScriptMask) + 1 /* flags */ + 1 /* nameLen */ + arg.name.size;
}

struct sScriptSig {
  ScriptMask retMask;
  u8         argCount;
  u16        argOffsets[script_sig_arg_count_max];
};

static usize sig_data_size(const ScriptSig* sig) {
  if (!sig->argCount) {
    return sizeof(ScriptSig);
  }
  usize result = sig->argOffsets[sig->argCount - 1];
  result += sig_arg_data_size(script_sig_arg(sig, sig->argCount - 1));
  result += sized_call(bits_padding, result, alignof(ScriptSig));
  return result;
}

ScriptSig* script_sig_create(
    Allocator* alloc, const ScriptMask ret, const ScriptSigArg args[], const u8 argCount) {
  diag_assert_msg(argCount <= script_sig_arg_count_max, "Argument count exceeds max");

  usize allocSize = sizeof(ScriptSig);
  for (u8 i = 0; i != argCount; ++i) {
    allocSize += sig_arg_data_size(args[i]);
    allocSize += sized_call(bits_padding, allocSize, alignof(ScriptMask));
  }
  allocSize += sized_call(bits_padding, allocSize, alignof(ScriptSig));

  ScriptSig* sig = alloc_alloc(alloc, allocSize, alignof(ScriptSig)).ptr;
  sig->retMask   = ret;
  sig->argCount  = argCount;

  usize offset = sizeof(ScriptSig);
  for (u8 i = 0; i != argCount; ++i) {
    diag_assert(offset < u16_max);
    diag_assert(bits_aligned(offset, alignof(ScriptMask)));

    sig->argOffsets[i] = (u16)offset;

    const ScriptMask        mask  = args[i].mask;
    const ScriptSigArgFlags flags = args[i].flags;
    const String            name  = args[i].name;

    *(ScriptMask*)bits_ptr_offset(sig, offset)                  = mask;
    *(u8*)bits_ptr_offset(sig, offset + sizeof(ScriptMask))     = flags;
    *(u8*)bits_ptr_offset(sig, offset + sizeof(ScriptMask) + 1) = (u8)name.size;
    mem_cpy(mem_create(bits_ptr_offset(sig, offset + sizeof(ScriptMask) + 2), name.size), name);

    offset += sig_arg_data_size(args[i]);
    offset += sized_call(bits_padding, offset, alignof(ScriptMask));
  }
  diag_assert(sized_call(bits_align, offset, alignof(ScriptSig)) == allocSize);

  return sig;
}

ScriptSig* script_sig_clone(Allocator* alloc, const ScriptSig* sig) {
  const usize dataSize = sig_data_size(sig);

  Mem newSigMem = alloc_alloc(alloc, dataSize, alignof(ScriptSig));
  mem_cpy(newSigMem, mem_create(sig, dataSize));

  return newSigMem.ptr;
}

void script_sig_destroy(ScriptSig* sig, Allocator* alloc) {
  const usize dataSize = sig_data_size(sig);
  alloc_free(alloc, mem_create(sig, dataSize));
}

ScriptMask script_sig_ret(const ScriptSig* sig) { return sig->retMask; }

u8 script_sig_arg_count(const ScriptSig* sig) { return sig->argCount; }

ScriptSigArg script_sig_arg(const ScriptSig* sig, const u8 index) {
  diag_assert_msg(index < sig->argCount, "Argument index exceeds maximum");

  const u16 offset = sig->argOffsets[index];
  diag_assert_msg(!sentinel_check(offset), "Argument index out of bounds");

  const ScriptMask        mask    = *(ScriptMask*)bits_ptr_offset(sig, offset);
  const ScriptSigArgFlags flags   = *(u8*)bits_ptr_offset(sig, offset + sizeof(ScriptMask));
  const u8                nameLen = *(u8*)bits_ptr_offset(sig, offset + sizeof(ScriptMask) + 1);
  const String name = mem_create(bits_ptr_offset(sig, offset + sizeof(ScriptMask) + 2), nameLen);

  return (ScriptSigArg){.mask = mask, .flags = flags, .name = name};
}

u8 script_sig_arg_min_count(const ScriptSig* sig) {
  u8 minCount = 0;
  for (; minCount != sig->argCount; ++minCount) {
    if (script_sig_arg(sig, minCount).mask & (1 << ScriptType_Null)) {
      break;
    }
  }
  return minCount;
}

u8 script_sig_arg_max_count(const ScriptSig* sig) {
  for (u8 i = 0; i != sig->argCount; ++i) {
    if (script_sig_arg(sig, i).flags & ScriptSigArgFlags_Multi) {
      return u8_max;
    }
  }
  return sig->argCount;
}

void script_sig_arg_write(const ScriptSig* sig, const u8 index, DynString* str) {
  const ScriptSigArg arg = script_sig_arg(sig, index);
  dynstring_append(str, arg.name);
  if (arg.mask && arg.mask != script_mask_null) {
    dynstring_append(str, string_lit(": "));
    script_mask_write(arg.mask, str);
  }
  if (arg.flags & ScriptSigArgFlags_Multi) {
    dynstring_append(str, string_lit("..."));
  }
}

String script_sig_arg_scratch(const ScriptSig* sig, const u8 index) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, 64, 1);
  DynString str        = dynstring_create_over(scratchMem);

  script_sig_arg_write(sig, index, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

void script_sig_write(const ScriptSig* sig, DynString* str) {
  dynstring_append_char(str, '(');
  for (u8 i = 0; i != sig->argCount; ++i) {
    if (i) {
      dynstring_append(str, string_lit(", "));
    }
    script_sig_arg_write(sig, i, str);
  }
  dynstring_append_char(str, ')');
  if (sig->retMask && sig->retMask != script_mask_null) {
    dynstring_append(str, string_lit(" -> "));
    script_mask_write(sig->retMask, str);
  }
}

String script_sig_scratch(const ScriptSig* sig) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, 512, 1);
  DynString str        = dynstring_create_over(scratchMem);

  script_sig_write(sig, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}
