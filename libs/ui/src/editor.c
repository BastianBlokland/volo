#include "core_array.h"
#include "core_ascii.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_sort.h"
#include "core_time.h"
#include "core_unicode.h"
#include "core_utf8.h"
#include "gap_input.h"

#include "editor_internal.h"
#include "escape_internal.h"

#define ui_editor_max_visual_slices 1

static const String g_editorCursorEsc = string_static(uni_esc "cFF");

typedef enum {
  UiEditorFlags_Active      = 1 << 0,
  UiEditorFlags_FirstUpdate = 1 << 1,
  UiEditorFlags_Dirty       = 1 << 2,
} UiEditorFlags;

typedef enum {
  UiEditorStride_Codepoint,
  UiEditorStride_Word,
} UiEditorStride;

typedef enum {
  UiEditorSource_InitialText,
  UiEditorSource_UserTyped,
} UiEditorSource;

/**
 * Visual-slices are used to track exta elements in the visual text (for example the cursor).
 */
typedef struct {
  String text;
  usize  index; // Index into the actual text.
} UiEditorVisualSlice;

struct sUiEditor {
  Allocator*    alloc;
  UiEditorFlags flags;
  UiId          textElement;
  DynString     text;
  usize         cursor;
  TimeSteady    lastInteractTime;

  DynString           visualText;
  UiEditorVisualSlice visualSlices[ui_editor_max_visual_slices]; // Sorted by index.
};

static i8 ui_visual_slice_compare(const void* a, const void* b) {
  return compare_usize(
      field_ptr(a, UiEditorVisualSlice, index), field_ptr(b, UiEditorVisualSlice, index));
}

static bool editor_cp_is_valid(const Unicode cp, const UiEditorSource source) {
  /**
   * Source specific rules.
   */
  switch (source) {
  case UiEditorSource_InitialText:
    if (cp == Unicode_HorizontalTab) {
      return true; // Tab characters are supported in text but when typing are handled separately.
    }
    break;
  case UiEditorSource_UserTyped:
    break;
  }
  /**
   * Generic rules.
   */
  if (unicode_is_ascii(cp) && ascii_is_control(cp)) {
    return false; // Control characters like delete / backspace are handled separately.
  }
  if (unicode_is_ascii(cp) && ascii_is_newline(cp)) {
    return false; // Multi line editing is not supported at this time.
  }
  if (cp == Unicode_ZeroWidthSpace) {
    return false; // Invisible characters (which also no not advance the cursor) are not supported.
  }
  return true;
}

static bool editor_cp_is_seperator(const Unicode cp) {
  switch (cp) {
  case Unicode_Space:
  case Unicode_ZeroWidthSpace:
  case Unicode_HorizontalTab:
    return true;
  default:
    return false;
  }
}

static Unicode editor_cp_at(UiEditor* editor, const usize index) {
  diag_assert(index < editor->text.size);

  const String total = dynstring_view(&editor->text);
  Unicode      cp;
  utf8_cp_read(string_consume(total, index), &cp);
  return cp;
}

static usize editor_next_index(UiEditor* editor, const usize index) {
  String str = dynstring_view(&editor->text);
  for (usize i = index + 1; i < str.size; ++i) {
    if (!utf8_contchar(*string_at(str, i))) {
      return i;
    }
  }
  return sentinel_usize;
}

static usize editor_prev_index(UiEditor* editor, const usize index) {
  String str = dynstring_view(&editor->text);
  for (usize i = index; i-- > 0;) {
    if (!utf8_contchar(*string_at(str, i))) {
      return i;
    }
  }
  return sentinel_usize;
}

static usize editor_next_word_start_index(UiEditor* editor, usize index) {
  bool startingWordEnded = false;
  while (true) {
    const usize nextIndex = editor_next_index(editor, index);
    if (sentinel_check(nextIndex)) {
      return editor->text.size; // Return the end index when no more characters are found.
    }
    const Unicode nextCp      = editor_cp_at(editor, nextIndex);
    const bool    isSeperator = editor_cp_is_seperator(nextCp);
    startingWordEnded |= isSeperator;
    if (!isSeperator && startingWordEnded) {
      return nextIndex;
    }
    index = nextIndex;
  }
}

static usize editor_prev_word_start_index(UiEditor* editor, usize index) {
  bool foundStartingWord = false;
  while (true) {
    const usize prevIndex = editor_prev_index(editor, index);
    if (sentinel_check(prevIndex)) {
      return index;
    }
    if (editor_cp_is_seperator(editor_cp_at(editor, prevIndex))) {
      if (foundStartingWord) {
        return index;
      }
    } else {
      foundStartingWord = true;
    }
    index = prevIndex;
  }
}

static void editor_insert_cp(UiEditor* editor, const Unicode cp) {
  DynString buffer = dynstring_create_over(mem_stack(4));
  utf8_cp_write(&buffer, cp);
  dynstring_insert(&editor->text, dynstring_view(&buffer), editor->cursor);
  editor->cursor += buffer.size;
  editor->flags |= UiEditorFlags_Dirty;
}

static void editor_insert_text(UiEditor* editor, String text, const UiEditorSource source) {
  while (!string_is_empty(text)) {
    Unicode cp;
    text = utf8_cp_read(text, &cp);
    switch (cp) {
    case Unicode_Escape:
    case Unicode_Bell:
      // Skip over escape sequences, editing text with escape sequences is not supported atm.
      text = ui_escape_read(text, null);
      break;
    default:
      if (editor_cp_is_valid(cp, source)) {
        editor_insert_cp(editor, cp);
      }
      break;
    }
  }
}

static void editor_erase_prev(UiEditor* editor, const UiEditorStride stride) {
  usize eraseFrom;
  switch (stride) {
  case UiEditorStride_Codepoint:
    eraseFrom = editor_prev_index(editor, editor->cursor);
    break;
  case UiEditorStride_Word:
    eraseFrom = editor_prev_word_start_index(editor, editor->cursor);
    break;
  }
  if (!sentinel_check(eraseFrom)) {
    const usize bytesToErase = editor->cursor - eraseFrom;
    dynstring_erase_chars(&editor->text, eraseFrom, bytesToErase);
    editor->cursor -= bytesToErase;
    editor->flags |= UiEditorFlags_Dirty;
  }
}

static void editor_erase_current(UiEditor* editor, const UiEditorStride stride) {
  usize eraseTo;
  switch (stride) {
  case UiEditorStride_Codepoint:
    eraseTo = editor_next_index(editor, editor->cursor);
    break;
  case UiEditorStride_Word:
    eraseTo = editor_next_word_start_index(editor, editor->cursor);
    break;
  }
  if (sentinel_check(eraseTo)) {
    eraseTo = editor->text.size; // No next found, just erase until the end.
  }
  const usize bytesToErase = eraseTo - editor->cursor;
  dynstring_erase_chars(&editor->text, editor->cursor, bytesToErase);
  editor->flags |= UiEditorFlags_Dirty;
}

static bool editor_cursor_valid_index(UiEditor* editor, const usize index) {
  if (index > editor->text.size) {
    return false; // Out of bounds.
  }
  if (index == editor->text.size) {
    return true; // At the end of the text.
  }
  // Validate that the index is the start of a utf8 codepoint.
  const String total = dynstring_view(&editor->text);
  return !utf8_contchar(*string_at(total, index));
}

static void editor_cursor_to_start(UiEditor* editor) {
  editor->cursor = 0;
  editor->flags |= UiEditorFlags_Dirty;
}

static void editor_cursor_to_end(UiEditor* editor) {
  editor->cursor = editor->text.size;
  editor->flags |= UiEditorFlags_Dirty;
}

static void editor_cursor_next(UiEditor* editor, const UiEditorStride stride) {
  usize next;
  switch (stride) {
  case UiEditorStride_Codepoint:
    next = editor_next_index(editor, editor->cursor);
    break;
  case UiEditorStride_Word:
    next = editor_next_word_start_index(editor, editor->cursor);
    break;
  }
  editor->cursor = sentinel_check(next) ? editor->text.size : next;
  editor->flags |= UiEditorFlags_Dirty;
}

static void editor_cursor_prev(UiEditor* editor, const UiEditorStride stride) {
  usize prev;
  switch (stride) {
  case UiEditorStride_Codepoint:
    prev = editor_prev_index(editor, editor->cursor);
    break;
  case UiEditorStride_Word:
    prev = editor_prev_word_start_index(editor, editor->cursor);
    break;
  }
  editor->cursor = sentinel_check(prev) ? 0 : prev;
  editor->flags |= UiEditorFlags_Dirty;
}

static UiEditorStride editor_stride_from_key_modifiers(const GapWindowComp* win) {
  if (gap_window_key_down(win, GapKey_Control)) {
    return UiEditorStride_Word;
  }
  return UiEditorStride_Codepoint;
}

static void editor_visual_slices_clear(UiEditor* editor) {
  mem_set(array_mem(editor->visualSlices), 0);
}

static void editor_visual_slices_update(UiEditor* editor, const TimeSteady timeNow) {
  editor_visual_slices_clear(editor);
  u32 sliceIdx = 0;

  // Add the cursor visual element.
  static const TimeDuration g_blinkDelay    = time_second;
  static const TimeDuration g_blinkInterval = time_second;
  const TimeDuration durSinceInteract = time_steady_duration(editor->lastInteractTime, timeNow);
  if (durSinceInteract < g_blinkDelay || ((durSinceInteract / g_blinkInterval) % 2) == 0) {
    editor->visualSlices[sliceIdx++] = (UiEditorVisualSlice){g_editorCursorEsc, editor->cursor};
  }

  // Sort the slices by index.
  sort_quicksort_t(
      &editor->visualSlices[0],
      &editor->visualSlices[sliceIdx],
      UiEditorVisualSlice,
      ui_visual_slice_compare);
}

static void editor_visual_text_update(UiEditor* editor) {
  dynstring_clear(&editor->visualText);

  /**
   * The visual text consists of both the real text and additional visual elements (eg the cursor).
   * NOTE: The visual slices are sorted by index.
   */
  const String text    = dynstring_view(&editor->text);
  usize        textIdx = 0;
  array_for_t(editor->visualSlices, UiEditorVisualSlice, visualSlice) {
    if (!string_is_empty(visualSlice->text)) {
      const String beforeText = string_slice(text, textIdx, visualSlice->index - textIdx);
      dynstring_append(&editor->visualText, beforeText);
      dynstring_append(&editor->visualText, visualSlice->text);
      textIdx = visualSlice->index;
    }
  }
  const String remainingText = string_slice(text, textIdx, text.size - textIdx);
  dynstring_append(&editor->visualText, remainingText);
}

static usize editor_visual_index_to_text_index(UiEditor* editor, const usize visualIndex) {
  /**
   * To map from the visual text to the real text we need to substract the space that the visual
   * slices (for example the cursor) take.
   */
  usize index = visualIndex;
  array_for_t(editor->visualSlices, UiEditorVisualSlice, visualSlice) {
    if (index > visualSlice->index) {
      index -= visualSlice->text.size;
    }
  }
  diag_assert(editor_cursor_valid_index(editor, index));
  return index;
}

UiEditor* ui_editor_create(Allocator* alloc) {
  UiEditor* editor = alloc_alloc_t(alloc, UiEditor);

  *editor = (UiEditor){
      .alloc       = alloc,
      .textElement = sentinel_u64,
      .text        = dynstring_create(alloc, 256),
      .visualText  = dynstring_create(alloc, 256),
  };
  return editor;
}

void ui_editor_destroy(UiEditor* editor) {
  dynstring_destroy(&editor->text);
  dynstring_destroy(&editor->visualText);
  alloc_free_t(editor->alloc, editor);
}

bool ui_editor_active(const UiEditor* editor) {
  return (editor->flags & UiEditorFlags_Active) != 0;
}

UiId ui_editor_element(const UiEditor* editor) { return editor->textElement; }

String ui_editor_result_text(const UiEditor* editor) { return dynstring_view(&editor->text); }

String ui_editor_visual_text(const UiEditor* editor) { return dynstring_view(&editor->visualText); }

void ui_editor_start(UiEditor* editor, const String initialText, const UiId element) {
  if (ui_editor_active(editor)) {
    ui_editor_stop(editor);
  }
  editor->flags |= UiEditorFlags_Active | UiEditorFlags_FirstUpdate | UiEditorFlags_Dirty;
  editor->textElement = element;
  editor->cursor      = 0;

  dynstring_clear(&editor->text);
  editor_insert_text(editor, initialText, UiEditorSource_InitialText);

  editor_visual_slices_clear(editor);
  editor_visual_text_update(editor);
}

void ui_editor_update(UiEditor* editor, const GapWindowComp* win, const UiBuildHover hover) {
  diag_assert(editor->flags & UiEditorFlags_Active);

  const bool firstUpdate     = (editor->flags & UiEditorFlags_FirstUpdate) != 0;
  const bool cursorToHovered = (firstUpdate || gap_window_key_down(win, GapKey_MouseLeft));
  if (cursorToHovered && hover.id == editor->textElement) {
    editor->cursor = editor_visual_index_to_text_index(editor, hover.textCharIndex);
    editor->flags |= UiEditorFlags_Dirty;
  }

  editor_insert_text(editor, gap_window_input_text(win), UiEditorSource_UserTyped);

  if (gap_window_key_pressed(win, GapKey_Tab)) {
    editor_insert_cp(editor, Unicode_HorizontalTab);
  }
  if (gap_window_key_pressed(win, GapKey_Backspace)) {
    editor_erase_prev(editor, editor_stride_from_key_modifiers(win));
  }
  if (gap_window_key_pressed(win, GapKey_Delete)) {
    editor_erase_current(editor, editor_stride_from_key_modifiers(win));
  }
  if (gap_window_key_pressed(win, GapKey_ArrowRight)) {
    editor_cursor_next(editor, editor_stride_from_key_modifiers(win));
  }
  if (gap_window_key_pressed(win, GapKey_ArrowLeft)) {
    editor_cursor_prev(editor, editor_stride_from_key_modifiers(win));
  }
  if (gap_window_key_pressed(win, GapKey_Home)) {
    editor_cursor_to_start(editor);
  }
  if (gap_window_key_pressed(win, GapKey_End)) {
    editor_cursor_to_end(editor);
  }
  if (gap_window_key_pressed(win, GapKey_Escape) || gap_window_key_pressed(win, GapKey_Return)) {
    ui_editor_stop(editor);
  }

  const TimeSteady timeNow = time_steady_clock();
  if (editor->flags & UiEditorFlags_Dirty) {
    editor->lastInteractTime = timeNow;
  }
  editor_visual_slices_update(editor, timeNow);
  editor_visual_text_update(editor);
  editor->flags &= ~(UiEditorFlags_FirstUpdate | UiEditorFlags_Dirty);
}

void ui_editor_stop(UiEditor* editor) {
  editor->flags &= ~UiEditorFlags_Active;
  editor->textElement = sentinel_u64;
}
