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

#define huffman_max_code_length 15
#define huffman_max_symbols 288

typedef struct {
  u16 symbolCount;
  u16 counts[huffman_max_code_length + 1]; // Number of symbols with the same code length.
  u16 symbols[huffman_max_symbols];
} HuffmanTree;

typedef struct {
  String     input;
  u32        inputBitIndex;
  DynString* out;
} InflateCtx;

static HuffmanTree g_fixedLiteralTree;
static HuffmanTree g_fixedDistanceTree;

static void huffman_build(HuffmanTree* tree, const u16 symbolCodeLengths[], const u16 symbolCount) {
  tree->symbolCount = symbolCount;

  // Gather the symbol count for each code-length.
  mem_set(array_mem(tree->counts), 0);
  for (u16 i = 0; i != symbolCount; ++i) {
    const u16 codeLength = symbolCodeLengths[i];
    if (!codeLength) {
      continue; // Unused symbol.
    }
    diag_assert(symbolCodeLengths[i] <= huffman_max_code_length);
    ++tree->counts[symbolCodeLengths[i]];
  }

  // Compute the start index for each of the code lengths.
  u16 codeLengthStart[huffman_max_code_length + 1];
  for (u16 i = 0, nodeCounter = 0; i != array_elems(codeLengthStart); ++i) {
    codeLengthStart[i] = nodeCounter;
    nodeCounter += tree->counts[i];
  }

  // Insert the symbols into tree sorted by code.
  for (u16 i = 0; i != symbolCount; ++i) {
    const u16 codeLength = symbolCodeLengths[i];
    if (!codeLength) {
      continue; // Unused symbol.
    }
    tree->symbols[codeLengthStart[codeLength]++] = i;
  }
}

/**
 * Retrieve the huffman code (paths through the tree) and its length for each leaf node.
 */
static void huffman_tree_codes(const HuffmanTree* tree, u16 codes[], u16 codeLengths[]) {
  for (u16 symbolIndex = 0, symbolCode = 0, bits = 1; bits <= huffman_max_code_length; ++bits) {
    symbolCode <<= 1;
    for (u16 i = 0; i != tree->counts[bits]; ++i, ++symbolIndex) {
      codes[symbolIndex]       = symbolCode++;
      codeLengths[symbolIndex] = bits;
    }
  }
}

static void huffman_write_code(DynString* out, const u16 code, const u16 codeLength) {
  // Iterate backwards as huffman codes are usually written out most- to least-significant bits.
  for (u16 i = codeLength; i-- != 0;) {
    dynstring_append_char(out, code & (1 << i) ? '1' : '0');
  }
}

/**
 * Dump the Huffman tree leaf nodes to stdout.
 * Includes the symbol value of the node and the code to reach it.
 */
MAYBE_UNUSED static void huffman_dump_tree_leaves(const HuffmanTree* tree) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, alloc_max_size(g_allocScratch), 1);
  DynString buffer     = dynstring_create_over(scratchMem);

  u16 codes[huffman_max_symbols];       // Path through the tree to reach a symbol.
  u16 codeLengths[huffman_max_symbols]; // Length in bits of the code for a symbol.
  huffman_tree_codes(tree, codes, codeLengths);

  for (u16 i = 0; i != tree->symbolCount; ++i) {
    fmt_write(&buffer, "[{}] ", fmt_int(tree->symbols[i], .minDigits = 3));
    huffman_write_code(&buffer, codes[i], codeLengths[i]);
    dynstring_append_char(&buffer, '\n');
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
  u16 symbolCodeLengths[huffman_max_symbols];
  u16 i = 0;
  while (i != 144)
    symbolCodeLengths[i++] = 8;
  while (i != 256)
    symbolCodeLengths[i++] = 9;
  while (i != 280)
    symbolCodeLengths[i++] = 7;
  while (i != 288)
    symbolCodeLengths[i++] = 8;
  huffman_build(tree, symbolCodeLengths, i);
}

static void deflate_init_fixed_distance_tree(HuffmanTree* tree) {
  u16 symbolCodeLengths[huffman_max_symbols];
  u16 i = 0;
  while (i != 32)
    symbolCodeLengths[i++] = 5;
  huffman_build(tree, symbolCodeLengths, i);
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
