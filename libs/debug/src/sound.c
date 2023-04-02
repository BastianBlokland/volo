#include "core_format.h"
#include "core_math.h"
#include "debug_sound.h"
#include "ecs_world.h"
#include "snd.h"
#include "ui.h"

ecs_comp_define(DebugSoundPanelComp) { UiPanel panel; };

ecs_view_define(GlobalView) { ecs_access_write(SndMixerComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugSoundPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void sound_draw_scope(UiCanvasComp* canvas, const SndMixerView buf, const SndChannel chan) {
  static const f32 g_step = 1.0f / 256.0f;

  ui_style_push(canvas);
  ui_style_outline(canvas, 0);

  for (f32 t = 0.0; t < 1.0f; t += g_step) {
    const f32 sample    = snd_mixer_sample(buf, chan, t);
    const f32 sampleAbs = math_abs(sample);

    const UiVector size = {.width = g_step, .height = sampleAbs * 0.5f};
    const UiVector pos  = {.x = t, .y = sample > 0.0f ? 0.5f : 0.5f - size.height};

    UiColor color;
    if (sampleAbs >= 0.95f) {
      color = ui_color_red;
    } else if (sampleAbs >= 0.8f) {
      color = ui_color_yellow;
    } else {
      color = ui_color_white;
    }
    ui_style_color(canvas, color);

    ui_layout_push(canvas);
    ui_layout_set(canvas, ui_rect(pos, size), UiBase_Current);
    ui_canvas_draw_glyph(canvas, UiShape_Square, 5, UiFlags_None);
    ui_layout_pop(canvas);
  }

  ui_style_pop(canvas);
}

static void
sound_panel_draw(UiCanvasComp* canvas, DebugSoundPanelComp* panelComp, SndMixerComp* mixer) {

  const String title = fmt_write_scratch("{} Sound Panel", fmt_ui_shape(MusicNote));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  const SndMixerView history = snd_mixer_history(mixer);
  sound_draw_scope(canvas, history, SndChannel_Left);

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugSoundUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp* mixer = ecs_view_write_t(globalItr, SndMixerComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId    entity    = ecs_view_entity(itr);
    DebugSoundPanelComp* panelComp = ecs_view_write_t(itr, DebugSoundPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    sound_panel_draw(canvas, panelComp, mixer);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_sound_module) {
  ecs_register_comp(DebugSoundPanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugSoundUpdatePanelSys, ecs_view_id(GlobalView), ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_sound_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugSoundPanelComp,
      .panel = ui_panel(.position = ui_vector(0.75f, 0.5f), .size = ui_vector(330, 190)));
  return panelEntity;
}
