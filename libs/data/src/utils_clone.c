#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "data_utils.h"

#include "registry_internal.h"

typedef struct {
  const DataReg* reg;
  Allocator*     alloc;
  DataMeta       meta;
  Mem            original, clone;
} CloneCtx;

static void data_clone_internal(const CloneCtx*);

static void data_clone_string(const CloneCtx* ctx) {
  const String originalVal = *mem_as_t(ctx->original, String);
  if (ctx->meta.flags & DataFlags_Intern) {
    *mem_as_t(ctx->clone, String) = originalVal;
  } else {
    *mem_as_t(ctx->clone, String) = string_maybe_dup(ctx->alloc, originalVal);
  }
}

static usize data_clone_mem_align(const usize size) {
  const usize biggestPow2 = usize_lit(1) << sized_call(bits_ctz, size);
  return math_min(biggestPow2, data_type_mem_align_max);
}

static void data_clone_mem(const CloneCtx* ctx) {
  const DataMem originalMem = *mem_as_t(ctx->original, DataMem);
  if (mem_valid(originalMem)) {
    if (originalMem.external) {
      *mem_as_t(ctx->clone, DataMem) = originalMem;
    } else {
      const usize align              = data_clone_mem_align(originalMem.size);
      const Mem   dup                = alloc_dup(ctx->alloc, data_mem(originalMem), align);
      *mem_as_t(ctx->clone, DataMem) = data_mem_create(dup);
    }
  } else {
    *mem_as_t(ctx->clone, DataMem) = data_mem_create(mem_empty);
  }
}

static void data_clone_struct(const CloneCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);

  mem_set(ctx->clone, 0); // Initialize non-specified memory to zero.

  dynarray_for_t(&decl->val_struct.fields, DataDeclField, fieldDecl) {
    const Mem originalFieldMem = data_field_mem(ctx->reg, fieldDecl, ctx->original);
    const Mem dataFieldMem     = data_field_mem(ctx->reg, fieldDecl, ctx->clone);

    const CloneCtx fieldCtx = {
        .reg      = ctx->reg,
        .alloc    = ctx->alloc,
        .meta     = fieldDecl->meta,
        .original = originalFieldMem,
        .clone    = dataFieldMem,
    };
    data_clone_internal(&fieldCtx);
  }
}

static void data_clone_union(const CloneCtx* ctx) {
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  const i32       tag  = *data_union_tag(&decl->val_union, ctx->original);

  mem_set(ctx->clone, 0); // Initialize non-specified memory to zero.

  *data_union_tag(&decl->val_union, ctx->clone) = tag;

  switch (data_union_name_type(&decl->val_union)) {
  case DataUnionNameType_None:
    break;
  case DataUnionNameType_String: {
    const String* nameStr = data_union_name_string(&decl->val_union, ctx->original);
    *data_union_name_string(&decl->val_union, ctx->clone) = string_maybe_dup(ctx->alloc, *nameStr);
  } break;
  case DataUnionNameType_StringHash: {
    const StringHash* nameHash = data_union_name_hash(&decl->val_union, ctx->original);
    *data_union_name_hash(&decl->val_union, ctx->clone) = *nameHash;
  } break;
  }

  const DataDeclChoice* choice = data_choice_from_tag(&decl->val_union, tag);
  diag_assert(choice);

  const bool emptyChoice = choice->meta.type == 0;
  if (!emptyChoice) {
    const Mem originalChoiceMem = data_choice_mem(ctx->reg, choice, ctx->original);
    const Mem choiceMem         = data_choice_mem(ctx->reg, choice, ctx->clone);

    const CloneCtx choiceCtx = {
        .reg      = ctx->reg,
        .alloc    = ctx->alloc,
        .meta     = choice->meta,
        .original = originalChoiceMem,
        .clone    = choiceMem,
    };
    data_clone_internal(&choiceCtx);
  }
}

static void data_clone_single(const CloneCtx* ctx) {
  switch (data_decl(ctx->reg, ctx->meta.type)->kind) {
  case DataKind_bool:
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
  case DataKind_Enum:
  case DataKind_StringHash:
  case DataKind_Opaque:
    mem_cpy(ctx->clone, ctx->original);
    return;
  case DataKind_String:
    data_clone_string(ctx);
    return;
  case DataKind_DataMem:
    data_clone_mem(ctx);
    return;
  case DataKind_Struct:
    data_clone_struct(ctx);
    return;
  case DataKind_Union:
    data_clone_union(ctx);
    return;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static void data_clone_pointer(const CloneCtx* ctx) {
  void* originalPtr = *mem_as_t(ctx->original, void*);
  if (!originalPtr) {
    *mem_as_t(ctx->clone, void*) = null;
    return;
  }

  const DataDecl* decl         = data_decl(ctx->reg, ctx->meta.type);
  const Mem       originalMem  = mem_create(originalPtr, decl->size);
  const Mem       newMem       = alloc_alloc(ctx->alloc, decl->size, decl->align);
  *mem_as_t(ctx->clone, void*) = newMem.ptr;

  const CloneCtx subCtx = {
      .reg      = ctx->reg,
      .alloc    = ctx->alloc,
      .meta     = data_meta_base(ctx->meta),
      .original = originalMem,
      .clone    = newMem,
  };
  data_clone_single(&subCtx);
}

static void data_clone_inline_array(const CloneCtx* ctx) {
  if (UNLIKELY(!ctx->meta.fixedCount)) {
    diag_crash_msg("Inline-arrays need at least 1 entry");
  }
  const usize expectedSize = data_meta_size(ctx->reg, ctx->meta);
  if (UNLIKELY(ctx->original.size != expectedSize || ctx->clone.size != expectedSize)) {
    diag_crash_msg("Unexpected data-size for inline array");
  }
  const DataDecl* decl = data_decl(ctx->reg, ctx->meta.type);
  for (u16 i = 0; i != ctx->meta.fixedCount; ++i) {
    const CloneCtx elemCtx = {
        .reg      = ctx->reg,
        .alloc    = ctx->alloc,
        .meta     = data_meta_base(ctx->meta),
        .original = mem_create(bits_ptr_offset(ctx->original.ptr, decl->size * i), decl->size),
        .clone    = mem_create(bits_ptr_offset(ctx->clone.ptr, decl->size * i), decl->size),
    };
    data_clone_single(&elemCtx);
  }
}

static void data_clone_heap_array(const CloneCtx* ctx) {
  const DataDecl*  decl          = data_decl(ctx->reg, ctx->meta.type);
  const HeapArray* originalArray = mem_as_t(ctx->original, HeapArray);
  const usize      count         = originalArray->count;
  if (!count) {
    *mem_as_t(ctx->clone, HeapArray) = (HeapArray){0};
    return;
  }

  const Mem  newArrayMem = alloc_alloc(ctx->alloc, decl->size * count, decl->align);
  HeapArray* newArray    = mem_as_t(ctx->clone, HeapArray);
  *newArray              = (HeapArray){.values = newArrayMem.ptr, .count = count};

  for (usize i = 0; i != count; ++i) {
    const CloneCtx elemCtx = {
        .reg      = ctx->reg,
        .alloc    = ctx->alloc,
        .meta     = data_meta_base(ctx->meta),
        .original = data_elem_mem(decl, originalArray, i),
        .clone    = data_elem_mem(decl, newArray, i),
    };
    data_clone_single(&elemCtx);
  }
}

static void data_clone_dynarray(const CloneCtx* ctx) {
  const DataDecl* decl          = data_decl(ctx->reg, ctx->meta.type);
  const DynArray* originalArray = mem_as_t(ctx->original, DynArray);

  DynArray* newArray = mem_as_t(ctx->clone, DynArray);
  *newArray          = dynarray_create(ctx->alloc, (u32)decl->size, (u16)decl->align, 0);

  dynarray_resize(newArray, originalArray->size);

  for (usize i = 0; i != originalArray->size; ++i) {
    const CloneCtx elemCtx = {
        .reg      = ctx->reg,
        .alloc    = ctx->alloc,
        .meta     = data_meta_base(ctx->meta),
        .original = dynarray_at(originalArray, i, 1),
        .clone    = dynarray_at(newArray, i, 1),
    };
    data_clone_single(&elemCtx);
  }
}

static void data_clone_internal(const CloneCtx* ctx) {
  switch (ctx->meta.container) {
  case DataContainer_None:
    data_clone_single(ctx);
    return;
  case DataContainer_Pointer:
    data_clone_pointer(ctx);
    return;
  case DataContainer_InlineArray:
    data_clone_inline_array(ctx);
    return;
  case DataContainer_HeapArray:
    data_clone_heap_array(ctx);
    return;
  case DataContainer_DynArray:
    data_clone_dynarray(ctx);
    return;
  }
  diag_crash();
}

void data_clone(
    const DataReg* reg,
    Allocator*     alloc,
    const DataMeta meta,
    const Mem      original,
    const Mem      clone) {
  diag_assert(original.size == data_meta_size(reg, meta));
  diag_assert(clone.size == data_meta_size(reg, meta));

  const CloneCtx ctx = {
      .reg      = reg,
      .alloc    = alloc,
      .meta     = meta,
      .original = original,
      .clone    = clone,
  };
  data_clone_internal(&ctx);
}
