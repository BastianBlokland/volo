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
  DynString     buffer, displayBuffer;
  u32           cursor;
};

static usize editor_cp_bytes_at(UiEditor* editor, const usize index) {
  const u8 ch = *string_at(dynstring_view(&editor->buffer), index);
  diag_assert(!utf8_contchar(ch)); // Should be a utf8 starting char.
  return utf8_cp_bytes_from_first(ch);
}

// static Unicode editor_cp_at(UiEditor* editor, const usize index) {
//   const String total = dynstring_view(&editor->buffer);
//   Unicode      cp;
//   utf8_cp_read(string_consume(total, index), &cp);
//   return cp;
// }

static usize editor_cp_prev(UiEditor* editor, const usize index) {
  String str = dynstring_view(&editor->buffer);
  for (usize i = index; i-- > 0;) {
    if (!utf8_contchar(*string_at(str, i))) {
      return i;
    }
  }
  return sentinel_usize;
}

static void editor_cp_erase(UiEditor* editor, const usize index) {
  dynstring_erase_chars(&editor->buffer, index, editor_cp_bytes_at(editor, index));
}

static void editor_cp_erase_last(UiEditor* editor) {
  const usize toErase = editor_cp_prev(editor, editor->buffer.size);
  if (!sentinel_check(toErase)) {
    editor_cp_erase(editor, toErase);
  }
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

static void editor_input_text(UiEditor* editor, String text) {
  while (!string_is_empty(text)) {
    Unicode cp;
    text = utf8_cp_read(text, &cp);
    if (cp && editor_validate_input_cp(cp)) {
      utf8_cp_write(&editor->buffer, cp);
    }
  }
}

static void editor_update_display(UiEditor* editor) {
  dynstring_clear(&editor->displayBuffer);
  dynstring_append(&editor->displayBuffer, dynstring_view(&editor->buffer));
  dynstring_insert(&editor->displayBuffer, string_lit(uni_esc "c"), editor->cursor);
}

UiEditor* ui_editor_create(Allocator* alloc) {
  UiEditor* editor = alloc_alloc_t(alloc, UiEditor);

  *editor = (UiEditor){
      .alloc         = alloc,
      .textElement   = sentinel_u64,
      .buffer        = dynstring_create(alloc, 256),
      .displayBuffer = dynstring_create(alloc, 256),
  };
  return editor;
}

void ui_editor_destroy(UiEditor* editor) {
  dynstring_destroy(&editor->buffer);
  dynstring_destroy(&editor->displayBuffer);
  alloc_free_t(editor->alloc, editor);
}

bool ui_editor_active(const UiEditor* editor) {
  return (editor->flags & UiEditorFlags_Active) != 0;
}

UiId ui_editor_element(const UiEditor* editor) { return editor->textElement; }

String ui_editor_result(const UiEditor* editor) { return dynstring_view(&editor->buffer); }

String ui_editor_display(const UiEditor* editor) { return dynstring_view(&editor->displayBuffer); }

void ui_editor_start(UiEditor* editor, const String initialText, const UiId element) {
  diag_assert(utf8_validate(initialText));

  if (ui_editor_active(editor)) {
    ui_editor_stop(editor);
  }
  editor->flags |= UiEditorFlags_Active;
  editor->textElement = element;

  dynstring_clear(&editor->buffer);
  dynstring_append(&editor->buffer, initialText);
}

void ui_editor_update(UiEditor* editor, const GapWindowComp* win) {
  diag_assert(editor->flags & UiEditorFlags_Active);

  editor_input_text(editor, gap_window_input_text(win));
  if (gap_window_key_pressed(win, GapKey_Backspace)) {
    // TODO: How to handle key repeat?
    editor_cp_erase_last(editor);
  }
  if (gap_window_key_pressed(win, GapKey_Escape) || gap_window_key_pressed(win, GapKey_Return)) {
    ui_editor_stop(editor);
  }
  if (gap_window_key_pressed(win, GapKey_ArrowLeft)) {
    --editor->cursor;
  }
  if (gap_window_key_pressed(win, GapKey_ArrowRight)) {
    ++editor->cursor;
  }

  editor_update_display(editor);
}

void ui_editor_stop(UiEditor* editor) {
  editor->flags &= ~UiEditorFlags_Active;
  editor->textElement = sentinel_u64;
}
