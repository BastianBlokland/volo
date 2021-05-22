#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_types.h"

#define bitset_byte_idx(_IDX_) ((_IDX_) >> 3)     // divide by 8.
#define bitset_bit_in_byte(_IDX_) ((_IDX_)&0b111) // modulo 8.

usize bitset_size(BitSet bits) { return bits.size * 8u; }

bool bitset_test(BitSet bits, usize idx) {
  diag_assert(idx < bitset_size(bits));
  return (*mem_at_u8(bits, bitset_byte_idx(idx)) & (1u << bitset_bit_in_byte(idx))) != 0;
}

bool bitset_count(BitSet bits) {
  usize result = 0;
  mem_for_u8(bits, byte, { result += bits_popcnt(byte); });
  return result;
}

bool bitset_any(BitSet bits) {
  mem_for_u8(bits, byte, {
    if (byte) {
      return true;
    }
  });
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
  diag_assert(idx < bitset_size(bits));
  usize    byteIdx = bitset_byte_idx(idx);
  const u8 byte    = *mem_at_u8(bits, byteIdx) >> bitset_bit_in_byte(idx);
  if (byte) {
    return idx + bits_ctz(byte);
  }
  for (++byteIdx; byteIdx != bits.size; ++byteIdx) {
    const u8 byte = *mem_at_u8(bits, byteIdx);
    if (byte) {
      return (byteIdx * 8) + bits_ctz(byte);
    }
  }
  return sentinel_usize;
}

usize bitset_index(BitSet bits, usize idx) {
  diag_assert(bitset_test(bits, idx));
  usize    byteIdx = bitset_byte_idx(idx);
  const u8 byte    = *mem_at_u8(bits, byteIdx) << (8 - bitset_bit_in_byte(idx));
  usize    result  = bits_popcnt(byte);
  while (byteIdx) {
    --byteIdx;
    result += bits_popcnt(*mem_at_u8(bits, byteIdx));
  }
  return result;
}

void bitset_set(BitSet bits, usize idx) {
  diag_assert(idx < bitset_size(bits));
  *mem_at_u8(bits, bitset_byte_idx(idx)) |= 1u << bitset_bit_in_byte(idx);
}

void bitset_clear(BitSet bits, usize idx) {
  diag_assert(idx < bitset_size(bits));
  *mem_at_u8(bits, bitset_byte_idx(idx)) &= ~(1u << bitset_bit_in_byte(idx));
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
