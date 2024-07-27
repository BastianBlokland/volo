#include "core_bits.h"
#include "core_diag.h"
#include "data_utils.h"

#include "registry_internal.h"

typedef struct {
  const DataReg*      reg;
  const DataHashFlags flags;
  const DataMeta      meta;
} HashCtx;

static u32 data_hash_internal(const HashCtx*);

static u32 data_hash_struct(const HashCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  diag_assert(decl->kind == DataKind_Struct);

  u32 hash = bits_hash_32_val((u32)decl->val_struct.fields.size);

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const HashCtx fieldCtx = {
        .reg   = ctx->reg,
        .flags = ctx->flags,
        .meta  = fieldDecl->meta,
    };
    const u32 fieldHash = data_hash_internal(&fieldCtx);

    if (!(ctx->flags & DataHashFlags_ExcludeIds)) {
      hash = bits_hash_32_combine(hash, fieldDecl->id.hash);
    }
    hash = bits_hash_32_combine(hash, fieldHash);
  }

  return hash;
}

static u32 data_hash_union(const HashCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  diag_assert(decl->kind == DataKind_Union);

  u32 hash = bits_hash_32_val((u32)decl->val_union.choices.size);

  const bool hasName = data_union_has_name(&decl->val_union);
  hash               = bits_hash_32_combine(hash, bits_hash_32_val(hasName));

  dynarray_for_t(&decl->val_union.choices, DataDeclChoice, choiceDecl) {
    const bool emptyChoice = choiceDecl->meta.type == 0;

    const u32 choiceTagHash = bits_hash_32_val(choiceDecl->tag);

    u32 choiceValHash;
    if (emptyChoice) {
      choiceValHash = bits_hash_32_val(42);
    } else {
      const HashCtx choiceCtx = {
          .reg   = ctx->reg,
          .flags = ctx->flags,
          .meta  = choiceDecl->meta,
      };
      choiceValHash = data_hash_internal(&choiceCtx);
    }

    if (!(ctx->flags & DataHashFlags_ExcludeIds)) {
      hash = bits_hash_32_combine(hash, choiceDecl->id.hash);
    }
    hash = bits_hash_32_combine(hash, choiceTagHash);
    hash = bits_hash_32_combine(hash, choiceValHash);
  }

  return hash;
}

static u32 data_hash_enum(const HashCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  u32 hash = bits_hash_32_val((u32)decl->val_enum.consts.size);

  dynarray_for_t(&decl->val_enum.consts, DataDeclConst, constDecl) {
    const u32 constValHash = bits_hash_32_val((u32)constDecl->value);

    if (!(ctx->flags & DataHashFlags_ExcludeIds)) {
      hash = bits_hash_32_combine(hash, constDecl->id.hash);
    }
    hash = bits_hash_32_combine(hash, constValHash);
  }

  return hash;
}

static u32 data_hash_single(const HashCtx* ctx) {
  const DataKind kind = data_decl(ctx->reg, ctx->meta.type)->kind;
  switch (kind) {
  case DataKind_bool:
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
  case DataKind_String:
  case DataKind_Mem:
    return bits_hash_32_val(kind);
  case DataKind_Struct:
    return data_hash_struct(ctx);
  case DataKind_Union:
    return data_hash_union(ctx);
  case DataKind_Enum:
    return data_hash_enum(ctx);
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static u32 data_hash_flags(const DataFlags flags) {
  static const DataFlags g_hashedFlags = DataFlags_NotEmpty;
  return bits_hash_32_val(flags & g_hashedFlags);
}

static u32 data_hash_internal(const HashCtx* ctx) {
  const u32 containerHash = bits_hash_32_val(ctx->meta.container);
  const u32 flagsHash     = data_hash_flags(ctx->meta.flags);

  u32 res = data_hash_single(ctx);
  res     = bits_hash_32_combine(res, containerHash);
  res     = bits_hash_32_combine(res, flagsHash);
  return res;
}

u32 data_hash(const DataReg* reg, const DataMeta meta, const DataHashFlags flags) {
  const HashCtx ctx = {
      .reg   = reg,
      .flags = flags,
      .meta  = meta,
  };
  return data_hash_internal(&ctx);
}
