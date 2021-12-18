#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_types.h"

usize bitset_size(BitSet bits) { return bytes_to_bits(bits.size); }

bool bitset_test(BitSet bits, usize idx) {
  const usize byteIdx = bits_to_bytes(idx);
  if (byteIdx >= bits.size) {
    return false;
  }
  return (*mem_at_u8(bits, byteIdx) & (1u << bit_in_byte(idx))) != 0;
}

usize bitset_count(BitSet bits) {
  usize result = 0;
  mem_for_u8(bits, itr) { result += bits_popcnt_32(*itr); }
  return result;
}

bool bitset_any(BitSet bits) {
  mem_for_u8(bits, itr) {
    if (*itr) {
      return true;
    }
  }
  return false;
}

bool bitset_any_of(BitSet bits, BitSet other) {
  const usize byteCount = math_min(bits.size, other.size);
  for (usize i = 0; i != byteCount; ++i) {
    u8* byte      = mem_at_u8(bits, i);
    u8* otherByte = mem_at_u8(other, i);
    if (*otherByte & *byte) {
      return true;
    }
  }
  return false;
}

bool bitset_all_of(BitSet bits, BitSet other) {
  diag_assert(bits.size >= other.size);
  for (usize i = 0; i != other.size; ++i) {
    u8* byte      = mem_at_u8(bits, i);
    u8* otherByte = mem_at_u8(other, i);
    if ((*byte & *otherByte) != *otherByte) {
      return false;
    }
  }
  return true;
}

usize bitset_next(BitSet bits, usize idx) {
  if (UNLIKELY(idx >= bitset_size(bits))) {
    return sentinel_usize;
  }
  usize byteIdx = bits_to_bytes(idx);
  u8    byte    = *mem_at_u8(bits, byteIdx) >> bit_in_byte(idx);
  if (byte) {
    return idx + bits_ctz_32(byte);
  }
  for (++byteIdx; byteIdx != bits.size; ++byteIdx) {
    byte = *mem_at_u8(bits, byteIdx);
    if (byte) {
      return bytes_to_bits(byteIdx) + bits_ctz_32(byte);
    }
  }
  return sentinel_usize;
}

usize bitset_index(BitSet bits, usize idx) {
  diag_assert(bitset_test(bits, idx));
  usize    byteIdx = bits_to_bytes(idx);
  const u8 byte    = (u8)(*mem_at_u8(bits, byteIdx) << (8 - bit_in_byte(idx)));
  usize    result  = bits_popcnt_32(byte);
  while (byteIdx) {
    --byteIdx;
    result += bits_popcnt_32(*mem_at_u8(bits, byteIdx));
  }
  return result;
}

void bitset_set(BitSet bits, usize idx) {
  diag_assert(idx < bitset_size(bits));
  *mem_at_u8(bits, bits_to_bytes(idx)) |= 1u << bit_in_byte(idx);
}

void bitset_set_all(BitSet bits, usize idx) {
  diag_assert(idx < bitset_size(bits));
  const usize byteIdx = bits_to_bytes(idx);

  // Set all bytes before the last byte to all ones.
  mem_set(mem_slice(bits, 0, byteIdx), u8_lit(0b11111111));

  // Set the remaining bits in the last byte.
  switch (idx - bytes_to_bits(byteIdx)) {
  case 7:
    *mem_at_u8(bits, byteIdx) |= 0b1111111;
  case 6:
    *mem_at_u8(bits, byteIdx) |= 0b111111;
  case 5:
    *mem_at_u8(bits, byteIdx) |= 0b11111;
  case 4:
    *mem_at_u8(bits, byteIdx) |= 0b1111;
  case 3:
    *mem_at_u8(bits, byteIdx) |= 0b111;
  case 2:
    *mem_at_u8(bits, byteIdx) |= 0b11;
  case 1:
    *mem_at_u8(bits, byteIdx) |= 0b1;
  case 0:
    break;
  default:
    diag_crash_msg("Invalid state");
  }
}

void bitset_clear(BitSet bits, usize idx) {
  diag_assert(idx < bitset_size(bits));
  *mem_at_u8(bits, bits_to_bytes(idx)) &= ~(1u << bit_in_byte(idx));
}

void bitset_clear_all(BitSet bits) { mem_set(bits, 0); }

void bitset_or(BitSet bits, BitSet other) {
  diag_assert(bits.size >= other.size);
  for (usize i = 0; i != other.size; ++i) {
    u8* byte      = mem_at_u8(bits, i);
    u8* otherByte = mem_at_u8(other, i);
    *byte         = *byte | *otherByte;
  }
}

void bitset_and(BitSet bits, BitSet other) {
  diag_assert(bits.size <= other.size);
  for (usize i = 0; i != bits.size; ++i) {
    u8* byte      = mem_at_u8(bits, i);
    u8* otherByte = mem_at_u8(other, i);
    *byte         = *byte & *otherByte;
  }
}

void bitset_xor(BitSet bits, BitSet other) {
  diag_assert(bits.size <= other.size);
  for (usize i = 0; i != bits.size; ++i) {
    u8* byte      = mem_at_u8(bits, i);
    u8* otherByte = mem_at_u8(other, i);
    *byte         = *byte ^ *otherByte;
  }
}
