#include "core.h"
#include "core_array.h"
#include "core_diag.h"

#include "input_internal.h"

static const Unicode g_keyChars[GapKey_Count] = {
    [GapKey_Plus] = '+',        [GapKey_Minus] = '-',
    [GapKey_BracketLeft] = '[', [GapKey_BracketRight] = ']',
    [GapKey_A] = 'A',           [GapKey_B] = 'B',
    [GapKey_C] = 'C',           [GapKey_D] = 'D',
    [GapKey_E] = 'E',           [GapKey_F] = 'F',
    [GapKey_G] = 'G',           [GapKey_H] = 'H',
    [GapKey_I] = 'I',           [GapKey_J] = 'J',
    [GapKey_K] = 'K',           [GapKey_L] = 'L',
    [GapKey_M] = 'M',           [GapKey_N] = 'N',
    [GapKey_O] = 'O',           [GapKey_P] = 'P',
    [GapKey_Q] = 'Q',           [GapKey_R] = 'R',
    [GapKey_S] = 'S',           [GapKey_T] = 'T',
    [GapKey_U] = 'U',           [GapKey_V] = 'V',
    [GapKey_W] = 'W',           [GapKey_X] = 'X',
    [GapKey_Y] = 'Y',           [GapKey_Z] = 'Z',
    [GapKey_Alpha0] = '0',      [GapKey_Alpha1] = '1',
    [GapKey_Alpha2] = '2',      [GapKey_Alpha3] = '3',
    [GapKey_Alpha4] = '4',      [GapKey_Alpha5] = '5',
    [GapKey_Alpha6] = '6',      [GapKey_Alpha7] = '7',
    [GapKey_Alpha8] = '8',      [GapKey_Alpha9] = '9',
};

Unicode gap_key_char(const GapKey key) {
  if (key == GapKey_None) {
    return Unicode_Invalid;
  }
  diag_assert(key >= 0 && key < GapKey_Count);
  return g_keyChars[key];
}

static const String g_keyStrs[] = {
    string_static("mouse-left"),
    string_static("mouse-right"),
    string_static("mouse-middle"),
    string_static("mouse-extra1"),
    string_static("mouse-extra2"),
    string_static("mouse-extra3"),
    string_static("shift"),
    string_static("control"),
    string_static("alt"),
    string_static("backspace"),
    string_static("delete"),
    string_static("tab"),
    string_static("tilde"),
    string_static("return"),
    string_static("escape"),
    string_static("space"),
    string_static("plus"),
    string_static("minus"),
    string_static("home"),
    string_static("end"),
    string_static("page-up"),
    string_static("page-down"),
    string_static("arrow-up"),
    string_static("arrow-down"),
    string_static("arrow-right"),
    string_static("arrow-left"),
    string_static("bracket-left"),
    string_static("bracket-right"),
    string_static("a"),
    string_static("b"),
    string_static("c"),
    string_static("d"),
    string_static("e"),
    string_static("f"),
    string_static("g"),
    string_static("h"),
    string_static("i"),
    string_static("j"),
    string_static("k"),
    string_static("l"),
    string_static("m"),
    string_static("n"),
    string_static("o"),
    string_static("p"),
    string_static("q"),
    string_static("r"),
    string_static("s"),
    string_static("t"),
    string_static("u"),
    string_static("v"),
    string_static("w"),
    string_static("x"),
    string_static("y"),
    string_static("z"),
    string_static("alpha-0"),
    string_static("alpha-1"),
    string_static("alpha-2"),
    string_static("alpha-3"),
    string_static("alpha-4"),
    string_static("alpha-5"),
    string_static("alpha-6"),
    string_static("alpha-7"),
    string_static("alpha-8"),
    string_static("alpha-9"),
    string_static("f1"),
    string_static("f2"),
    string_static("f3"),
    string_static("f4"),
    string_static("f5"),
    string_static("f6"),
    string_static("f7"),
    string_static("f8"),
    string_static("f9"),
    string_static("f10"),
    string_static("f11"),
    string_static("f12"),
};

ASSERT(array_elems(g_keyStrs) == GapKey_Count, "Incorrect number of GapKey strings");

String gap_key_str(const GapKey key) {
  if (key == GapKey_None) {
    return string_empty;
  }
  diag_assert(key >= 0 && key < GapKey_Count);
  return g_keyStrs[key];
}

static const String g_paramStrs[] = {
    string_static("window-size"),
    string_static("window-size-pre-fullscreen"),
    string_static("cursor-pos"),
    string_static("cursor-pos-pre-lock"),
    string_static("cursor-delta"),
    string_static("scroll-delta"),
};

ASSERT(array_elems(g_paramStrs) == GapParam_Count, "Incorrect number of GapParam strings");

String gap_param_str(const GapParam param) {
  diag_assert(param >= 0 && param < GapParam_Count);
  return g_paramStrs[param];
}

void gap_keyset_clear(GapKeySet* set) { mem_set(array_mem(set->data), 0); }

bool gap_keyset_test(const GapKeySet* set, const GapKey key) {
  diag_assert(key >= 0 && key < GapKey_Count);
  return bitset_test(bitset_from_array(set->data), key);
}

void gap_keyset_set(GapKeySet* set, const GapKey key) {
  diag_assert(key >= 0 && key < GapKey_Count);
  bitset_set(bitset_from_array(set->data), key);
}

void gap_keyset_unset(GapKeySet* set, const GapKey key) {
  diag_assert(key >= 0 && key < GapKey_Count);
  bitset_clear(bitset_from_array(set->data), key);
}
