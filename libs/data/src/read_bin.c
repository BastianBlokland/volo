#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "data_read.h"
#include "data_utils.h"

#include "registry_internal.h"

static const String g_dataBinMagic   = string_static("VOLO");
static const u32    g_dataBinVersion = 1;

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
  const DataMeta meta;
  Mem            data;
} ReadCtx;

static bool bin_pop_u8(ReadCtx* ctx, u8* out) {
  if (UNLIKELY(ctx->input.size < sizeof(u8))) {
    return false;
  }
  ctx->input = mem_consume_u8(ctx->input, out);
  return true;
}

static bool bin_pop_u16(ReadCtx* ctx, u16* out) {
  if (UNLIKELY(ctx->input.size < sizeof(u16))) {
    return false;
  }
  ctx->input = mem_consume_le_u16(ctx->input, out);
  return true;
}

static bool bin_pop_u32(ReadCtx* ctx, u32* out) {
  if (UNLIKELY(ctx->input.size < sizeof(u32))) {
    return false;
  }
  ctx->input = mem_consume_le_u32(ctx->input, out);
  return true;
}

static bool bin_pop_u64(ReadCtx* ctx, u64* out) {
  if (UNLIKELY(ctx->input.size < sizeof(u64))) {
    return false;
  }
  ctx->input = mem_consume_le_u64(ctx->input, out);
  return true;
}

static bool bin_pop_f32(ReadCtx* ctx, f32* out) {
  if (UNLIKELY(ctx->input.size < sizeof(f32))) {
    return false;
  }
  mem_cpy(mem_create(out, sizeof(f32)), ctx->input);
  ctx->input = mem_consume(ctx->input, sizeof(f32));
  return true;
}

static bool bin_pop_f64(ReadCtx* ctx, f64* out) {
  if (UNLIKELY(ctx->input.size < sizeof(f64))) {
    return false;
  }
  mem_cpy(mem_create(out, sizeof(f64)), ctx->input);
  ctx->input = mem_consume(ctx->input, sizeof(f64));
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

static void data_read_bin_header(ReadCtx* ctx, DataReadResult* res) {
  Mem inMagic;
  if (!bin_pop_bytes(ctx, g_dataBinMagic.size, &inMagic) || !mem_eq(inMagic, g_dataBinMagic)) {
    *res = result_fail(DataReadError_Malformed, "Input mismatched magic");
    return;
  }
  u32 inFormatVersion = 0;
  if (!bin_pop_u32(ctx, &inFormatVersion) || inFormatVersion != g_dataBinVersion) {
    *res = result_fail(
        DataReadError_Malformed, "Input format {} is unsupported", fmt_int(inFormatVersion));
    return;
  }
  const u32 typeNameHash   = data_name_hash(ctx->reg, ctx->meta.type);
  u32       inTypeNameHash = 0;
  if (!bin_pop_u32(ctx, &inTypeNameHash) || inTypeNameHash != typeNameHash) {
    *res = result_fail(DataReadError_Malformed, "Input mismatched type");
    return;
  }
  const u32 typeHash   = data_hash(ctx->reg, ctx->meta, DataHashFlags_ExcludeIds);
  u32       intypeHash = 0;
  if (!bin_pop_u32(ctx, &intypeHash) || intypeHash != typeHash) {
    *res = result_fail(DataReadError_Malformed, "Input mismatched type hash");
    return;
  }
  *res = result_success();
}

static void data_read_bin_val(ReadCtx*, DataReadResult*);

/**
 * Track allocations so they can be undone in case of an error.
 */
static void data_register_alloc(ReadCtx* ctx, const Mem allocation) {
  *dynarray_push_t(ctx->allocations, Mem) = allocation;
}

static void data_read_bin_number(ReadCtx* ctx, DataReadResult* res) {
  /**
   * NOTE: For signed values we assume the host system is using 2's complement integers.
   */
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  // clang-format off
  switch (decl->kind) {
  case DataKind_i8:
  case DataKind_u8:
    if (LIKELY(bin_pop_u8(ctx, mem_as_t(ctx->data, u8))))   { goto Success; } else { goto Trunc; }
  case DataKind_i16:
  case DataKind_u16:
    if (LIKELY(bin_pop_u16(ctx, mem_as_t(ctx->data, u16)))) { goto Success; } else { goto Trunc; }
  case DataKind_i32:
  case DataKind_u32:
    if (LIKELY(bin_pop_u32(ctx, mem_as_t(ctx->data, u32)))) { goto Success; } else { goto Trunc; }
  case DataKind_i64:
  case DataKind_u64:
    if (LIKELY(bin_pop_u64(ctx, mem_as_t(ctx->data, u64)))) { goto Success; } else { goto Trunc; }
  case DataKind_f32:
    if (LIKELY(bin_pop_f32(ctx, mem_as_t(ctx->data, f32)))) { goto Success; } else { goto Trunc; }
  case DataKind_f64:
    if (LIKELY(bin_pop_f64(ctx, mem_as_t(ctx->data, f64)))) { goto Success; } else { goto Trunc; }
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

static void data_read_bin_bool(ReadCtx* ctx, DataReadResult* res) {
  u8 val;
  if (UNLIKELY(!bin_pop_u8(ctx, &val))) {
    *res = result_fail_truncated();
    return;
  }
  *mem_as_t(ctx->data, bool) = (bool)val;
  *res                       = result_success();
}

static void data_read_bin_string(ReadCtx* ctx, DataReadResult* res) {
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

static usize data_read_bin_mem_align(const usize size) {
  const usize biggestPow2 = u64_lit(1) << bits_ctz(size);
  return math_min(biggestPow2, data_type_mem_align_max);
}

static void data_read_bin_mem(ReadCtx* ctx, DataReadResult* res) {
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

  const Mem mem = alloc_alloc(ctx->alloc, val.size, data_read_bin_mem_align(val.size));
  mem_cpy(mem, val);

  data_register_alloc(ctx, mem);

  *mem_as_t(ctx->data, DataMem) = data_mem_create(mem);

  *res = result_success();
}

static void data_read_bin_struct(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  mem_set(ctx->data, 0); // Initialize non-specified memory to zero.

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    ReadCtx fieldCtx = {
        .reg         = ctx->reg,
        .alloc       = ctx->alloc,
        .allocations = ctx->allocations,
        .input       = ctx->input,
        .meta        = fieldDecl->meta,
        .data        = data_field_mem(ctx->reg, fieldDecl, ctx->data),
    };
    data_read_bin_val(&fieldCtx, res);
    if (UNLIKELY(res->error)) {
      *res = result_fail(
          DataReadError_InvalidField,
          "Invalid field '{}': {}",
          fmt_text(fieldDecl->id.name),
          fmt_text(res->errorMsg));
      return;
    }
    ctx->input = fieldCtx.input; // Consume data that was taken up by the field.
  }
  *res = result_success();
}

static const DataDeclChoice* data_read_bin_union_choice(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

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

static void data_read_bin_union(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl*       decl   = data_decl(ctx->reg, ctx->meta.type);
  const DataDeclChoice* choice = data_read_bin_union_choice(ctx, res);
  if (UNLIKELY(res->error)) {
    return;
  }

  mem_set(ctx->data, 0); // Initialize non-specified memory to zero.

  *data_union_tag(&decl->val_union, ctx->data) = choice->tag;

  String* unionNamePtr = data_union_name(&decl->val_union, ctx->data);
  if (unionNamePtr) {
    Mem nameMem;
    if (UNLIKELY(!bin_pop_mem(ctx, &nameMem))) {
      *res = result_fail_truncated();
      return;
    }
    if (!string_is_empty(nameMem)) {
      const String name = string_dup(ctx->alloc, nameMem);
      data_register_alloc(ctx, name);
      *unionNamePtr = name;
    }
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

static void data_read_bin_enum(ReadCtx* ctx, DataReadResult* res) {
  u32 val;
  if (UNLIKELY(!bin_pop_u32(ctx, &val))) {
    *res = result_fail_truncated();
    return;
  }
  *mem_as_t(ctx->data, i32) = (i32)val;

  *res = result_success();
}

static void data_read_bin_val_single(ReadCtx* ctx, DataReadResult* res) {
  switch (data_decl(ctx->reg, ctx->meta.type)->kind) {
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
  case DataKind_f32:
  case DataKind_f64:
    data_read_bin_number(ctx, res);
    return;
  case DataKind_String:
    data_read_bin_string(ctx, res);
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

  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
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

static void data_read_bin_val_array(ReadCtx* ctx, DataReadResult* res) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  u64 count;
  if (UNLIKELY(!bin_pop_u64(ctx, &count))) {
    *res = result_fail_truncated();
    return;
  }

  if (!count) {
    *mem_as_t(ctx->data, DataArray) = (DataArray){0};
    *res                            = result_success();
    return;
  }
  const Mem arrayMem = alloc_alloc(ctx->alloc, decl->size * count, decl->align);
  data_register_alloc(ctx, arrayMem);

  void* ptr                       = arrayMem.ptr;
  *mem_as_t(ctx->data, DataArray) = (DataArray){.values = arrayMem.ptr, .count = count};

  for (u64 i = 0; i != count; ++i) {
    ReadCtx elemCtx = {
        .reg         = ctx->reg,
        .alloc       = ctx->alloc,
        .allocations = ctx->allocations,
        .input       = ctx->input,
        .meta        = data_meta_base(ctx->meta),
        .data        = mem_create(ptr, decl->size),
    };
    data_read_bin_val_single(&elemCtx, res);
    if (UNLIKELY(res->error)) {
      return;
    }
    ptr        = bits_ptr_offset(ptr, decl->size);
    ctx->input = elemCtx.input; // Consume data that was taken up by the element.
  }
  *res = result_success();
}

static void data_read_bin_val(ReadCtx* ctx, DataReadResult* res) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_read_bin_val_single(ctx, res);
    return;
  case DataContainer_Pointer:
    data_read_bin_val_pointer(ctx, res);
    return;
  case DataContainer_Array:
    data_read_bin_val_array(ctx, res);
    return;
  }
  diag_crash();
}

String data_read_bin(
    const DataReg*  reg,
    const String    input,
    Allocator*      alloc,
    const DataMeta  meta,
    Mem             data,
    DataReadResult* res) {

  DynArray allocations = dynarray_create_t(g_allocHeap, Mem, 64);

  ReadCtx ctx = {
      .reg         = reg,
      .alloc       = alloc,
      .allocations = &allocations,
      .input       = input,
      .meta        = meta,
      .data        = data,
  };
  data_read_bin_header(&ctx, res);
  if (res->error) {
    goto Ret;
  }
  data_read_bin_val(&ctx, res);

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
