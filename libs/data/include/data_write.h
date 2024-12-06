#pragma once
#include "core.h"
#include "data.h"

typedef struct {
  u8   numberMaxDecDigits;
  f64  numberExpThresholdPos;
  f64  numberExpThresholdNeg;
  bool compact;
} DataWriteJsonOpts;

#define data_write_json_opts(...)                                                                  \
  ((DataWriteJsonOpts){                                                                            \
      .numberMaxDecDigits    = 10,                                                                 \
      .numberExpThresholdPos = 1e20,                                                               \
      .numberExpThresholdNeg = 1e-5,                                                               \
      __VA_ARGS__})

/**
 * Write a data value as a json string.
 *
 * Pre-condition: data memory contains an initialized value compatable with the given DataMeta.
 * Pre-condition: data memory does not contain any cycles.
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_write_json(const DataReg*, DynString*, DataMeta, Mem data, const DataWriteJsonOpts*);

/**
 * Write a data value as a binary blob.
 *
 * Pre-condition: data memory contains an initialized value compatable with the given DataMeta.
 * Pre-condition: data memory does not contain any cycles.
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_write_bin(const DataReg*, DynString*, DataMeta, Mem data);
