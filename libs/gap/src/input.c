#include "core/array.h"
#include "core/diag.h"
#include "core/forward.h"

#include "input.h"
#include "platform.h"

static const String g_keyStrs[] = {
    [GapKey_MouseLeft]    = string_static("mouse-left"),
    [GapKey_MouseRight]   = string_static("mouse-right"),
    [GapKey_MouseMiddle]  = string_static("mouse-middle"),
    [GapKey_MouseExtra1]  = string_static("mouse-extra1"),
    [GapKey_MouseExtra2]  = string_static("mouse-extra2"),
    [GapKey_MouseExtra3]  = string_static("mouse-extra3"),
    [GapKey_Shift]        = string_static("shift"),
    [GapKey_Control]      = string_static("control"),
    [GapKey_Alt]          = string_static("alt"),
    [GapKey_Backspace]    = string_static("backspace"),
    [GapKey_Delete]       = string_static("delete"),
    [GapKey_Tab]          = string_static("tab"),
    [GapKey_Tilde]        = string_static("tilde"),
    [GapKey_Return]       = string_static("return"),
    [GapKey_Escape]       = string_static("escape"),
    [GapKey_Space]        = string_static("space"),
    [GapKey_Plus]         = string_static("plus"),
    [GapKey_Minus]        = string_static("minus"),
    [GapKey_Home]         = string_static("home"),
    [GapKey_End]          = string_static("end"),
    [GapKey_PageUp]       = string_static("page-up"),
    [GapKey_PageDown]     = string_static("page-down"),
    [GapKey_ArrowUp]      = string_static("arrow-up"),
    [GapKey_ArrowDown]    = string_static("arrow-down"),
    [GapKey_ArrowRight]   = string_static("arrow-right"),
    [GapKey_ArrowLeft]    = string_static("arrow-left"),
    [GapKey_BracketLeft]  = string_static("bracket-left"),
    [GapKey_BracketRight] = string_static("bracket-right"),
    [GapKey_A]            = string_static("a"),
    [GapKey_B]            = string_static("b"),
    [GapKey_C]            = string_static("c"),
    [GapKey_D]            = string_static("d"),
    [GapKey_E]            = string_static("e"),
    [GapKey_F]            = string_static("f"),
    [GapKey_G]            = string_static("g"),
    [GapKey_H]            = string_static("h"),
    [GapKey_I]            = string_static("i"),
    [GapKey_J]            = string_static("j"),
    [GapKey_K]            = string_static("k"),
    [GapKey_L]            = string_static("l"),
    [GapKey_M]            = string_static("m"),
    [GapKey_N]            = string_static("n"),
    [GapKey_O]            = string_static("o"),
    [GapKey_P]            = string_static("p"),
    [GapKey_Q]            = string_static("q"),
    [GapKey_R]            = string_static("r"),
    [GapKey_S]            = string_static("s"),
    [GapKey_T]            = string_static("t"),
    [GapKey_U]            = string_static("u"),
    [GapKey_V]            = string_static("v"),
    [GapKey_W]            = string_static("w"),
    [GapKey_X]            = string_static("x"),
    [GapKey_Y]            = string_static("y"),
    [GapKey_Z]            = string_static("z"),
    [GapKey_Alpha0]       = string_static("alpha-0"),
    [GapKey_Alpha1]       = string_static("alpha-1"),
    [GapKey_Alpha2]       = string_static("alpha-2"),
    [GapKey_Alpha3]       = string_static("alpha-3"),
    [GapKey_Alpha4]       = string_static("alpha-4"),
    [GapKey_Alpha5]       = string_static("alpha-5"),
    [GapKey_Alpha6]       = string_static("alpha-6"),
    [GapKey_Alpha7]       = string_static("alpha-7"),
    [GapKey_Alpha8]       = string_static("alpha-8"),
    [GapKey_Alpha9]       = string_static("alpha-9"),
    [GapKey_F1]           = string_static("f1"),
    [GapKey_F2]           = string_static("f2"),
    [GapKey_F3]           = string_static("f3"),
    [GapKey_F4]           = string_static("f4"),
    [GapKey_F5]           = string_static("f5"),
    [GapKey_F6]           = string_static("f6"),
    [GapKey_F7]           = string_static("f7"),
    [GapKey_F8]           = string_static("f8"),
    [GapKey_F9]           = string_static("f9"),
    [GapKey_F10]          = string_static("f10"),
    [GapKey_F11]          = string_static("f11"),
    [GapKey_F12]          = string_static("f12"),
};

ASSERT(array_elems(g_keyStrs) == GapKey_Count, "Incorrect number of GapKey strings");

String gap_key_str(const GapKey key) {
  if (key == GapKey_None) {
    return string_empty;
  }
  diag_assert(key >= 0 && key < GapKey_Count);
  return g_keyStrs[key];
}

bool gap_key_label(const GapPlatformComp* plat, const GapKey key, DynString* out) {
  return gap_pal_key_label(plat->pal, key, out);
}

static const String g_paramStrs[] = {
    string_static("window-size"),
    string_static("window-size-requested"),
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
