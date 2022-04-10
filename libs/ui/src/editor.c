#include "core_ascii.h"
#include "core_diag.h"
#include "core_unicode.h"
#include "core_utf8.h"
#include "gap_input.h"

#include "editor_internal.h"

static const String g_editorCursorEsc = string_static(uni_esc "c");

typedef enum {
  UiEditorFlags_Active      = 1 << 0,
  UiEditorFlags_FirstUpdate = 1 << 1,
} UiEditorFlags;

typedef enum {
  UiEditorStride_Codepoint,
  UiEditorStride_Word,
} UiEditorStride;

struct sUiEditor {
  Allocator*    alloc;
  UiEditorFlags flags;
  UiId          textElement;
  DynString     text, displayText;
  usize         cursor;
};

static bool editor_cp_is_valid(const Unicode cp) {
  if (ascii_is_control(cp)) {
    return false; // Control characters like tab / backspace are handled separately.
  }
  if (ascii_is_newline(cp)) {
    return false; // Multi line editing is not supported at this time.
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
}

static void editor_insert_text(UiEditor* editor, String text) {
  while (!string_is_empty(text)) {
    Unicode cp;
    text = utf8_cp_read(text, &cp);
    if (cp && editor_cp_is_valid(cp)) {
      editor_insert_cp(editor, cp);
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
}

static void editor_cursor_to_start(UiEditor* editor) { editor->cursor = 0; }
static void editor_cursor_to_end(UiEditor* editor) { editor->cursor = editor->text.size; }

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
}

static UiEditorStride editor_stride_from_key_modifiers(const GapWindowComp* win) {
  if (gap_window_key_down(win, GapKey_Control)) {
    return UiEditorStride_Word;
  }
  return UiEditorStride_Codepoint;
}

static void editor_update_display(UiEditor* editor) {
  dynstring_clear(&editor->displayText);
  dynstring_append(&editor->displayText, dynstring_view(&editor->text));
  dynstring_insert(&editor->displayText, g_editorCursorEsc, editor->cursor);
}

static usize editor_display_index_to_text_index(UiEditor* editor, const usize displayIndex) {
  /**
   * The displayed string contains a cursor which does not exist in the real text.
   * If the given position is beyond the cursor we substract the cursor sequence to compensate.
   */
  if (displayIndex <= editor->cursor) {
    return displayIndex;
  }
  return displayIndex - g_editorCursorEsc.size;
}

UiEditor* ui_editor_create(Allocator* alloc) {
  UiEditor* editor = alloc_alloc_t(alloc, UiEditor);

  *editor = (UiEditor){
      .alloc       = alloc,
      .textElement = sentinel_u64,
      .text        = dynstring_create(alloc, 256),
      .displayText = dynstring_create(alloc, 256),
  };
  return editor;
}

void ui_editor_destroy(UiEditor* editor) {
  dynstring_destroy(&editor->text);
  dynstring_destroy(&editor->displayText);
  alloc_free_t(editor->alloc, editor);
}

bool ui_editor_active(const UiEditor* editor) {
  return (editor->flags & UiEditorFlags_Active) != 0;
}

UiId ui_editor_element(const UiEditor* editor) { return editor->textElement; }

String ui_editor_result(const UiEditor* editor) { return dynstring_view(&editor->text); }

String ui_editor_display(const UiEditor* editor) { return dynstring_view(&editor->displayText); }

void ui_editor_start(UiEditor* editor, const String initialText, const UiId element) {
  diag_assert(utf8_validate(initialText));

  if (ui_editor_active(editor)) {
    ui_editor_stop(editor);
  }
  editor->flags |= UiEditorFlags_Active | UiEditorFlags_FirstUpdate;
  editor->textElement = element;

  dynstring_clear(&editor->text);
  dynstring_append(&editor->text, initialText);

  editor_cursor_to_end(editor);
}

void ui_editor_update(UiEditor* editor, const GapWindowComp* win, const UiBuildHover hover) {
  diag_assert(editor->flags & UiEditorFlags_Active);

  const bool firstUpdate     = (editor->flags & UiEditorFlags_FirstUpdate) != 0;
  const bool cursorToHovered = (firstUpdate || gap_window_key_down(win, GapKey_MouseLeft));
  if (cursorToHovered && hover.id == editor->textElement) {
    editor->cursor = editor_display_index_to_text_index(editor, hover.textCharIndex);
  }

  editor_insert_text(editor, gap_window_input_text(win));

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

  editor_update_display(editor);

  editor->flags &= ~UiEditorFlags_FirstUpdate;
}

void ui_editor_stop(UiEditor* editor) {
  editor->flags &= ~UiEditorFlags_Active;
  editor->textElement = sentinel_u64;
}
