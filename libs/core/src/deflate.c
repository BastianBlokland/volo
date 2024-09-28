#include "core_alloc.h"
#include "core_annotation.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_deflate.h"
#include "core_diag.h"
#include "core_file.h"

/**
 * DEFLATE (RFC 1951) compressed data stream utilities.
 *
 * Spec: https://www.rfc-editor.org/rfc/rfc1951
 */

#define huffman_validation 0
#define huffman_max_levels 16
#define huffman_max_symbols 288

/**
 * Huffman code, encodes a path through the tree.
 * Bit 0 represents following the left child and bit 1 the right child.
 */
typedef struct {
  u16 bits;
  u16 level;
} HuffmanCode;

typedef struct {
  u16 leafNodes, internalNodes;
  u16 symbolStart;
} HuffmanLevel;

typedef struct {
  u16 level, index;
} HuffmanNode;

/**
 * Huffman tree is a binary tree that is indexed by prefix codes (HuffmanCode's) which represent
 * paths through the tree.
 *
 * Properties of the Huffman tree's as used in deflate:
 * - Symbols have consecutive values within the same tree level (the same code length).
 * - In each level there are leafCountPerLevel[] leaves on the left and internal nodes on the right.
 *
 * The structure of the tree is not explicitly stored but can be implied from these properties.
 */
typedef struct {
  u16 leafCount;
  u16 leafCountPerLevel[huffman_max_levels]; // Number of leaf nodes per tree level.
  u16 leafSymbols[huffman_max_symbols];
} HuffmanTree;

typedef struct {
  String     input;
  u32        inputBitIndex;
  DynString* out;
} InflateCtx;

static HuffmanTree g_fixedLiteralTree;
static HuffmanTree g_fixedDistanceTree;

/**
 * Test if we should take the left (false) or right (true) branch at the given level of the tree.
 */
static bool huffman_code_sample(const HuffmanCode code, const u16 level) {
  diag_assert(level <= code.level);
  // Huffman codes are sampled from most- to least-significant bits.
  return (code.bits & (1 << (code.level - 1 - level))) != 0;
}

static void huffman_code_write(const HuffmanCode code, DynString* out) {
  for (u16 level = 0; level != code.level; ++level) {
    dynstring_append_char(out, huffman_code_sample(code, level) ? '1' : '0');
  }
}

static u32 huffman_max_nodes_for_level(const u16 level) {
  // Because a huffman tree is a binary tree the amount of nodes is bounded by pow(2, level).
  return 1 << level;
}

/**
 * Lookup the index of the first symbol in the given level.
 */
static u16 huffman_symbol_start(const HuffmanTree* t, const u16 level) {
  diag_assert(level < huffman_max_levels);
  u16 leafCounter = 0;
  for (u16 i = 0; i != level; ++i) {
    leafCounter += t->leafCountPerLevel[i];
  }
  return leafCounter;
}

static bool huffman_is_root(const HuffmanNode node) { return node.level == 0; }

static bool huffman_is_leaf(const HuffmanTree* t, const HuffmanNode node) {
  return node.index < t->leafCountPerLevel[node.level];
}

static HuffmanNode huffman_child(const HuffmanTree* t, const HuffmanNode node, const bool right) {
  diag_assert(!huffman_is_leaf(t, node));
  const u16 internalIndex = node.index - t->leafCountPerLevel[node.level];
  // NOTE: Multiply by two as the next level as twice as many nodes.
  return (HuffmanNode){.level = node.level + 1, .index = internalIndex * 2 + right};
}

/**
 * Retrieve the huffman code (path through the tree) for each leaf node.
 */
static void huffman_codes(const HuffmanTree* t, HuffmanCode codes[]) {
  // NOTE: Start level from 1 as the root is always an internal node.
  for (u16 symbolIndex = 0, codeBits = 0, level = 1; level < huffman_max_levels; ++level) {
    codeBits <<= 1;
    for (u16 i = 0; i != t->leafCountPerLevel[level]; ++i, ++symbolIndex) {
      codes[symbolIndex] = (HuffmanCode){.bits = codeBits++, .level = level};
    }
  }
}

/**
 * Lookup the structure of each level in the tree.
 */
static void huffman_levels(const HuffmanTree* t, HuffmanLevel levels[]) {
  mem_set(mem_create(levels, sizeof(HuffmanLevel) * huffman_max_levels), 0);

  for (u16 level = huffman_max_levels; level-- != 0;) {
    levels[level].symbolStart = huffman_symbol_start(t, level);
    levels[level].leafNodes   = t->leafCountPerLevel[level];

    const u16 totalNodes = levels[level].leafNodes + levels[level].internalNodes;
    diag_assert(totalNodes <= huffman_max_nodes_for_level(level));

    if (level) {
      levels[level - 1].internalNodes = totalNodes / 2;
    } else {
      diag_assert(levels[level].leafNodes == 0);
      diag_assert(levels[level].internalNodes == 1);
    }
  }
}

/**
 * Lookup a symbol by its code (path through the tree).
 * Returns sentinel_u16 when the code does not point to a leaf node in the tree.
 */
MAYBE_UNUSED static u16 huffman_lookup(const HuffmanTree* t, const HuffmanCode code) {
  HuffmanNode node        = {.level = 0, .index = 0}; // Root node.
  u16         symbolStart = 0;

  while (node.level <= code.level) {
    node = huffman_child(t, node, huffman_code_sample(code, node.level));
    if (huffman_is_leaf(t, node)) {
      return t->leafSymbols[symbolStart + node.index];
    }
    symbolStart += t->leafCountPerLevel[node.level];
  }
  return sentinel_u16;
}

static DeflateError huffman_build(HuffmanTree* t, const u16 symbolLevels[], const u16 symbolCount) {
  // Gather the symbol count for each level.
  mem_set(array_mem(t->leafCountPerLevel), 0);
  for (u16 i = 0; i != symbolCount; ++i) {
    const u16 level = symbolLevels[i];
    if (!level) {
      continue; // The root node is always an internal node and cannot contain a symbol.
    }
    diag_assert(level < huffman_max_levels);
    ++t->leafCountPerLevel[level];
  }

  // Compute the start symbol index for each level.
  u16 symbolStart[huffman_max_levels];
  u16 availableInternalNodes[huffman_max_levels];
  for (u16 level = 0, leafCounter = 0;; ++level) {
    symbolStart[level] = leafCounter;

    const u16 maxNodes = level ? (availableInternalNodes[level - 1] * 2) : 1;
    if (!maxNodes) {
      break; // End of the tree.
    }
    const u16 leafNodes = t->leafCountPerLevel[level];
    if (UNLIKELY(leafNodes > maxNodes)) {
      return DeflateError_Malformed; // Invalid tree.
    }
    availableInternalNodes[level] = maxNodes - leafNodes;
    leafCounter += leafNodes;
  }

  // Insert the symbols for the leaf nodes into tree.
  for (u16 i = 0; i != symbolCount; ++i) {
    const u16 level = symbolLevels[i];
    if (!level) {
      continue; // The root node is always an internal node and cannot contain a symbol.
    }
    ++t->leafCount;
    t->leafSymbols[symbolStart[level]++] = i;
  }

#if huffman_validation
  {
    HuffmanCode codes[huffman_max_symbols];
    huffman_codes(t, codes);

    for (u32 i = 0; i != symbolCount; ++i) {
      diag_assert(huffman_lookup(t, codes[i]) == t->leafSymbols[i]);
    }
  }
#endif
  return DeflateError_None;
}

/**
 * Dump the Huffman tree leaf nodes to stdout.
 * Includes the symbol value of the node and the code to reach it.
 */
MAYBE_UNUSED static void huffman_dump_tree_symbols(const HuffmanTree* t) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, alloc_max_size(g_allocScratch), 1);
  DynString buffer     = dynstring_create_over(scratchMem);

  HuffmanCode codes[huffman_max_symbols]; // Path through the tree to reach the symbol.
  huffman_codes(t, codes);

  for (u16 i = 0; i != t->leafCount; ++i) {
    fmt_write(&buffer, "[{}] ", fmt_int(t->leafSymbols[i], .minDigits = 3));
    huffman_code_write(codes[i], &buffer);
    dynstring_append_char(&buffer, '\n');
  }

  file_write_sync(g_fileStdOut, dynstring_view(&buffer));
}

/**
 * Dump the huffman tree structure to stdout.
 */
MAYBE_UNUSED static void huffman_dump_tree_structure(const HuffmanTree* t) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, alloc_max_size(g_allocScratch), 1);
  DynString buffer     = dynstring_create_over(scratchMem);

  HuffmanLevel levels[huffman_max_levels];
  huffman_levels(t, levels);

  HuffmanNode queue[16];
  u32         queueCount = 0;

  // Enqueue the root node.
  queue[queueCount++] = (HuffmanNode){.level = 0, .index = 0};

  while (queueCount) {
    const HuffmanNode node = queue[--queueCount];
    dynstring_append_chars(&buffer, ' ', node.level * 2);

    if (huffman_is_root(node)) {
      dynstring_append(&buffer, string_lit("Root"));
    } else {
      const bool isLeft = (node.index % 2) == 0;
      fmt_write(&buffer, "{}", fmt_char(isLeft ? 'L' : 'R'));
    }

    if (huffman_is_leaf(t, node)) {
      const u16 symbol = t->leafSymbols[levels[node.level].symbolStart + node.index];
      fmt_write(&buffer, " [{}]\n", fmt_int(symbol));
    } else {
      fmt_write(&buffer, "\n");

      // Enqueue the child nodes.
      diag_assert((queueCount + 2) < array_elems(queue));
      queue[queueCount++] = huffman_child(t, node, true /* right */);
      queue[queueCount++] = huffman_child(t, node, false /* left */);
    }
  }

  file_write_sync(g_fileStdOut, dynstring_view(&buffer));
}

static void inflate_consume(InflateCtx* ctx, const usize amount) {
  ctx->input.ptr = bits_ptr_offset(ctx->input.ptr, amount);
  ctx->input.size -= amount;
}

static u32 inflate_read_unaligned(InflateCtx* ctx, const u32 bits, DeflateError* err) {
  diag_assert(bits <= 32);
  u32 res = 0;
  for (u32 i = 0; i != bits; ++i) {
    if (ctx->inputBitIndex == 8) {
      inflate_consume(ctx, 1);
      ctx->inputBitIndex = 0;
    }
    if (UNLIKELY(!ctx->input.size)) {
      *err = DeflateError_Truncated;
      return res;
    }

    // Extract one bit from the input.
    const u32 bit = (*mem_begin(ctx->input) >> ctx->inputBitIndex++) & 1;

    // Append it to the result.
    res |= bit << i;
  }
  return res;
}

static void inflate_read_align(InflateCtx* ctx) {
  if (ctx->inputBitIndex) {
    diag_assert(ctx->input.size);
    inflate_consume(ctx, 1);
    ctx->inputBitIndex = 0;
  }
}

static u16 inflate_read_u16(InflateCtx* ctx, DeflateError* err) {
  inflate_read_align(ctx); // Align to a byte boundary.
  if (UNLIKELY(ctx->input.size < sizeof(u16))) {
    *err = DeflateError_Truncated;
    return 0;
  }
  u8*       data = mem_begin(ctx->input);
  const u16 val  = (u16)data[0] | (u16)data[1] << 8;
  inflate_consume(ctx, 2);
  return val;
}

static void inflate_block_uncompressed(InflateCtx* ctx, DeflateError* err) {
  const u16 len  = inflate_read_u16(ctx, err);
  const u16 nlen = inflate_read_u16(ctx, err);
  if (UNLIKELY(*err)) {
    return;
  }
  if (UNLIKELY((u16)~len != nlen)) {
    *err = DeflateError_Malformed;
    return;
  }
  if (UNLIKELY(ctx->input.size < len)) {
    *err = DeflateError_Truncated;
    return;
  }
  dynstring_append(ctx->out, mem_slice(ctx->input, 0, len));
  inflate_consume(ctx, len);
}

static bool inflate_block(InflateCtx* ctx, DeflateError* err) {
  const bool finalBlock = inflate_read_unaligned(ctx, 1, err);
  const u32  type       = inflate_read_unaligned(ctx, 2, err);

  if (UNLIKELY(*err)) {
    return false;
  }

  switch (type) {
  case 0: /* no compression */
    inflate_block_uncompressed(ctx, err);
    break;
  case 1: /* compressed with fixed Huffman codes */
    break;
  case 2: /* compressed with dynamic Huffman codes */
    break;
  case 3: /* reserved */
    *err = DeflateError_Malformed;
    return false;
  default:
    UNREACHABLE
  }

  return !finalBlock;
}

static void deflate_init_fixed_literal_tree(HuffmanTree* tree) {
  u16 symbolLevels[huffman_max_symbols];
  u16 i = 0;
  while (i != 144)
    symbolLevels[i++] = 8;
  while (i != 256)
    symbolLevels[i++] = 9;
  while (i != 280)
    symbolLevels[i++] = 7;
  while (i != 288)
    symbolLevels[i++] = 8;
  MAYBE_UNUSED const DeflateError err = huffman_build(tree, symbolLevels, i);
  diag_assert(!err);
}

static void deflate_init_fixed_distance_tree(HuffmanTree* tree) {
  u16 symbolLevels[huffman_max_symbols];
  u16 i = 0;
  while (i != 32)
    symbolLevels[i++] = 5;
  MAYBE_UNUSED const DeflateError err = huffman_build(tree, symbolLevels, i);
  diag_assert(!err);
}

void deflate_init(void) {
  deflate_init_fixed_literal_tree(&g_fixedLiteralTree);
  deflate_init_fixed_distance_tree(&g_fixedDistanceTree);
}

String deflate_decode(const String input, DynString* out, DeflateError* err) {
  InflateCtx ctx = {
      .input = input,
      .out   = out,
  };
  *err = DeflateError_None;
  while (inflate_block(&ctx, err) && *err == DeflateError_None)
    ;
  inflate_read_align(&ctx); // Always end on a byte boundary.
  return ctx.input;
}
