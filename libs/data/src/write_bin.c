#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "data_utils.h"
#include "data_write.h"

#include "registry_internal.h"

static const String g_dataBinMagic   = string_static("VOLO");
static const u32    g_dataBinVersion = 1;

typedef struct {
  const DataReg* reg;
  DynString*     out;
  const DataMeta meta;
  Mem            data;
} WriteCtx;

static void bin_push_u8(const WriteCtx* ctx, const u8 val) {
  mem_write_u8(dynstring_push(ctx->out, sizeof(u8)), val);
}

static void bin_push_u16(const WriteCtx* ctx, const u16 val) {
  mem_write_le_u16(dynstring_push(ctx->out, sizeof(u16)), val);
}

static void bin_push_u32(const WriteCtx* ctx, const u32 val) {
  mem_write_le_u32(dynstring_push(ctx->out, sizeof(u32)), val);
}

static void bin_push_u64(const WriteCtx* ctx, const u64 val) {
  mem_write_le_u64(dynstring_push(ctx->out, sizeof(u64)), val);
}

static void bin_push_f32(const WriteCtx* ctx, const f32 val) {
  mem_cpy(dynstring_push(ctx->out, sizeof(f32)), mem_var(val));
}

static void bin_push_f64(const WriteCtx* ctx, const f64 val) {
  mem_cpy(dynstring_push(ctx->out, sizeof(f64)), mem_var(val));
}

static void bin_push_mem(const WriteCtx* ctx, const Mem mem) {
  if (mem_valid(mem)) {
    /**
     * NOTE: No endianness conversion is done so its the callers choice what endianness to use.
     */
    bin_push_u64(ctx, mem.size);
    mem_cpy(dynstring_push(ctx->out, mem.size), mem);
  } else {
    bin_push_u64(ctx, 0);
  }
}

static void data_write_bin_header(const WriteCtx* ctx) {
  mem_cpy(dynstring_push(ctx->out, g_dataBinMagic.size), g_dataBinMagic);
  bin_push_u32(ctx, g_dataBinVersion);
  bin_push_u32(ctx, data_name_hash(ctx->reg, ctx->meta.type));
  bin_push_u32(ctx, data_hash(ctx->reg, ctx->meta, DataHashFlags_ExcludeIds));
}

static void data_write_bin_val(const WriteCtx*);

static void data_write_bin_struct(const WriteCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const WriteCtx fieldCtx = {
        .reg  = ctx->reg,
        .out  = ctx->out,
        .meta = fieldDecl->meta,
        .data = data_field_mem(ctx->reg, fieldDecl, ctx->data),
    };
    data_write_bin_val(&fieldCtx);
  }
}

static void data_write_bin_union(const WriteCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  const i32       tag  = *data_union_tag(&decl->val_union, ctx->data);

  bin_push_u32(ctx, (u32)tag);

  const DataDeclChoice* choice = data_choice_from_tag(&decl->val_union, tag);
  diag_assert(choice);

  const String* name = data_union_name(&decl->val_union, ctx->data);
  if (name) {
    bin_push_mem(ctx, *name);
  }

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    const WriteCtx choiceCtx = {
        .reg  = ctx->reg,
        .out  = ctx->out,
        .meta = choice->meta,
        .data = data_choice_mem(ctx->reg, choice, ctx->data),
    };
    data_write_bin_val(&choiceCtx);
  }
}

static void data_write_bin_enum(const WriteCtx* ctx) {
  const i32 val = *mem_as_t(ctx->data, i32);
  bin_push_u32(ctx, (u32)val);
}

static void data_write_bin_val_single(const WriteCtx* ctx) {
  /**
   * NOTE: For signed values we assume the host system is using 2's complement integers.
   */
  switch (data_decl(ctx->reg, ctx->meta.type)->kind) {
  case DataKind_bool:
    bin_push_u8(ctx, *mem_as_t(ctx->data, bool));
    return;
  case DataKind_i8:
  case DataKind_u8:
    bin_push_u8(ctx, *mem_as_t(ctx->data, u8));
    return;
  case DataKind_i16:
  case DataKind_u16:
    bin_push_u16(ctx, *mem_as_t(ctx->data, u16));
    return;
  case DataKind_i32:
  case DataKind_u32:
    bin_push_u32(ctx, *mem_as_t(ctx->data, u32));
    return;
  case DataKind_i64:
  case DataKind_u64:
    bin_push_u64(ctx, *mem_as_t(ctx->data, u64));
    return;
  case DataKind_f32:
    bin_push_f32(ctx, *mem_as_t(ctx->data, f32));
    return;
  case DataKind_f64:
    bin_push_f64(ctx, *mem_as_t(ctx->data, f64));
    return;
  case DataKind_String:
  case DataKind_Mem:
    bin_push_mem(ctx, *mem_as_t(ctx->data, Mem));
    return;
  case DataKind_Struct:
    data_write_bin_struct(ctx);
    return;
  case DataKind_Union:
    data_write_bin_union(ctx);
    return;
  case DataKind_Enum:
    data_write_bin_enum(ctx);
    return;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static void data_write_bin_val_pointer(const WriteCtx* ctx) {
  void* ptr = *mem_as_t(ctx->data, void*);
  bin_push_u8(ctx, ptr != null);
  if (ptr) {
    const DataDecl* decl   = data_decl(ctx->reg, ctx->meta.type);
    const WriteCtx  subCtx = {
         .reg  = ctx->reg,
         .out  = ctx->out,
         .meta = data_meta_base(ctx->meta),
         .data = mem_create(ptr, decl->size),
    };
    data_write_bin_val_single(&subCtx);
  }
}

static void data_write_bin_val_array(const WriteCtx* ctx) {
  const DataDecl*  decl  = data_decl(ctx->reg, ctx->meta.type);
  const DataArray* array = mem_as_t(ctx->data, DataArray);

  bin_push_u64(ctx, array->count);

  for (usize i = 0; i != array->count; ++i) {
    const WriteCtx elemCtx = {
        .reg  = ctx->reg,
        .out  = ctx->out,
        .meta = data_meta_base(ctx->meta),
        .data = data_elem_mem(decl, array, i),
    };
    data_write_bin_val_single(&elemCtx);
  }
}

static void data_write_bin_val(const WriteCtx* ctx) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_write_bin_val_single(ctx);
    return;
  case DataContainer_Pointer:
    data_write_bin_val_pointer(ctx);
    return;
  case DataContainer_Array:
    data_write_bin_val_array(ctx);
    return;
  }
  diag_crash();
}

void data_write_bin(const DataReg* reg, DynString* str, const DataMeta meta, const Mem data) {
  const WriteCtx ctx = {
      .reg  = reg,
      .out  = str,
      .meta = meta,
      .data = data,
  };
  data_write_bin_header(&ctx);
  data_write_bin_val(&ctx);
}
