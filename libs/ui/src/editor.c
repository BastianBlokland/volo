#include "core_ascii.h"
#include "core_diag.h"
#include "core_unicode.h"
#include "core_utf8.h"

#include "editor_internal.h"

typedef enum {
  UiEditorFlags_Active = 1 << 0,
} UiEditorFlags;

struct sUiEditor {
  Allocator*    alloc;
  UiEditorFlags flags;
  UiId          textElement;
  DynString     text, displayText;
  usize         cursor;
};

static usize editor_cp_bytes_at(UiEditor* editor, const usize index) {
  const u8 ch = *string_at(dynstring_view(&editor->text), index);
  diag_assert(!utf8_contchar(ch)); // Should be a utf8 starting char.
  return utf8_cp_bytes_from_first(ch);
}

// static Unicode editor_cp_at(UiEditor* editor, const usize index) {
//   const String total = dynstring_view(&editor->buffer);
//   Unicode      cp;
//   utf8_cp_read(string_consume(total, index), &cp);
//   return cp;
// }

static usize editor_cp_next(UiEditor* editor, const usize index) {
  String str = dynstring_view(&editor->text);
  for (usize i = index + 1; i < str.size; ++i) {
    if (!utf8_contchar(*string_at(str, i))) {
      return i;
    }
  }
  return sentinel_usize;
}

static usize editor_cp_prev(UiEditor* editor, const usize index) {
  String str = dynstring_view(&editor->text);
  for (usize i = index; i-- > 0;) {
    if (!utf8_contchar(*string_at(str, i))) {
      return i;
    }
  }
  return sentinel_usize;
}

static bool editor_validate_input_cp(const Unicode cp) {
  if (ascii_is_control(cp)) {
    return false; // Control characters like tab / backspace are handled separately.
  }
  if (ascii_is_newline(cp)) {
    return false; // Multi line editing is not supported at this time.
  }
  return true;
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
    if (cp && editor_validate_input_cp(cp)) {
      editor_insert_cp(editor, cp);
    }
  }
}

static void editor_erase_prev(UiEditor* editor) {
  const usize toErase = editor_cp_prev(editor, editor->cursor);
  if (!sentinel_check(toErase)) {
    const usize chars = editor_cp_bytes_at(editor, toErase);
    dynstring_erase_chars(&editor->text, toErase, chars);
    editor->cursor -= chars;
  }
}

static void editor_erase_current(UiEditor* editor) {
  if (editor->cursor != editor->text.size) {
    const usize chars = editor_cp_bytes_at(editor, editor->cursor);
    dynstring_erase_chars(&editor->text, editor->cursor, chars);
  }
}

static void editor_cursor_to_start(UiEditor* editor) { editor->cursor = 0; }
static void editor_cursor_to_end(UiEditor* editor) { editor->cursor = editor->text.size; }

static void editor_cursor_next(UiEditor* editor) {
  const usize prev = editor_cp_next(editor, editor->cursor);
  editor->cursor   = sentinel_check(prev) ? editor->text.size : prev;
}

static void editor_cursor_prev(UiEditor* editor) {
  const usize prev = editor_cp_prev(editor, editor->cursor);
  editor->cursor   = sentinel_check(prev) ? 0 : prev;
}

static void editor_update_display(UiEditor* editor) {
  dynstring_clear(&editor->displayText);
  dynstring_append(&editor->displayText, dynstring_view(&editor->text));
  dynstring_insert(&editor->displayText, string_lit(uni_esc "c"), editor->cursor);
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
  editor->flags |= UiEditorFlags_Active;
  editor->textElement = element;

  dynstring_clear(&editor->text);
  dynstring_append(&editor->text, initialText);

  editor_cursor_to_end(editor);
}

void ui_editor_update(UiEditor* editor, const GapWindowComp* win) {
  diag_assert(editor->flags & UiEditorFlags_Active);

  editor_insert_text(editor, gap_window_input_text(win));

  if (gap_window_key_pressed(win, GapKey_Backspace)) {
    editor_erase_prev(editor);
  }
  if (gap_window_key_pressed(win, GapKey_Delete)) {
    editor_erase_current(editor);
  }
  if (gap_window_key_pressed(win, GapKey_ArrowRight)) {
    editor_cursor_next(editor);
  }
  if (gap_window_key_pressed(win, GapKey_ArrowLeft)) {
    editor_cursor_prev(editor);
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
}

void ui_editor_stop(UiEditor* editor) {
  editor->flags &= ~UiEditorFlags_Active;
  editor->textElement = sentinel_u64;
}
