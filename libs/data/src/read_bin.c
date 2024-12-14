#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "data_read.h"
#include "data_utils.h"

#include "registry_internal.h"

#define VOLO_DATA_VALIDATE_CHECKSUMS 0

static const String g_dataBinMagic = string_static("VOLO");

#define result_success()                                                                           \
  (DataReadResult) { 0 }

#define result_fail(_ERR_, _MSG_FORMAT_LIT_, ...)                                                  \
  (DataReadResult) {                                                                               \
    .error = (_ERR_), .errorMsg = fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__)                 \
  }

#define result_fail_truncated()                                                                    \
  (DataReadResult) { .error = DataReadError_Malformed, .errorMsg = string_lit("Input truncated") }

typedef struct {
  const DataReg* reg;
  Allocator*     alloc;
  DynArray*      allocations;
  String         input;
  DataMeta       meta;
  Mem            data;
} ReadCtx;

static usize data_meta_size_unchecked(const DataReg* reg, const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None:
    return data_decl_unchecked(reg, meta.type)->size;
  case DataContainer_Pointer:
    return sizeof(void*);
  case DataContainer_InlineArray:
    return data_decl_unchecked(reg, meta.type)->size * meta.fixedCount;
  case DataContainer_HeapArray:
    return sizeof(HeapArray);
  case DataContainer_DynArray:
    return sizeof(DynArray);
  }
  diag_crash();
}

INLINE_HINT static void bin_mem_consume_inplace(Mem* mem, const usize amount) {
  mem->ptr = bits_ptr_offset(mem->ptr, amount);
  mem->size -= amount;
}

INLINE_HINT static bool bin_pop_u8(ReadCtx* ctx, u8* out) {
  if (UNLIKELY(ctx->input.size < sizeof(u8))) {
    return false;
  }
  *out = *mem_begin(ctx->input);
  bin_mem_consume_inplace(&ctx->input, 1);
  return true;
}

INLINE_HINT static bool bin_pop_u16(ReadCtx* ctx, u16* out) {
  if (UNLIKELY(ctx->input.size < sizeof(u16))) {
    return false;
  }
  u8* data = mem_begin(ctx->input);
  *out     = (u16)data[0] | (u16)data[1] << 8;
  bin_mem_consume_inplace(&ctx->input, 2);
  return true;
}

INLINE_HINT static bool bin_pop_u32(ReadCtx* ctx, u32* out) {
  if (UNLIKELY(ctx->input.size < sizeof(u32))) {
    return false;
  }
  u8* data = mem_begin(ctx->input);
  *out     = (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
  bin_mem_consume_inplace(&ctx->input, 4);
  return true;
}

INLINE_HINT static bool bin_pop_u64(ReadCtx* ctx, u64* out) {
  if (UNLIKELY(ctx->input.size < sizeof(u64))) {
    return false;
  }
  u8* data = mem_begin(ctx->input);
  *out =
      ((u64)data[0] | (u64)data[1] << 8 | (u64)data[2] << 16 | (u64)data[3] << 24 |
       (u64)data[4] << 32 | (u64)data[5] << 40 | (u64)data[6] << 48 | (u64)data[7] << 56);
  bin_mem_consume_inplace(&ctx->input, 8);
  return true;
}

INLINE_HINT static bool bin_pop_f16(ReadCtx* ctx, f16* out) {
  if (UNLIKELY(ctx->input.size < sizeof(f16))) {
    return false;
  }
  u8* data   = mem_begin(ctx->input);
  *(u16*)out = (u16)data[0] | (u16)data[1] << 8;
  bin_mem_consume_inplace(&ctx->input, 2);
  return true;
}

INLINE_HINT static bool bin_pop_f32(ReadCtx* ctx, f32* out) {
  if (UNLIKELY(ctx->input.size < sizeof(f32))) {
    return false;
  }
  u8* data   = mem_begin(ctx->input);
  *(u32*)out = (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
  bin_mem_consume_inplace(&ctx->input, 4);
  return true;
}

INLINE_HINT static bool bin_pop_f64(ReadCtx* ctx, f64* out) {
  if (UNLIKELY(ctx->input.size < sizeof(f64))) {
    return false;
  }
  u8* data = mem_begin(ctx->input);
  *(u64*)out =
      ((u64)data[0] | (u64)data[1] << 8 | (u64)data[2] << 16 | (u64)data[3] << 24 |
       (u64)data[4] << 32 | (u64)data[5] << 40 | (u64)data[6] << 48 | (u64)data[7] << 56);
  bin_mem_consume_inplace(&ctx->input, 8);
  return true;
}

static bool bin_pop_bytes(ReadCtx* ctx, const usize bytes, Mem* out) {
  if (UNLIKELY(ctx->input.size < bytes)) {
    return false;
  }
  *out       = mem_slice(ctx->input, 0, bytes);
  ctx->input = mem_consume(ctx->input, bytes);
  return true;
}

static bool bin_pop_mem(ReadCtx* ctx, Mem* out) {
  u64 size;
  if (UNLIKELY(!bin_pop_u64(ctx, &size))) {
    return false;
  }
  /**
   * NOTE: No endianness conversion is done so its the callers choice what endianness to use.
   */
  return bin_pop_bytes(ctx, (usize)size, out);
}

static bool bin_pop_padding(ReadCtx* ctx) {
  u8 padding;
  if (UNLIKELY(!bin_pop_u8(ctx, &padding))) {
    return false;
  }
  Mem paddingMem;
  return bin_pop_bytes(ctx, padding, &paddingMem);
}

static void data_read_bin_header_internal(ReadCtx* ctx, DataBinHeader* out, DataReadResult* res) {
  Mem inMagic;
  if (!bin_pop_bytes(ctx, g_dataBinMagic.size, &inMagic) || !mem_eq(inMagic, g_dataBinMagic)) {
    *res = result_fail(DataReadError_Malformed, "Input mismatched magic");
    return;
  }
  if (!bin_pop_u32(ctx, &out->protocolVersion)) {
    goto Truncated;
  }
  if (!out->protocolVersion || out->protocolVersion > 3) {
    *res = result_fail(
        DataReadError_Incompatible,
        "Input protocol version {} is unsupported",
        fmt_int(out->protocolVersion));
    return;
  }

  if (out->protocolVersion == 1) {
    out->checksum = 0; // Version 1 had no checksum.
  } else if (!bin_pop_u32(ctx, &out->checksum)) {
    goto Truncated;
  }

  if (!bin_pop_u32(ctx, &out->metaTypeNameHash)) {
    goto Truncated;
  }
  if (!bin_pop_u32(ctx, &out->metaFormatHash)) {
    goto Truncated;
  }

  u8 metaContainerVal, metaFlagsVal;
  if (!bin_pop_u8(ctx, &metaContainerVal)) {
    goto Truncated;
  }
  if (!bin_pop_u8(ctx, &metaFlagsVal)) {
    goto Truncated;
  }
  out->metaContainer = (DataContainer)metaContainerVal;
  out->metaFlags     = (DataFlags)metaFlagsVal;

  if (!bin_pop_u16(ctx, &out->metaFixedCount)) {
    goto Truncated;
  }

  *res = result_success();
  return;

Truncated:
  *res = result_fail_truncated();
}

static void data_read_bin_val(ReadCtx*, DataReadResult*);

/**
 * Track allocations so they can be undone in case of an error.
 */
static void data_register_alloc(ReadCtx* ctx, const Mem allocation) {
  *dynarray_push_t(ctx->allocations, Mem) = allocation;
}

NO_INLINE_HINT static void data_read_bin_number(ReadCtx* ctx, DataReadResult* res) {
  /**
   * NOTE: For signed values we assume the host system is using 2's complement integers.
   */
  const DataDecl* decl = data_decl_unchecked(ctx->reg, ctx->meta.type);

  // clang-format off
  switch (decl->kind) {
  case DataKind_i8:
  case DataKind_u8:
    if (LIKELY(bin_pop_u8(ctx, ctx->data.ptr)))  { goto Success; } else { goto Trunc; }
  case DataKind_i16:
  case DataKind_u16:
    if (LIKELY(bin_pop_u16(ctx, ctx->data.ptr))) { goto Success; } else { goto Trunc; }
  case DataKind_i32:
  case DataKind_u32:
    if (LIKELY(bin_pop_u32(ctx, ctx->data.ptr))) { goto Success; } else { goto Trunc; }
  case DataKind_i64:
  case DataKind_u64:
  case DataKind_TimeDuration:
    if (LIKELY(bin_pop_u64(ctx, ctx->data.ptr))) { goto Success; } else { goto Trunc; }
  case DataKind_f16:
    if (LIKELY(bin_pop_f16(ctx, ctx->data.ptr))) { goto Success; } else { goto Trunc; }
  case DataKind_f32:
  case DataKind_Angle:
    if (LIKELY(bin_pop_f32(ctx, ctx->data.ptr))) { goto Success; } else { goto Trunc; }
  case DataKind_f64:
    if (LIKELY(bin_pop_f64(ctx, ctx->data.ptr))) { goto Success; } else { goto Trunc; }
  default:
    UNREACHABLE
  }
  // clang-format on
Success:
  *res = result_success();
  return;

Trunc:
  *res = result_fail_truncated();
}

NO_INLINE_HINT static void data_read_bin_bool(ReadCtx* ctx, DataReadResult* res) {
  u8 val;
  if (UNLIKELY(!bin_pop_u8(ctx, &val))) {
    *res = result_fail_truncated();
    return;
  }
  *mem_as_t(ctx->data, bool) = (bool)val;
  *res                       = result_success();
}

NO_INLINE_HINT static void data_read_bin_string(ReadCtx* ctx, DataReadResult* res) {
  Mem val;
  if (UNLIKELY(!bin_pop_mem(ctx, &val))) {
    *res = result_fail_truncated();
    return;
  }
  if (string_is_empty(val)) {
    *mem_as_t(ctx->data, String) = string_empty;
  } else {
    if (ctx->meta.flags & DataFlags_Intern) {
      *mem_as_t(ctx->data, String) = stringtable_intern(g_stringtable, val);
    } else {
      const String str = string_dup(ctx->alloc, val);
      data_register_alloc(ctx, str);
      *mem_as_t(ctx->data, String) = str;
    }
  }
  *res = result_success();
}

NO_INLINE_HINT static void data_read_bin_string_hash(ReadCtx* ctx, DataReadResult* res) {
  u32 val;
  if (UNLIKELY(!bin_pop_u32(ctx, &val))) {
    *res = result_fail_truncated();
    return;
  }
  *(StringHash*)ctx->data.ptr = val;
  *res                        = result_success();
}

static usize data_read_bin_mem_align(const usize size) {
  const usize biggestPow2 = u64_lit(1) << bits_ctz(size);
  return math_min(biggestPow2, data_type_mem_align_max);
}

NO_INLINE_HINT static void data_read_bin_mem(ReadCtx* ctx, DataReadResult* res) {
  if (ctx->meta.flags & DataFlags_ExternalMemory && UNLIKELY(!bin_pop_padding(ctx))) {
    *res = result_fail_truncated();
    return;
  }
  Mem val;
  if (UNLIKELY(!bin_pop_mem(ctx, &val))) {
    *res = result_fail_truncated();
    return;
  }
  if (!val.size) {
    *mem_as_t(ctx->data, DataMem) = data_mem_create(mem_empty);
    *res                          = result_success();
    return;
  }

  const usize reqAlign = data_read_bin_mem_align(val.size);
  /**
   * NOTE: Even though we've padded the data it not be aligned if the data start was not
   * sufficiently aligned.
   */
  const bool valIsAligned = bits_aligned_ptr(val.ptr, reqAlign);
  if (ctx->meta.flags & DataFlags_ExternalMemory && valIsAligned) {
    *mem_as_t(ctx->data, DataMem) = data_mem_create_ext(val);
  } else {
    const Mem copy = alloc_alloc(ctx->alloc, val.size, reqAlign);
    mem_cpy(copy, val);

    data_register_alloc(ctx, copy);
    *mem_as_t(ctx->data, DataMem) = data_mem_create(copy);
  }

  *res = result_success();
}

NO_INLINE_HINT static void data_read_bin_struct(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl_unchecked(ctx->reg, ctx->meta.type);

  if (decl->val_struct.hasHole) {
    mem_set(ctx->data, 0); // Initialize non-specified memory to zero.
  }

  ReadCtx fieldCtx = {
      .reg         = ctx->reg,
      .alloc       = ctx->alloc,
      .allocations = ctx->allocations,
      .input       = ctx->input,
  };

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    fieldCtx.meta      = fieldDecl->meta;
    fieldCtx.data.ptr  = bits_ptr_offset(ctx->data.ptr, fieldDecl->offset);
    fieldCtx.data.size = data_meta_size_unchecked(ctx->reg, fieldDecl->meta);

    data_read_bin_val(&fieldCtx, res);
    if (UNLIKELY(res->error)) {
      *res = result_fail(
          DataReadError_InvalidField,
          "Invalid field '{}': {}",
          fmt_text(fieldDecl->id.name),
          fmt_text(res->errorMsg));
      return;
    }
  }
  ctx->input = fieldCtx.input; // Consume data that was taken up by the field.
  *res       = result_success();
}

static const DataDeclChoice* data_read_bin_union_choice(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl_unchecked(ctx->reg, ctx->meta.type);

  u32 tag;
  if (UNLIKELY(!bin_pop_u32(ctx, &tag))) {
    *res = result_fail_truncated();
    return null;
  }

  dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choice) {
    if (choice->tag == (i32)tag) {
      *res = result_success();
      return choice;
    }
  }

  *res = result_fail(
      DataReadError_UnionTypeUnsupported,
      "Invalid union tag '{}' for union {}",
      fmt_int((i32)tag),
      fmt_text(decl->id.name));
  return null;
}

NO_INLINE_HINT static void data_read_bin_union(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl*       decl   = data_decl_unchecked(ctx->reg, ctx->meta.type);
  const DataDeclChoice* choice = data_read_bin_union_choice(ctx, res);
  if (UNLIKELY(res->error)) {
    return;
  }

  mem_set(ctx->data, 0); // Initialize non-specified memory to zero.

  *data_union_tag(&decl->val_union, ctx->data) = choice->tag;

  switch (data_union_name_type(&decl->val_union)) {
  case DataUnionNameType_None:
    break;
  case DataUnionNameType_String: {
    Mem nameMem;
    if (UNLIKELY(!bin_pop_mem(ctx, &nameMem))) {
      *res = result_fail_truncated();
      return;
    }
    if (!string_is_empty(nameMem)) {
      const String name = string_dup(ctx->alloc, nameMem);
      data_register_alloc(ctx, name);
      *data_union_name_string(&decl->val_union, ctx->data) = name;
    }
  } break;
  case DataUnionNameType_StringHash: {
    StringHash* out = data_union_name_hash(&decl->val_union, ctx->data);
    if (UNLIKELY(!bin_pop_u32(ctx, out))) {
      *res = result_fail_truncated();
      return;
    }
  } break;
  }

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    ReadCtx choiceCtx = {
        .reg         = ctx->reg,
        .alloc       = ctx->alloc,
        .allocations = ctx->allocations,
        .input       = ctx->input,
        .meta        = choice->meta,
        .data        = data_choice_mem(ctx->reg, choice, ctx->data),
    };
    data_read_bin_val(&choiceCtx, res);
    if (UNLIKELY(res->error)) {
      *res = result_fail(
          DataReadError_UnionDataInvalid,
          "Invalid union data '{}': {}",
          fmt_text(choice->id.name),
          fmt_text(res->errorMsg));
      return;
    }
    ctx->input = choiceCtx.input; // Consume data that was taken up by the choice.
  }

  *res = result_success();
}

NO_INLINE_HINT static void data_read_bin_enum(ReadCtx* ctx, DataReadResult* res) {
  u32 val;
  if (UNLIKELY(!bin_pop_u32(ctx, &val))) {
    *res = result_fail_truncated();
    return;
  }
  *(i32*)ctx->data.ptr = (i32)val;
  *res                 = result_success();
}

NO_INLINE_HINT static void data_read_bin_opaque(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl_unchecked(ctx->reg, ctx->meta.type);
  Mem             bytes;
  if (UNLIKELY(!bin_pop_bytes(ctx, decl->size, &bytes))) {
    *res = result_fail_truncated();
    return;
  }
  diag_assert(ctx->data.size == decl->size);
  /**
   * NOTE: No endianness conversion is done so its important that file and host endianess match.
   */
  mem_cpy(ctx->data, bytes);
  *res = result_success();
}

INLINE_HINT static void data_read_bin_val_single(ReadCtx* ctx, DataReadResult* res) {
  switch (data_decl_unchecked(ctx->reg, ctx->meta.type)->kind) {
  case DataKind_bool:
    data_read_bin_bool(ctx, res);
    return;
  case DataKind_i8:
  case DataKind_i16:
  case DataKind_i32:
  case DataKind_i64:
  case DataKind_u8:
  case DataKind_u16:
  case DataKind_u32:
  case DataKind_u64:
  case DataKind_f16:
  case DataKind_f32:
  case DataKind_f64:
  case DataKind_TimeDuration:
  case DataKind_Angle:
    data_read_bin_number(ctx, res);
    return;
  case DataKind_String:
    data_read_bin_string(ctx, res);
    return;
  case DataKind_StringHash:
    data_read_bin_string_hash(ctx, res);
    return;
  case DataKind_DataMem:
    data_read_bin_mem(ctx, res);
    return;
  case DataKind_Struct:
    data_read_bin_struct(ctx, res);
    return;
  case DataKind_Union:
    data_read_bin_union(ctx, res);
    return;
  case DataKind_Enum:
    data_read_bin_enum(ctx, res);
    return;
  case DataKind_Opaque:
    data_read_bin_opaque(ctx, res);
    return;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static void data_read_bin_val_pointer(ReadCtx* ctx, DataReadResult* res) {
  u8 hasVal;
  if (UNLIKELY(!bin_pop_u8(ctx, &hasVal))) {
    *res = result_fail_truncated();
    return;
  }
  if (!hasVal) {
    *mem_as_t(ctx->data, void*) = null;
    *res                        = result_success();
    return;
  }

  const DataDecl* decl = data_decl_unchecked(ctx->reg, ctx->meta.type);
  const Mem       mem  = alloc_alloc(ctx->alloc, decl->size, decl->align);
  data_register_alloc(ctx, mem);

  ReadCtx subCtx = {
      .reg         = ctx->reg,
      .alloc       = ctx->alloc,
      .allocations = ctx->allocations,
      .input       = ctx->input,
      .meta        = data_meta_base(ctx->meta),
      .data        = mem,
  };
  data_read_bin_val_single(&subCtx, res);
  *mem_as_t(ctx->data, void*) = mem.ptr;
  ctx->input                  = subCtx.input; // Consume data that was taken up by the value.
}

static void data_read_bin_elems(ReadCtx* ctx, const usize count, void* out, DataReadResult* res) {
  const DataDecl* decl    = data_decl_unchecked(ctx->reg, ctx->meta.type);
  const void*     dataEnd = bits_ptr_offset(out, decl->size * count);

  ReadCtx elemCtx = {
      .reg         = ctx->reg,
      .alloc       = ctx->alloc,
      .allocations = ctx->allocations,
      .input       = ctx->input,
      .meta        = data_meta_base(ctx->meta),
      .data        = mem_create(out, decl->size),
  };

  while (elemCtx.data.ptr < dataEnd) {
    data_read_bin_val_single(&elemCtx, res);
    if (UNLIKELY(res->error)) {
      return;
    }
    elemCtx.data.ptr = bits_ptr_offset(elemCtx.data.ptr, decl->size);
  }
  ctx->input = elemCtx.input; // Consume data that was taken up by the element.
  *res       = result_success();
}

static void data_read_bin_val_inline_array(ReadCtx* ctx, DataReadResult* res) {
  if (UNLIKELY(!ctx->meta.fixedCount)) {
    diag_crash_msg("Inline-arrays need at least 1 entry");
  }
  if (UNLIKELY(ctx->data.size != data_meta_size_unchecked(ctx->reg, ctx->meta))) {
    diag_crash_msg("Unexpected data-size for inline array");
  }
  data_read_bin_elems(ctx, ctx->meta.fixedCount, ctx->data.ptr, res);
}

static void data_read_bin_val_heap_array(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl_unchecked(ctx->reg, ctx->meta.type);

  u64 count;
  if (UNLIKELY(!bin_pop_u64(ctx, &count))) {
    *res = result_fail_truncated();
    return;
  }

  if (!count) {
    *mem_as_t(ctx->data, HeapArray) = (HeapArray){0};
    *res                            = result_success();
    return;
  }
  const Mem arrayMem = alloc_alloc(ctx->alloc, decl->size * count, decl->align);
  data_register_alloc(ctx, arrayMem);

  void* ptr                       = arrayMem.ptr;
  *mem_as_t(ctx->data, HeapArray) = (HeapArray){.values = arrayMem.ptr, .count = count};

  data_read_bin_elems(ctx, count, ptr, res);
}

static void data_read_bin_val_dynarray(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl_unchecked(ctx->reg, ctx->meta.type);

  u64 count;
  if (UNLIKELY(!bin_pop_u64(ctx, &count))) {
    *res = result_fail_truncated();
    return;
  }

  DynArray* out = mem_as_t(ctx->data, DynArray);
  *out          = dynarray_create(ctx->alloc, (u32)decl->size, (u16)decl->align, 0);

  if (!count) {
    *res = result_success();
    return;
  }
  dynarray_resize(out, count);
  data_register_alloc(ctx, out->data);

  data_read_bin_elems(ctx, count, out->data.ptr, res);
}

static void data_read_bin_val(ReadCtx* ctx, DataReadResult* res) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_read_bin_val_single(ctx, res);
    return;
  case DataContainer_Pointer:
    data_read_bin_val_pointer(ctx, res);
    return;
  case DataContainer_InlineArray:
    data_read_bin_val_inline_array(ctx, res);
    return;
  case DataContainer_HeapArray:
    data_read_bin_val_heap_array(ctx, res);
    return;
  case DataContainer_DynArray:
    data_read_bin_val_dynarray(ctx, res);
    return;
  }
  diag_crash();
}

static void data_read_bin_stringhash_values(ReadCtx* ctx, DataReadResult* res) {
  u32 count;
  if (!bin_pop_u32(ctx, &count)) {
    goto Truncated;
  }
  for (u32 i = 0; i != count; ++i) {
    u8 length;
    if (!bin_pop_u8(ctx, &length)) {
      goto Truncated;
    }
    String str;
    if (!bin_pop_bytes(ctx, length, &str)) {
      goto Truncated;
    }
    stringtable_add(g_stringtable, str);
  }
  *res = result_success();
  return;

Truncated:
  *res = result_fail_truncated();
}

String data_read_bin(
    const DataReg*  reg,
    const String    input,
    Allocator*      alloc,
    const DataMeta  meta,
    Mem             data,
    DataReadResult* res) {

  DynArray allocations = dynarray_create_t(g_allocHeap, Mem, 0);

  ReadCtx ctx = {
      .reg         = reg,
      .alloc       = alloc,
      .allocations = &allocations,
      .input       = input,
      .meta        = meta,
      .data        = data,
  };
  DataBinHeader header;
  data_read_bin_header_internal(&ctx, &header, res);
  if (UNLIKELY(res->error)) {
    goto Ret;
  }
#if VOLO_DATA_VALIDATE_CHECKSUMS
  if (UNLIKELY(header.checksum && header.checksum != data_read_bin_checksum(input))) {
    *res = result_fail(DataReadError_Corrupted, "Checksum mismatch");
    goto Ret;
  }
#endif
  if (UNLIKELY(header.metaTypeNameHash != data_name_hash(reg, meta.type))) {
    *res = result_fail(DataReadError_Incompatible, "Input mismatched type name");
    goto Ret;
  }
  if (UNLIKELY(header.metaContainer != meta.container)) {
    *res = result_fail(DataReadError_Incompatible, "Input mismatched meta container");
    goto Ret;
  }
  if (UNLIKELY(header.metaFlags != meta.flags)) {
    *res = result_fail(DataReadError_Incompatible, "Input mismatched meta flags");
    goto Ret;
  }
  if (UNLIKELY(header.metaFormatHash != data_hash(reg, meta, DataHashFlags_ExcludeIds))) {
    *res = result_fail(DataReadError_Incompatible, "Input mismatched format hash");
    goto Ret;
  }
  data_read_bin_val(&ctx, res);

  if (header.protocolVersion >= 3) {
    data_read_bin_stringhash_values(&ctx, res);
  }

Ret:
  if (res->error) {
    /**
     * Free all allocations in case of an error.
     * This way the caller doesn't have to attempt to cleanup a half initialized object.
     */
    dynarray_for_t(&allocations, Mem, mem) { alloc_free(alloc, *mem); }
    mem_set(data, 0);
  }
  dynarray_destroy(&allocations);
  return ctx.input;
}

String data_read_bin_header(const String input, DataBinHeader* out, DataReadResult* res) {
  ReadCtx ctx = {
      .input = input,
  };
  data_read_bin_header_internal(&ctx, out, res);
  return ctx.input;
}

u32 data_read_bin_checksum(const String input) {
  const usize offset = g_dataBinMagic.size + sizeof(u32) /* version */ + sizeof(u32) /* checksum */;
  if (UNLIKELY(input.size < offset)) {
    return 0; // Invalid data blob.
  }
  return bits_crc_32(0, mem_consume(input, offset));
}
