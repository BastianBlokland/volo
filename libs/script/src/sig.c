#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_sentinel.h"
#include "script_sig.h"

/**
 * Signature, stores the return type and names and types for arguments.
 *
 * Memory layout is a ScriptSig header followed by the following data per argument:
 * - ScriptMask (2 bytes)
 * - nameLen (1 byte)
 * - Name (nameLen bytes)
 * - Padding (? bytes)
 *
 */

ASSERT(script_sig_arg_name_max <= u8_max, "Argument name length has to be storable in a byte");

static usize sig_arg_data_size(const ScriptSigArg arg) {
  diag_assert_msg(arg.name.size <= script_sig_arg_name_max, "Argument name length exceeds max");
  return sizeof(ScriptMask) + 1 /* nameLen */ + arg.name.size;
}

struct sScriptSig {
  Allocator* alloc;
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
  result += bits_padding(result, alignof(ScriptSig));
  return result;
}

ScriptSig* script_sig_create(
    Allocator* alloc, const ScriptMask ret, const ScriptSigArg args[], const u8 argCount) {
  diag_assert_msg(argCount <= script_sig_arg_count_max, "Argument count exceeds max");

  usize allocSize = sizeof(ScriptSig);
  for (u8 i = 0; i != argCount; ++i) {
    allocSize += sig_arg_data_size(args[i]);
    allocSize += bits_padding(allocSize, alignof(ScriptMask));
  }
  allocSize += bits_padding(allocSize, alignof(ScriptSig));

  ScriptSig* sig = alloc_alloc(alloc, allocSize, alignof(ScriptSig)).ptr;
  sig->alloc     = alloc;
  sig->retMask   = ret;
  sig->argCount  = argCount;

  usize offset = sizeof(ScriptSig);
  for (u8 i = 0; i != argCount; ++i) {
    diag_assert(offset < u16_max);
    diag_assert(bits_aligned(offset, alignof(ScriptMask)));

    sig->argOffsets[i] = (u16)offset;

    const ScriptMask mask = args[i].mask;
    const String     name = args[i].name;

    *(ScriptMask*)bits_ptr_offset(sig, offset)              = mask;
    *(u8*)bits_ptr_offset(sig, offset + sizeof(ScriptMask)) = (u8)name.size;
    mem_cpy(mem_create(bits_ptr_offset(sig, offset + sizeof(ScriptMask) + 1), name.size), name);

    offset += sig_arg_data_size(args[i]);
    offset += bits_padding(offset, alignof(ScriptMask));
  }
  diag_assert(bits_align(offset, alignof(ScriptSig)) == allocSize);

  return sig;
}

ScriptSig* script_sig_clone(Allocator* alloc, const ScriptSig* sig) {
  const usize dataSize = sig_data_size(sig);

  Mem newSigMem = alloc_alloc(alloc, dataSize, alignof(ScriptSig));
  mem_cpy(newSigMem, mem_create(sig, dataSize));

  ScriptSig* newSig = newSigMem.ptr;
  newSig->alloc     = alloc;

  return newSig;
}

void script_sig_destroy(ScriptSig* sig) {
  const usize dataSize = sig_data_size(sig);
  alloc_free(sig->alloc, mem_create(sig, dataSize));
}

ScriptMask script_sig_ret(const ScriptSig* sig) { return sig->retMask; }

u8 script_sig_arg_count(const ScriptSig* sig) { return sig->argCount; }

ScriptSigArg script_sig_arg(const ScriptSig* sig, const u8 index) {
  diag_assert_msg(index < script_sig_arg_count_max, "Argument index exceeds maximum");

  const u16 offset = sig->argOffsets[index];
  diag_assert_msg(!sentinel_check(offset), "Argument index out of bounds");

  const ScriptMask mask    = *(ScriptMask*)bits_ptr_offset(sig, offset);
  const u8         nameLen = *(u8*)bits_ptr_offset(sig, offset + sizeof(ScriptMask));
  const String name = mem_create(bits_ptr_offset(sig, offset + sizeof(ScriptMask) + 1), nameLen);

  return (ScriptSigArg){.mask = mask, .name = name};
}
