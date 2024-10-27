#include "core_diag.h"
#include "core_stringtable.h"
#include "script_enum.h"
#include "script_panic.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

ASSERT((script_enum_max_entries % 8) == 0, "Only multiple of 8 max entry counts are supported");

INLINE_HINT static u32 script_enum_find_name(const ScriptEnum* e, const StringHash nameHash) {
#ifdef VOLO_SIMD
  const SimdVec targetVec = simd_vec_broadcast_u32(nameHash);
  for (u32 i = 0; i != script_enum_max_entries; i += 8) {
    const SimdVec eqA    = simd_vec_eq_u32(simd_vec_load(e->nameHashes + i), targetVec);
    const SimdVec eqB    = simd_vec_eq_u32(simd_vec_load(e->nameHashes + i + 4), targetVec);
    const u32     eqMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(eqA, eqB));
    if (eqMask) {
      return i + intrinsic_ctz_32(eqMask) / 2; // Div 2 due to 16 bit entries.
    }
  }
#else
  for (u32 i = 0; i != script_enum_max_entries; ++i) {
    if (e->nameHashes[i] == nameHash) {
      return i;
    }
  }
#endif
  return sentinel_u32;
}

INLINE_HINT static u32 script_enum_find_value(const ScriptEnum* e, const i32 value) {
#ifdef VOLO_SIMD
  const SimdVec targetVec = simd_vec_broadcast_i32(value);
  for (u32 i = 0; i != script_enum_max_entries; i += 8) {
    const SimdVec eqA    = simd_vec_eq_u32(simd_vec_load(e->values + i), targetVec);
    const SimdVec eqB    = simd_vec_eq_u32(simd_vec_load(e->values + i + 4), targetVec);
    const u32     eqMask = simd_vec_mask_u8(simd_vec_pack_u32_to_u16(eqA, eqB));
    if (eqMask) {
      return i + intrinsic_ctz_32(eqMask) / 2; // Div 2 due to 16 bit entries.
    }
  }
#else
  for (u32 i = 0; i != script_enum_max_entries; ++i) {
    if (e->values[i] == value) {
      return i;
    }
  }
#endif
  return sentinel_u32;
}

void script_enum_push(ScriptEnum* e, const String name, const i32 value) {
  const StringHash nameHash = stringtable_add(g_stringtable, name);
  diag_assert_msg(!script_enum_contains_name(e, nameHash), "Duplicate name in ScriptEnum");

  const u32 index = script_enum_find_name(e, 0 /* unused nameHash */);
  diag_assert_msg(!sentinel_check(index), "ScriptEnum entry count exceeds max");

  e->nameHashes[index] = nameHash;
  e->values[index]     = value;
}

bool script_enum_contains_name(const ScriptEnum* e, const StringHash nameHash) {
  const u32 index = script_enum_find_name(e, nameHash);
  return !sentinel_check(index);
}

i32 script_enum_lookup_value(const ScriptEnum* e, const StringHash nameHash, ScriptPanic* panic) {
  const u32 index = script_enum_find_name(e, nameHash);
  if (sentinel_check(index)) {
    return *panic = (ScriptPanic){ScriptPanic_EnumInvalidEntry}, 0;
  }
  return e->values[index];
}

i32 script_enum_lookup_maybe_value(const ScriptEnum* e, const StringHash nameHash, const i32 def) {
  const u32 index = script_enum_find_name(e, nameHash);
  if (sentinel_check(index)) {
    return def;
  }
  return e->values[index];
}

StringHash script_enum_lookup_name(const ScriptEnum* e, const i32 value) {
  const u32 index = script_enum_find_value(e, value);
  if (sentinel_check(index)) {
    return 0;
  }
  // NOTE: Index can point to an unused entry but in that case nameHashes will always be zero.
  return e->nameHashes[index];
}
