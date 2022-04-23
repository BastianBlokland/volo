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

#define ui_editor_max_visual_slices 3

static const String       g_editorCursorEsc      = string_static(uni_esc "cFF");
static const String       g_editorSelectBeginEsc = string_static(uni_esc "@0000FF88" uni_esc "|00");
static const String       g_editorSelectEndEsc   = string_static(uni_esc "r");
static const TimeDuration g_editorBlinkDelay     = time_second;
static const TimeDuration g_editorBlinkInterval  = time_second;

typedef enum {
  UiEditorFlags_Active      = 1 << 0,
  UiEditorFlags_FirstUpdate = 1 << 1,
  UiEditorFlags_Dirty       = 1 << 2,
  UiEditorFlags_SelectMode  = 1 << 3,

  EiEditorFlags_Volatile = UiEditorFlags_FirstUpdate | UiEditorFlags_Dirty
} UiEditorFlags;

typedef enum {
  UiEditorStride_Codepoint,
  UiEditorStride_Word,
} UiEditorStride;

typedef enum {
  UiEditorSource_InitialText,
  UiEditorSource_UserTyped,
  UiEditorSource_Clipboard,
} UiEditorSource;

/**
 * Visual-slices are used to track exta elements in the visual text (for example the cursor).
 */
typedef struct {
  String text;
  usize  index; // Index into the actual text.
} UiEditorVisualSlice;

typedef struct {
  u32        repeat;
  TimeSteady lastTime;
  GapVector  lastPos;
} UiEditorClickInfo;

struct sUiEditor {
  Allocator*          alloc;
  UiEditorFlags       flags;
  UiId                textElement;
  DynString           text;
  usize               maxTextLength;
  usize               cursor;
  usize               selectBegin, selectEnd, selectPivot;
  TimeSteady          lastInteractTime;
  UiEditorClickInfo   click;
  usize               viewportOffset;
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
  case UiEditorSource_Clipboard:
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
  switch ((u32)cp) {
  case Unicode_Space:
  case Unicode_ZeroWidthSpace:
  case Unicode_HorizontalTab:
  case '.':
  case ',':
  case ':':
  case ';':
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

static usize editor_word_end_index(UiEditor* editor, usize index) {
  bool foundStartingWord = false;
  while (true) {
    const usize nextIndex = editor_next_index(editor, index);
    if (sentinel_check(nextIndex)) {
      return editor->text.size; // Return the end index when no more characters are found.
    }
    const Unicode nextCp      = editor_cp_at(editor, nextIndex);
    const bool    isSeperator = editor_cp_is_seperator(nextCp);
    foundStartingWord |= !isSeperator;
    if (isSeperator && foundStartingWord) {
      return nextIndex;
    }
    index = nextIndex;
  }
}

static usize editor_word_start_index(UiEditor* editor, usize index) {
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

static bool editor_cursor_valid_index(UiEditor* editor, const usize index) {
  if (index > editor->text.size + 1) {
    return false; // Out of bounds.
  }
  if (index == editor->text.size) {
    return true; // At the end of the text.
  }
  // Validate that the index is the start of a utf8 codepoint.
  const String total = dynstring_view(&editor->text);
  return !utf8_contchar(*string_at(total, index));
}

static void editor_cursor_set(UiEditor* editor, const usize index) {
  diag_assert(editor_cursor_valid_index(editor, index));

  if (editor->flags & UiEditorFlags_SelectMode) {
    editor->selectBegin = index > editor->selectPivot ? editor->selectPivot : index;
    editor->selectEnd   = index < editor->selectPivot ? editor->selectPivot : index;
  } else { // !select
    editor->selectBegin = editor->selectEnd = index;
  }
  editor->cursor = index;
  editor->flags |= UiEditorFlags_Dirty;
}

static void editor_cursor_to_start(UiEditor* editor) { editor_cursor_set(editor, 0); }
static void editor_cursor_to_end(UiEditor* editor) { editor_cursor_set(editor, editor->text.size); }

static void editor_cursor_next(UiEditor* editor, const UiEditorStride stride) {
  usize next;
  switch (stride) {
  case UiEditorStride_Codepoint:
    next = editor_next_index(editor, editor->cursor);
    break;
  case UiEditorStride_Word:
    next = editor_word_end_index(editor, editor->cursor);
    break;
  }
  editor_cursor_set(editor, sentinel_check(next) ? editor->text.size : next);
}

static void editor_cursor_prev(UiEditor* editor, const UiEditorStride stride) {
  usize prev;
  switch (stride) {
  case UiEditorStride_Codepoint:
    prev = editor_prev_index(editor, editor->cursor);
    break;
  case UiEditorStride_Word:
    prev = editor_word_start_index(editor, editor->cursor);
    break;
  }
  editor_cursor_set(editor, sentinel_check(prev) ? 0 : prev);
}

static bool editor_has_selection(UiEditor* editor) {
  return editor->selectBegin != editor->selectEnd;
}

static String editor_selection(UiEditor* editor) {
  const String totalText = dynstring_view(&editor->text);
  return string_slice(totalText, editor->selectBegin, editor->selectEnd - editor->selectBegin);
}

static void editor_erase_selection(UiEditor* editor) {
  const usize bytesToErase = editor->selectEnd - editor->selectBegin;
  dynstring_erase_chars(&editor->text, editor->selectBegin, bytesToErase);
  editor->selectPivot = editor->selectEnd = editor->selectBegin;
  if (editor->cursor > editor->selectBegin) {
    editor_cursor_set(editor, editor->cursor - bytesToErase);
  }
  editor->flags |= UiEditorFlags_Dirty;
}

static void editor_erase_prev(UiEditor* editor, const UiEditorStride stride) {
  usize eraseFrom;
  switch (stride) {
  case UiEditorStride_Codepoint:
    eraseFrom = editor_prev_index(editor, editor->cursor);
    break;
  case UiEditorStride_Word:
    eraseFrom = editor_word_start_index(editor, editor->cursor);
    break;
  }
  if (!sentinel_check(eraseFrom)) {
    const usize bytesToErase = editor->cursor - eraseFrom;
    dynstring_erase_chars(&editor->text, eraseFrom, bytesToErase);

    if (editor->selectPivot >= editor->cursor) {
      editor->selectPivot -= bytesToErase;
    }
    editor_cursor_set(editor, editor->cursor - bytesToErase);
  }
}

static void editor_erase_current(UiEditor* editor, const UiEditorStride stride) {
  usize eraseTo;
  switch (stride) {
  case UiEditorStride_Codepoint:
    eraseTo = editor_next_index(editor, editor->cursor);
    break;
  case UiEditorStride_Word:
    eraseTo = editor_word_end_index(editor, editor->cursor);
    break;
  }
  if (sentinel_check(eraseTo)) {
    eraseTo = editor->text.size; // No next found, just erase until the end.
  }
  const usize bytesToErase = eraseTo - editor->cursor;
  dynstring_erase_chars(&editor->text, editor->cursor, bytesToErase);

  if (editor->selectPivot > editor->cursor) {
    editor->selectPivot -= bytesToErase;
  }
  editor_cursor_set(editor, editor->cursor); // NOTE: Important for updating the select indices.
}

static void editor_select_mode_start(UiEditor* editor) {
  editor->selectPivot = editor->cursor;
  editor->flags |= UiEditorFlags_SelectMode;
}

static void editor_select_mode_stop(UiEditor* editor) {
  editor->flags &= ~UiEditorFlags_SelectMode;
}

static void editor_select_line(UiEditor* editor) {
  editor_cursor_set(editor, editor->text.size);
  editor->selectBegin = 0;
  editor->selectEnd   = editor->text.size;
}

static void editor_select_word(UiEditor* editor) {
  const usize begin = editor_word_start_index(editor, editor->cursor);
  const usize end   = editor_word_end_index(editor, editor->cursor);
  editor_cursor_set(editor, end);
  editor->selectBegin = begin;
  editor->selectEnd   = end;
}

static void editor_insert_cp(UiEditor* editor, const Unicode cp) {
  DynString buffer = dynstring_create_over(mem_stack(4));
  utf8_cp_write(&buffer, cp);

  if (UNLIKELY((editor->text.size + buffer.size) > editor->maxTextLength)) {
    return;
  }

  dynstring_insert(&editor->text, dynstring_view(&buffer), editor->cursor);
  editor_cursor_set(editor, editor->cursor + buffer.size);
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
        editor_select_mode_stop(editor);
        editor_erase_selection(editor);
        editor_insert_cp(editor, cp);
      }
      break;
    }
  }
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

  // Add the cursor visual slice.
  const TimeDuration sinceInteract = time_steady_duration(editor->lastInteractTime, timeNow);
  if (sinceInteract < g_editorBlinkDelay || ((sinceInteract / g_editorBlinkInterval) % 2) == 0) {
    editor->visualSlices[sliceIdx++] = (UiEditorVisualSlice){
        .text  = g_editorCursorEsc,
        .index = editor->cursor,
    };
  }

  // Add the selection visual slices.
  if (editor_has_selection(editor)) {
    editor->visualSlices[sliceIdx++] = (UiEditorVisualSlice){
        .text  = g_editorSelectBeginEsc,
        .index = math_max(editor->viewportOffset, editor->selectBegin),
    };
    editor->visualSlices[sliceIdx++] = (UiEditorVisualSlice){
        .text  = g_editorSelectEndEsc,
        .index = math_max(editor->viewportOffset, editor->selectEnd),
    };
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
  usize        textIdx = editor->viewportOffset;
  array_for_t(editor->visualSlices, UiEditorVisualSlice, visualSlice) {
    if (visualSlice->index >= textIdx && !string_is_empty(visualSlice->text)) {
      const String beforeText = string_slice(text, textIdx, visualSlice->index - textIdx);
      dynstring_append(&editor->visualText, beforeText);
      dynstring_append(&editor->visualText, visualSlice->text);
      textIdx = visualSlice->index;
    }
  }
  if (text.size > textIdx) {
    const String remainingText = string_slice(text, textIdx, text.size - textIdx);
    dynstring_append(&editor->visualText, remainingText);
  }
  // NOTE: Pad the end so the viewport logic knows there is space for additional text.
  dynstring_append_chars(&editor->visualText, ' ', 64);
}

static void editor_click(UiEditorClickInfo* click, GapWindowComp* win, const TimeSteady timeNow) {
  static const i32 g_maxMovement = 15;

  const GapVector cursorPos    = gap_window_param(win, GapParam_CursorPos);
  const i32       cursorDeltaX = math_abs(click->lastPos.x - cursorPos.x);
  const i32       cursorDeltaY = math_abs(click->lastPos.y - cursorPos.y);
  const bool      cursorIdle   = cursorDeltaX < g_maxMovement && cursorDeltaY < g_maxMovement;
  click->lastPos               = cursorPos;

  const TimeDuration sinceLastClick = time_steady_duration(click->lastTime, timeNow);
  click->lastTime                   = timeNow;

  if (cursorIdle && sinceLastClick <= gap_window_doubleclick_interval(win)) {
    ++click->repeat;
  } else {
    click->repeat = 0;
  }
}

/**
 * Map from an index in the visual text  (including cursor etc) to the real text.
 */
static usize editor_visual_index_to_text_index(UiEditor* editor, const usize visualIndex) {
  usize index = editor->viewportOffset + visualIndex;
  array_for_t(editor->visualSlices, UiEditorVisualSlice, visualSlice) {
    if (visualSlice->index >= editor->viewportOffset && visualSlice->index < index) {
      index -= visualSlice->text.size;
    }
  }
  return math_min(index, editor->text.size);
}

/**
 * Update the viewport to keep the cursor in the visible area of the text.
 */
static void editor_viewport_update(UiEditor* editor, const UiBuildTextInfo textInfo) {
  const usize viewportLeft = editor->viewportOffset;
  const usize cursorPrev   = editor_prev_index(editor, editor->cursor);
  const usize leftRef      = sentinel_check(cursorPrev) ? editor->cursor : cursorPrev;
  if (leftRef < viewportLeft) {
    editor->viewportOffset -= viewportLeft - leftRef;
  }

  const usize viewportRight = editor_visual_index_to_text_index(editor, textInfo.maxLineCharWidth);
  const usize cursorNext    = editor_next_index(editor, editor->cursor);
  const usize rightRef      = sentinel_check(cursorNext) ? editor->cursor : cursorNext;
  if (rightRef > viewportRight) {
    editor->viewportOffset += rightRef - viewportRight;
  }

  if (!editor_cursor_valid_index(editor, editor->viewportOffset)) {
    /**
     * The viewport starts at an invalid index (probably in the middle of a non-ascii character).
     * Fix up by moving it to the start of the next character.
     */
    editor->viewportOffset = editor_next_index(editor, editor->viewportOffset);
  }
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

void ui_editor_start(
    UiEditor* editor, const String initialText, const UiId element, const usize maxTextLength) {
  if (ui_editor_active(editor)) {
    ui_editor_stop(editor);
  }
  editor->flags |= UiEditorFlags_Active | UiEditorFlags_FirstUpdate | UiEditorFlags_Dirty;
  editor->textElement   = element;
  editor->maxTextLength = maxTextLength;
  editor->cursor = editor->selectBegin = editor->selectEnd = 0;

  dynstring_clear(&editor->text);
  editor_insert_text(editor, initialText, UiEditorSource_InitialText);

  editor->viewportOffset = 0;
  editor_visual_slices_clear(editor);
  editor_visual_text_update(editor);
}

void ui_editor_update(
    UiEditor*             editor,
    GapWindowComp*        win,
    const UiBuildHover    hover,
    const UiBuildTextInfo textInfo) {

  diag_assert(editor->flags & UiEditorFlags_Active);
  const bool       isHovering = hover.id == editor->textElement;
  const bool       dragging   = gap_window_key_down(win, GapKey_MouseLeft) && !editor->click.repeat;
  const bool       firstUpdate = (editor->flags & UiEditorFlags_FirstUpdate) != 0;
  const TimeSteady timeNow     = time_steady_clock();

  if (dragging && !sentinel_check(textInfo.hoveredCharIndex)) {
    editor_cursor_set(editor, editor_visual_index_to_text_index(editor, textInfo.hoveredCharIndex));
  }

  // NOTE: Assumes that the editor was started by a click.
  if (gap_window_key_pressed(win, GapKey_MouseLeft) || firstUpdate) {
    editor_click(&editor->click, win, timeNow);
    if (isHovering && !sentinel_check(textInfo.hoveredCharIndex)) {
      switch (editor->click.repeat % 3) {
      case 0:
        editor_cursor_set(
            editor, editor_visual_index_to_text_index(editor, textInfo.hoveredCharIndex));
        break;
      case 1:
        editor_select_word(editor);
        break;
      case 2:
        editor_select_line(editor);
        break;
      }
    } else if (!firstUpdate) {
      ui_editor_stop(editor);
      return;
    }
  }

  const bool shouldSelect =
      gap_window_key_down(win, GapKey_MouseLeft) || gap_window_key_down(win, GapKey_Shift);
  if (shouldSelect && !(editor->flags & UiEditorFlags_SelectMode)) {
    editor_select_mode_start(editor);
  }
  if (gap_window_key_released(win, GapKey_MouseLeft) && !gap_window_key_down(win, GapKey_Shift)) {
    editor_select_mode_stop(editor);
  }
  if (gap_window_key_released(win, GapKey_Shift)) {
    editor_select_mode_stop(editor);
  }

  if (gap_window_key_down(win, GapKey_Control)) {
    if (gap_window_key_pressed(win, GapKey_A)) {
      editor_select_line(editor);
    }
    if (gap_window_key_pressed(win, GapKey_C)) {
      gap_window_clip_copy(win, editor_selection(editor));
    }
    if (gap_window_key_pressed(win, GapKey_X)) {
      gap_window_clip_copy(win, editor_selection(editor));
      editor_erase_selection(editor);
    }
    if (gap_window_key_pressed(win, GapKey_V)) {
      gap_window_clip_paste(win);
    }
  } else {
    editor_insert_text(editor, gap_window_input_text(win), UiEditorSource_UserTyped);
  }
  if (gap_window_events(win) & GapWindowEvents_ClipPaste) {
    editor_insert_text(editor, gap_window_clip_paste_result(win), UiEditorSource_Clipboard);
  }

  if (gap_window_key_pressed(win, GapKey_Tab)) {
    editor_insert_cp(editor, Unicode_HorizontalTab);
  }
  if (gap_window_key_pressed_with_repeat(win, GapKey_Backspace)) {
    if (editor_has_selection(editor)) {
      editor_erase_selection(editor);
    } else {
      editor_erase_prev(editor, editor_stride_from_key_modifiers(win));
    }
  }
  if (gap_window_key_pressed_with_repeat(win, GapKey_Delete)) {
    if (editor_has_selection(editor)) {
      editor_erase_selection(editor);
    } else {
      editor_erase_current(editor, editor_stride_from_key_modifiers(win));
    }
  }
  if (gap_window_key_pressed_with_repeat(win, GapKey_ArrowRight)) {
    if (editor_has_selection(editor) && !(editor->flags & UiEditorFlags_SelectMode)) {
      editor_cursor_set(editor, editor->selectEnd);
    } else {
      editor_cursor_next(editor, editor_stride_from_key_modifiers(win));
    }
  }
  if (gap_window_key_pressed_with_repeat(win, GapKey_ArrowLeft)) {
    if (editor_has_selection(editor) && !(editor->flags & UiEditorFlags_SelectMode)) {
      editor_cursor_set(editor, editor->selectBegin);
    } else {
      editor_cursor_prev(editor, editor_stride_from_key_modifiers(win));
    }
  }
  if (gap_window_key_pressed(win, GapKey_Home)) {
    editor_cursor_to_start(editor);
  }
  if (gap_window_key_pressed(win, GapKey_End)) {
    editor_cursor_to_end(editor);
  }
  if (gap_window_key_pressed(win, GapKey_Escape) || gap_window_key_pressed(win, GapKey_Return)) {
    ui_editor_stop(editor);
    return;
  }

  if (editor->flags & UiEditorFlags_Dirty) {
    editor->lastInteractTime = timeNow;
  }
  editor_viewport_update(editor, textInfo);
  editor_visual_slices_update(editor, timeNow);
  editor_visual_text_update(editor);
  editor->flags &= ~EiEditorFlags_Volatile;
}

void ui_editor_stop(UiEditor* editor) {
  editor_select_mode_stop(editor);
  editor->flags &= ~(UiEditorFlags_Active | EiEditorFlags_Volatile);
  editor->textElement = sentinel_u64;
  editor->click       = (UiEditorClickInfo){0};
}
