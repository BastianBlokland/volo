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
#define huffman_dump_trees 0
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
static bool huffman_code_sample(const HuffmanCode code, const u32 level) {
  diag_assert(level <= code.level);
  // Huffman codes are sampled from most- to least-significant bits.
  return (code.bits & (1 << (code.level - 1 - level))) != 0;
}

static void huffman_code_write(const HuffmanCode code, DynString* out) {
  for (u32 level = 0; level != code.level; ++level) {
    dynstring_append_char(out, huffman_code_sample(code, level) ? '1' : '0');
  }
}

static u32 huffman_max_nodes_for_level(const u32 level) {
  // Because a huffman tree is a binary tree the amount of nodes is bounded by pow(2, level).
  return 1 << level;
}

/**
 * Lookup the index of the first symbol in the given level.
 */
static u16 huffman_symbol_start(const HuffmanTree* t, const u32 level) {
  diag_assert(level < huffman_max_levels);
  u16 leafCounter = 0;
  for (u32 i = 0; i != level; ++i) {
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
  if (!t->leafCount) {
    return sentinel_u16;
  }
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

/**
 * Build a complete (no unconnected nodes) Huffman tree.
 * Symbol values are based on their index and the position in the tree is based on the given level.
 *
 * Example input symbols (NOTE: value is inferred by the order, level is provided explicitly):
 *   [A] 1
 *   [B] 0 (level zero indicates this symbol should be skipped)
 *   [C] 3
 *   [D] 2
 *   [E] 3
 *
 * Example resulting Huffman tree:
 *     .
 *    / \
 *   A   .
 *      / \
 *     D   .
 *        / \
 *       C   E
 *
 * Example resulting Huffman codes (paths through the tree):
 *   [A] 0
 *   [C] 110
 *   [D] 10
 *   [E] 111
 */
static DeflateError huffman_build(HuffmanTree* t, const u8 symbolLevels[], const u32 symbolCount) {
  if (UNLIKELY(symbolCount > huffman_max_symbols)) {
    return DeflateError_Malformed;
  }

  // Gather the symbol count for each level.
  mem_set(array_mem(t->leafCountPerLevel), 0);
  for (u32 i = 0; i != symbolCount; ++i) {
    const u8 level = symbolLevels[i];
    if (!level) {
      continue; // The root node is always an internal node and cannot contain a symbol.
    }
    if (UNLIKELY(level >= huffman_max_levels)) {
      return DeflateError_Malformed;
    }
    ++t->leafCountPerLevel[level];
  }

  // Compute the start symbol index for each level.
  u16 symbolStart[huffman_max_levels];
  u16 availableInternalNodes[huffman_max_levels];

  t->leafCount = 0;
  for (u16 level = 0; level != huffman_max_levels; ++level) {
    symbolStart[level] = t->leafCount;

    const u16 maxNodes  = level ? (availableInternalNodes[level - 1] * 2) : 1;
    const u16 leafNodes = t->leafCountPerLevel[level];
    if (UNLIKELY(leafNodes > maxNodes)) {
      return DeflateError_Malformed; // Invalid tree.
    }
    availableInternalNodes[level] = maxNodes - leafNodes;
    t->leafCount += leafNodes;
  }

  // Insert the symbols for the leaf nodes into tree.
  for (u32 i = 0; i != symbolCount; ++i) {
    const u8 level = symbolLevels[i];
    if (!level) {
      continue; // The root node is always an internal node and cannot contain a symbol.
    }
    t->leafSymbols[symbolStart[level]++] = i;
  }

  if (t->leafCount == 1) {
    /**
     * Special case for tree's with only 1 leaf node. Because the root node is always an internal
     * node the single leaf will be inserted at level 1 with an unused sibling.
     */
    t->leafCountPerLevel[1] = t->leafCount = 2;
    t->leafSymbols[1]                      = 0; // Unused sibling.
  } else {
    // Validate the tree is complete.
    if (UNLIKELY(availableInternalNodes[huffman_max_levels - 1])) {
      return DeflateError_Malformed; // Incomplete tree (has unconnected nodes).
    }
  }

#if huffman_validation
  {
    HuffmanCode codes[huffman_max_symbols];
    huffman_codes(t, codes);

    for (u32 i = 0; i != t->leafCount; ++i) {
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

  for (u32 i = 0; i != t->leafCount; ++i) {
    fmt_write(&buffer, "[{}] ", fmt_int(t->leafSymbols[i], .minDigits = 3));
    huffman_code_write(codes[i], &buffer);
    dynstring_append_char(&buffer, '\n');
  }

  file_write_sync(g_fileStdOut, dynstring_view(&buffer));
}

/**
 * Dump the huffman tree structure to stdout.
 */
MAYBE_UNUSED static void huffman_dump_tree_structure(const HuffmanTree* t, const String name) {
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
      fmt_write(&buffer, "<{}>", fmt_text(name));
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

static u16 inflate_read_symbol(InflateCtx* ctx, const HuffmanTree* t, DeflateError* err) {
  if (!t->leafCount) {
    *err = DeflateError_Malformed;
    return sentinel_u16;
  }
  HuffmanNode node        = {.level = 0, .index = 0}; // Root node.
  u16         symbolStart = 0;
  while (*err == DeflateError_None) {
    node = huffman_child(t, node, inflate_read_unaligned(ctx, 1, err));
    if (huffman_is_leaf(t, node)) {
      return t->leafSymbols[symbolStart + node.index];
    }
    if (UNLIKELY(node.level == (huffman_max_levels - 1))) {
      *err = DeflateError_Malformed;
      break;
    }
    symbolStart += t->leafCountPerLevel[node.level];
  }
  diag_assert(*err);
  return sentinel_u16;
}

static u32 inflate_read_run_length(InflateCtx* ctx, const u16 symbol, DeflateError* err) {
  diag_assert(symbol > 256 && symbol < huffman_max_symbols);
  /**
   * Run length is based on the input symbol plus additional bits.
   * Source of the tables can be found in the RFC.
   */
  static const u16 g_lengthBase[] = {
      3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23,  27,
      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
  };
  static const u16 g_lengthBits[] = {
      0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
  };
  const u32 tableIndex = symbol - 257; // 0 - 28.
  return g_lengthBase[tableIndex] + inflate_read_unaligned(ctx, g_lengthBits[tableIndex], err);
}

static u32 inflate_read_run_distance(InflateCtx* ctx, const HuffmanTree* t, DeflateError* err) {
  const u16 symbol = inflate_read_symbol(ctx, t, err);
  if (UNLIKELY(symbol > 29)) {
    *err = DeflateError_Malformed;
    return sentinel_u16;
  }
  /**
   * Run distance is based on an input symbol plus additional bits.
   * Source of the tables can be found in the RFC.
   */
  static const u16 g_distBase[] = {
      1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
      193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
  };
  static const u16 g_distBits[] = {
      0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
  };
  return g_distBase[symbol] + inflate_read_unaligned(ctx, g_distBits[symbol], err);
}

/**
 * Read two dynamic huffman tree's from the input:
 * - Literal (or run length) tree.
 * - Distance tree.
 * For the meaning of these tree's check the usage in 'inflate_block_compressed()'.
 */
static void inflate_read_huffman_trees(
    InflateCtx* ctx, HuffmanTree* outLiteral, HuffmanTree* outDistance, DeflateError* err) {
  /**
   * The Huffman trees are defined by a collection of symbol tree level's (see 'huffman_build()').
   * These levels are themselves encoded with a third dynamic Huffman tree we call the level-tree.
   */
  const u32 numLiteralSymbols  = inflate_read_unaligned(ctx, 5, err) + 257; // hlit + 257.
  const u32 numDistanceSymbols = inflate_read_unaligned(ctx, 5, err) + 1;   // hdist + 1.
  const u32 numLevelSymbols    = inflate_read_unaligned(ctx, 4, err) + 4;   // hclen + 4.
  if (UNLIKELY(*err)) {
    return;
  }
  if (UNLIKELY(numLiteralSymbols > 286 || numDistanceSymbols > 32 || numLevelSymbols > 19)) {
    *err = DeflateError_Malformed;
    return;
  }

  /**
   * Read a Huffman tree for the tree levels (also known as the 'code length' tree in the spec).
   * The source of the index table can be found in the RFC.
   */
  static const u8 g_levelSymbolIndex[] = {
      16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
  };
  u8 levelSymbolLevels[19];
  for (u32 i = 0; i != array_elems(levelSymbolLevels); ++i) {
    u32 val = 0;
    if (i < numLevelSymbols) {
      val = inflate_read_unaligned(ctx, 3, err);
    }
    levelSymbolLevels[g_levelSymbolIndex[i]] = val;
  }
  HuffmanTree levelTree;
  *err = huffman_build(&levelTree, levelSymbolLevels, array_elems(levelSymbolLevels));
  if (UNLIKELY(*err)) {
    return;
  }
#if huffman_dump_trees
  huffman_dump_tree_structure(&levelTree, string_lit("Level Tree"));
#endif
  if (UNLIKELY(!levelTree.leafCount)) {
    *err = DeflateError_Malformed;
    return;
  }

  /**
   * Read the symbol levels (depth in the tree) for both the literal and distance trees.
   */
  u8        symbolLevels[286 /* literal symbols */ + 32 /* distance symbols */];
  const u32 numSymbolLevels = numLiteralSymbols + numDistanceSymbols;
  for (u32 i = 0; i != numSymbolLevels;) {
    const u16 symbol = inflate_read_symbol(ctx, &levelTree, err);
    if (UNLIKELY(*err)) {
      break;
    }
    if (symbol < 16) /* 0 - 15 are literal levels */ {
      symbolLevels[i++] = (u8)symbol;
      continue;
    }
    u8 runValue, runLength;
    switch (symbol) {
    case 16: // Copy the previous level 3 - 6 times.
      if (UNLIKELY(i == 0)) {
        *err = DeflateError_Malformed;
        return; // Previous level is missing.
      }
      runValue  = symbolLevels[i - 1];
      runLength = (u8)inflate_read_unaligned(ctx, 2, err) + 3;
      break;
    case 17: // Repeat a level of 0 for 3 - 10 times.
      runValue  = 0;
      runLength = (u8)inflate_read_unaligned(ctx, 3, err) + 3;
      break;
    case 18: // Repeat a level of 0 for 11 - 138 times.
      runValue  = 0;
      runLength = (u8)inflate_read_unaligned(ctx, 7, err) + 11;
      break;
    default:
      *err = DeflateError_Malformed;
      return;
    }
    if (UNLIKELY(*err)) {
      return; // Input truncated.
    }
    if (UNLIKELY(i + runLength > numSymbolLevels)) {
      *err = DeflateError_Malformed;
      return;
    }
    for (u32 runItr = 0; runItr != runLength; ++runItr) {
      symbolLevels[i++] = runValue;
    }
  }

  /**
   * Build the output trees.
   */
  *err = huffman_build(outLiteral, symbolLevels, numLiteralSymbols);
  if (UNLIKELY(*err)) {
    return;
  }
  *err = huffman_build(outDistance, symbolLevels + numLiteralSymbols, numDistanceSymbols);
  if (UNLIKELY(*err)) {
    return;
  }

#if huffman_dump_trees
  huffman_dump_tree_structure(outLiteral, string_lit("Literal Tree"));
  huffman_dump_tree_structure(outDistance, string_lit("Distance Tree"));
#endif
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

static void inflate_block_compressed(
    InflateCtx*        ctx,
    const HuffmanTree* literalTree,
    const HuffmanTree* distanceTree,
    DeflateError*      err) {
  for (;;) {
    const u16 symbol = inflate_read_symbol(ctx, literalTree, err);
    if (UNLIKELY(*err)) {
      break;
    }
    if (symbol == 256) {
      break; // End of block.
    }
    if (symbol < 256) {
      // Output literal byte.
      dynstring_append_char(ctx->out, (u8)symbol);
      continue;
    }
    /**
     * Run-length data; copy a section (indicated by a length and a backwards distance) from output.
     * NOTE: The spec limits the backwards distance to 32k but because we keep the whole output in
     * memory we have no such limit in practice.
     */
    const u32 runLength   = inflate_read_run_length(ctx, symbol, err);
    const u32 runDistance = inflate_read_run_distance(ctx, distanceTree, err);
    if (UNLIKELY(*err)) {
      break;
    }
    if (UNLIKELY(runDistance > ctx->out->size)) {
      *err = DeflateError_Malformed;
      break;
    }
    // Copy section from output.
    for (u32 i = 0; i != runLength; ++i) {
      const String history = dynstring_view(ctx->out);
      dynstring_append_char(ctx->out, *(mem_end(history) - runDistance));
    }
  }
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
    inflate_block_compressed(ctx, &g_fixedLiteralTree, &g_fixedDistanceTree, err);
    break;
  case 2: { /* compressed with dynamic Huffman codes */
    HuffmanTree literalTree, distanceTree;
    inflate_read_huffman_trees(ctx, &literalTree, &distanceTree, err);
    if (LIKELY(*err == DeflateError_None)) {
      inflate_block_compressed(ctx, &literalTree, &distanceTree, err);
    }
  } break;
  case 3: /* reserved */
    *err = DeflateError_Malformed;
    return false;
  default:
    UNREACHABLE
  }

  return !finalBlock;
}

static void deflate_init_fixed_literal_tree(HuffmanTree* tree) {
  u8  symbolLevels[huffman_max_symbols];
  u32 i = 0;
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
  u8  symbolLevels[huffman_max_symbols];
  u32 i = 0;
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
