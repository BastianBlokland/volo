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
#define huffman_max_levels 15
#define huffman_max_symbols 288

/**
 * Huffman code, encodes a path through the tree.
 * Bit 0 represents following the left child and bit 1 the right child.
 */
typedef struct {
  u16 bits;
  u16 length; // Number of levels into the tree.
} HuffmanCode;

typedef struct {
  u16 leafNodes, internalNodes;
  u16 symbolStart;
} HuffmanLevel;

typedef struct {
  u16 level, index;
} HuffmanNode;

/**
 * Huffman tree is a binary tree that is indexed by prefix codes (HuffmanCode) which represent
 * paths through the tree.
 *
 * Properties of the Huffman tree's as used in deflate:
 * - Symbols have consecutive values within the same tree level (eg the same code length).
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

static bool huffman_code_sample(const HuffmanCode code, const u16 level) {
  diag_assert(level < code.length);
  // Huffman codes are sampled from most- to least-significant bits.
  return (code.bits & (1 << (code.length - 1 - level))) != 0;
}

static void huffman_code_write(const HuffmanCode code, DynString* out) {
  for (u16 level = 0; level != code.length; ++level) {
    dynstring_append_char(out, huffman_code_sample(code, level) ? '1' : '0');
  }
}

static u32 huffman_max_nodes_for_level(const u16 level) {
  // Because a huffman tree is a binary tree the amount of nodes is bounded by pow(2, level + 1).
  return 1 << (level + 1);
}

/**
 * Lookup the index of the first symbol in the given level.
 */
static u16 huffman_tree_symbol_start(const HuffmanTree* tree, const u16 level) {
  diag_assert(level < huffman_max_levels);
  u16 leafCounter = 0;
  for (u16 i = 0; i != level; ++i) {
    leafCounter += tree->leafCountPerLevel[i];
  }
  return leafCounter;
}

/**
 * Check if a node is a leaf-node or an internal node.
 */
static bool huffman_node_is_leaf(const HuffmanTree* tree, const HuffmanNode node) {
  return node.index < tree->leafCountPerLevel[node.level];
}

/**
 * Retrieve the huffman code (path through the tree) for each leaf node.
 */
static void huffman_tree_codes(const HuffmanTree* tree, HuffmanCode codes[]) {
  for (u16 symbolIndex = 0, codeBits = 0, level = 0; level < huffman_max_levels; ++level) {
    codeBits <<= 1;
    for (u16 i = 0; i != tree->leafCountPerLevel[level]; ++i, ++symbolIndex) {
      codes[symbolIndex] = (HuffmanCode){.bits = codeBits++, .length = level + 1};
    }
  }
}

/**
 * Lookup the structure of each level in the tree.
 */
static void huffman_tree_levels(const HuffmanTree* tree, HuffmanLevel levels[]) {
  mem_set(mem_create(levels, sizeof(HuffmanLevel) * huffman_max_levels), 0);

  for (u16 level = huffman_max_levels; level-- != 0;) {
    levels[level].symbolStart = huffman_tree_symbol_start(tree, level);
    levels[level].leafNodes   = tree->leafCountPerLevel[level];

    const u16 totalNodes = levels[level].leafNodes + levels[level].internalNodes;
    diag_assert(totalNodes <= huffman_max_nodes_for_level(level));

    if (level) {
      levels[level - 1].internalNodes = totalNodes / 2;
    } else {
      diag_assert(totalNodes == 2);
    }
  }
}

/**
 * Lookup a symbol by its code (path through the tree).
 * Returns sentinel_u16 when the code does not point to a leaf node in the tree.
 */
MAYBE_UNUSED static u16 huffman_lookup(const HuffmanTree* tree, const HuffmanCode code) {
  u16 levelStart = 0, levelIndex = 0;
  for (u16 level = 0; level != code.length; ++level) {
    levelIndex *= 2;
    if (huffman_code_sample(code, level)) {
      ++levelIndex; // Take the right branch.
    }
    if (levelIndex < tree->leafCountPerLevel[level]) {
      diag_assert(levelStart + levelIndex < huffman_max_symbols);
      return tree->leafSymbols[levelStart + levelIndex];
    }
    levelStart += tree->leafCountPerLevel[level];
    levelIndex -= tree->leafCountPerLevel[level];
  }
  return sentinel_u16;
}

static void huffman_build(HuffmanTree* tree, const u16 symbolLevels[], const u16 symbolCount) {
  tree->leafCount = symbolCount;

  // Gather the symbol count for each level.
  mem_set(array_mem(tree->leafCountPerLevel), 0);
  for (u16 i = 0; i != symbolCount; ++i) {
    const u16 level = symbolLevels[i];
    if (!level) {
      continue; // Unused symbol.
    }
    diag_assert(level < huffman_max_levels);
    ++tree->leafCountPerLevel[level];
    diag_assert(tree->leafCountPerLevel[level] <= huffman_max_nodes_for_level(level));
  }

  // Compute the start index for each level.
  u16 levelStart[huffman_max_levels];
  for (u16 level = 0, leafCounter = 0; level != array_elems(levelStart); ++level) {
    levelStart[level] = leafCounter;
    leafCounter += tree->leafCountPerLevel[level];
  }

  // Insert the symbols into tree.
  for (u16 i = 0; i != symbolCount; ++i) {
    const u16 level = symbolLevels[i];
    if (!level) {
      continue; // Unused symbol.
    }
    tree->leafSymbols[levelStart[level]++] = i;
  }

#if huffman_validation
  {
    HuffmanCode codes[huffman_max_symbols];
    huffman_tree_codes(tree, codes);

    for (u32 i = 0; i != symbolCount; ++i) {
      diag_assert(huffman_lookup(tree, codes[i]) == tree->leafSymbols[i]);
    }
  }
#endif
}

/**
 * Dump the Huffman tree leaf nodes to stdout.
 * Includes the symbol value of the node and the code to reach it.
 */
MAYBE_UNUSED static void huffman_dump_tree_symbols(const HuffmanTree* tree) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, alloc_max_size(g_allocScratch), 1);
  DynString buffer     = dynstring_create_over(scratchMem);

  HuffmanCode codes[huffman_max_symbols]; // Path through the tree to reach the symbol.
  huffman_tree_codes(tree, codes);

  for (u16 i = 0; i != tree->leafCount; ++i) {
    fmt_write(&buffer, "[{}] ", fmt_int(tree->leafSymbols[i], .minDigits = 3));
    huffman_code_write(codes[i], &buffer);
    dynstring_append_char(&buffer, '\n');
  }

  file_write_sync(g_fileStdOut, dynstring_view(&buffer));
}

/**
 * Dump the huffman tree structure to stdout.
 */
MAYBE_UNUSED static void huffman_dump_tree_structure(const HuffmanTree* tree) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, alloc_max_size(g_allocScratch), 1);
  DynString buffer     = dynstring_create_over(scratchMem);

  HuffmanLevel levels[huffman_max_levels];
  huffman_tree_levels(tree, levels);

  HuffmanNode queue[16];
  u32         queueCount = 0;

  // The first level always contains 2 nodes.
  queue[queueCount++] = (HuffmanNode){.level = 0, .index = 1};
  queue[queueCount++] = (HuffmanNode){.level = 0, .index = 0};

  while (queueCount) {
    const HuffmanNode node = queue[--queueCount];
    dynstring_append_chars(&buffer, ' ', node.level * 2);
    const bool isLeft = (node.index % 2) == 0;
    fmt_write(&buffer, "{}", fmt_char(isLeft ? 'L' : 'R'));
    if (huffman_node_is_leaf(tree, node)) {
      const u16 symbol = tree->leafSymbols[levels[node.level].symbolStart + node.index];
      fmt_write(&buffer, " [{}]\n", fmt_int(symbol));
    } else {
      fmt_write(&buffer, "\n");
      diag_assert((queueCount + 2) < array_elems(queue));
      const u16 internalIndex = node.index - tree->leafCountPerLevel[node.level];
      queue[queueCount++] = (HuffmanNode){.level = node.level + 1, .index = internalIndex * 2 + 1};
      queue[queueCount++] = (HuffmanNode){.level = node.level + 1, .index = internalIndex * 2 + 0};
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
    symbolLevels[i++] = 7;
  while (i != 256)
    symbolLevels[i++] = 8;
  while (i != 280)
    symbolLevels[i++] = 6;
  while (i != 288)
    symbolLevels[i++] = 7;
  huffman_build(tree, symbolLevels, i);
}

static void deflate_init_fixed_distance_tree(HuffmanTree* tree) {
  u16 symbolLevels[huffman_max_symbols];
  u16 i = 0;
  while (i != 32)
    symbolLevels[i++] = 4;
  huffman_build(tree, symbolLevels, i);
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
