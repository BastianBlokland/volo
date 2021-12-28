#include "cli.h"
#include "core.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_signal.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs.h"
#include "jobs.h"
#include "log.h"

/**
 * Volo Snake Demo - Simple terminal snake game implemented using the ECS library.
 */

typedef enum {
  Direction_Up,
  Direction_Down,
  Direction_Right,
  Direction_Left,

  Direction_Count,
} Direction;

typedef enum {
  GameState_Playing,
  GameState_GameOver,

  GameState_Count,
} GameState;

typedef enum {
  Input_None      = 0,
  Input_Restart   = 1 << 0,
  Input_Quit      = 1 << 1,
  Input_TurnUp    = 1 << 2,
  Input_TurnDown  = 1 << 3,
  Input_TurnRight = 1 << 4,
  Input_TurnLeft  = 1 << 5,
  Input_Turn      = Input_TurnUp | Input_TurnDown | Input_TurnRight | Input_TurnLeft,
} InputType;

typedef struct {
  InputType input;
  String    str;
} InputMapping;

static const InputMapping g_inputMappings[] = {
    {Input_TurnUp, string_static(tty_esc "[A")},
    {Input_TurnDown, string_static(tty_esc "[B")},
    {Input_TurnRight, string_static(tty_esc "[C")},
    {Input_TurnLeft, string_static(tty_esc "[D")},
    {Input_TurnUp, string_static("w")},
    {Input_TurnDown, string_static("s")},
    {Input_TurnRight, string_static("d")},
    {Input_TurnLeft, string_static("a")},
    {Input_Restart, string_static("r")},
    {Input_Restart, string_static(" ")},
    {Input_Quit, string_static(tty_esc)},
};

ecs_comp_define(InputComp) {
  u64       pickupDensity;
  i32       termWidth, termHeight;
  InputType input;
};
ecs_comp_define(ResultComp) {
  GameState state;
  u32       score;
};
ecs_comp_define(InitializeComp);
ecs_comp_define(QuitComp);

ecs_comp_define(GraphicsComp) {
  String   text;
  TtyStyle style;
};
ecs_comp_define(PositionComp) { i32 x, y; };
ecs_comp_define(ColliderComp);
ecs_comp_define(VelocityComp) { Direction dir; };

ecs_comp_define(PlayerComp) { i64 serial, tailLength; };
ecs_comp_define(TailComp) { i64 serial; };
ecs_comp_define(DeadComp);
ecs_comp_define(ResetableComp);
ecs_comp_define(PickupComp) { u32 score; };

static void input_read(InputComp* comp, String inputText) {
  comp->input = Input_None;
  while (!string_is_empty(inputText)) {

    array_for_t(g_inputMappings, InputMapping, mapping) {
      if (string_starts_with(inputText, mapping->str)) {
        comp->input |= mapping->input;
        inputText = string_consume(inputText, mapping->str.size);
        goto ValidInput;
      }
    }
    inputText = string_consume(inputText, 1);

  ValidInput:
    continue;
  }
}

static Direction input_steer(const Direction dir, const InputType input) {
  if (input & Input_TurnUp && dir != Direction_Down) {
    return Direction_Up;
  }
  if (input & Input_TurnDown && dir != Direction_Up) {
    return Direction_Down;
  }
  if (input & Input_TurnRight && dir != Direction_Left) {
    return Direction_Right;
  }
  if (input & Input_TurnLeft && dir != Direction_Right) {
    return Direction_Left;
  }
  return dir;
}

static i32 wrap(i32 val, const i32 max) {
  while (val >= max - 2) {
    val -= max - 2;
  }
  while (val < 1) {
    val += max - 2;
  }
  return val;
}

ecs_view_define(InitSysGlobal) {
  ecs_access_with(InitializeComp);
  ecs_access_read(InputComp);
  ecs_access_write(ResultComp);
}

ecs_view_define(InitSysResettable) { ecs_access_with(ResetableComp); }

ecs_system_define(InitSys) {
  EcsIterator* initializeItr = ecs_view_first(ecs_world_view_t(world, InitSysGlobal));
  if (!initializeItr) {
    return;
  }
  ecs_world_remove_t(world, ecs_view_entity(initializeItr), InitializeComp);

  const InputComp* inputComp  = ecs_view_read_t(initializeItr, InputComp);
  ResultComp*      resultComp = ecs_view_write_t(initializeItr, ResultComp);
  resultComp->score           = 0;
  resultComp->state           = GameState_Playing;

  // Cleanup entities from the previous session.
  EcsView* resetableView = ecs_world_view_t(world, InitSysResettable);
  for (EcsIterator* itr = ecs_view_itr(resetableView); ecs_view_walk(itr);) {
    ecs_world_entity_destroy(world, ecs_view_entity(itr));
  }

  // Spawn a new player.
  const EcsEntityId player = ecs_world_entity_create(world);
  ecs_world_add_t(world, player, PlayerComp, .tailLength = 1);
  ecs_world_add_t(
      world, player, PositionComp, .x = inputComp->termWidth / 2, .y = inputComp->termHeight / 2);
  ecs_world_add_t(world, player, VelocityComp, .dir = rng_sample_range(g_rng, 0, Direction_Count));
  ecs_world_add_t(world, player, GraphicsComp, .text = string_lit("●"));
  ecs_world_add_empty_t(world, player, ResetableComp);
}

ecs_view_define(SpawnPickupsSysGlobal) { ecs_access_read(InputComp); }

ecs_view_define(SpawnPickupsSysPickup) { ecs_access_with(PickupComp); }

ecs_system_define(SpawnPickupsSys) {
  EcsIterator*     globalItr = ecs_view_first(ecs_world_view_t(world, SpawnPickupsSysGlobal));
  const InputComp* inputComp = ecs_view_read_t(globalItr, InputComp);

  const usize desiredPickups = (usize)math_round_f64(
      inputComp->termWidth * inputComp->termHeight * (inputComp->pickupDensity * .001f));

  // Count the number of current pickups.
  usize    pickupCount = 0;
  EcsView* pickupView  = ecs_world_view_t(world, SpawnPickupsSysPickup);
  for (EcsIterator* itr = ecs_view_itr(pickupView); ecs_view_walk(itr); ++pickupCount)
    ;

  // Spawn additional pickups if required.
  for (usize i = pickupCount; i < desiredPickups; ++i) {
    const EcsEntityId pickup        = ecs_world_entity_create(world);
    const bool        specialPickup = rng_sample_f32(g_rng) > 0.9f;
    const TtyFgColor  color         = specialPickup ? TtyFgColor_Blue : TtyFgColor_Yellow;
    const i32         posX          = (i32)rng_sample_range(g_rng, 1, inputComp->termWidth - 2);
    const i32         posY          = (i32)rng_sample_range(g_rng, 1, inputComp->termHeight - 2);

    ecs_world_add_t(world, pickup, PickupComp, .score = specialPickup ? 10 : 1);
    ecs_world_add_t(world, pickup, PositionComp, .x = posX, .y = posY);
    ecs_world_add_t(
        world,
        pickup,
        GraphicsComp,
        .text  = string_lit("●"),
        .style = ttystyle(.fgColor = color, .flags = TtyStyleFlags_Bold));
    ecs_world_add_empty_t(world, pickup, ColliderComp);
    ecs_world_add_empty_t(world, pickup, ResetableComp);
  }
}

ecs_view_define(InputSysGlobal) { ecs_access_write(InputComp); }

ecs_system_define(InputSys) {
  DynString inputBuffer = dynstring_create(g_alloc_scratch, usize_kibibyte);
  tty_read(g_file_stdin, &inputBuffer, TtyReadFlags_NoBlock);

  EcsIterator* itr       = ecs_view_first(ecs_world_view_t(world, InputSysGlobal));
  InputComp*   inputComp = ecs_view_write_t(itr, InputComp);
  inputComp->termHeight  = tty_height(g_file_stdout);
  inputComp->termWidth   = tty_width(g_file_stdout);
  input_read(inputComp, dynstring_view(&inputBuffer));

  if (inputComp->input & Input_Quit) {
    ecs_world_add_empty_t(world, ecs_view_entity(itr), QuitComp);
  }
  if (inputComp->input & Input_Restart) {
    ecs_world_add_empty_t(world, ecs_view_entity(itr), InitializeComp);
  }
}

ecs_view_define(SteerSysGlobal) { ecs_access_read(InputComp); }

ecs_view_define(SteerSysPlayer) {
  ecs_access_with(PlayerComp);
  ecs_access_write(VelocityComp);
  ecs_access_write(GraphicsComp);
}

ecs_system_define(SteerSys) {
  EcsIterator*     globalItr = ecs_view_first(ecs_world_view_t(world, SteerSysGlobal));
  const InputComp* inputComp = ecs_view_read_t(globalItr, InputComp);

  EcsIterator* playerItr = ecs_view_first(ecs_world_view_t(world, SteerSysPlayer));
  if (playerItr) {
    VelocityComp* velocityComp = ecs_view_write_t(playerItr, VelocityComp);
    velocityComp->dir          = input_steer(velocityComp->dir, inputComp->input);

    GraphicsComp* graphicsComp = ecs_view_write_t(playerItr, GraphicsComp);
    switch (velocityComp->dir) {
    case Direction_Count:
    case Direction_Up:
      graphicsComp->text = string_lit("▲");
      break;
    case Direction_Down:
      graphicsComp->text = string_lit("▼");
      break;
    case Direction_Right:
      graphicsComp->text = string_lit("►");
      break;
    case Direction_Left:
      graphicsComp->text = string_lit("◄");
      break;
    }
  }
}

ecs_view_define(UpdateTailSysPlayer) {
  ecs_access_without(DeadComp);
  ecs_access_write(PlayerComp);
  ecs_access_read(PositionComp);
}

ecs_view_define(UpdateTailSysEntity) { ecs_access_read(TailComp); }

ecs_system_define(UpdateTailSys) {
  EcsIterator* playerItr = ecs_view_first(ecs_world_view_t(world, UpdateTailSysPlayer));
  if (!playerItr) {
    return;
  }

  const PositionComp* posComp    = ecs_view_read_t(playerItr, PositionComp);
  PlayerComp*         playerComp = ecs_view_write_t(playerItr, PlayerComp);
  ++playerComp->serial;

  // Spawn a new tail segment at the player position.
  const EcsEntityId seg = ecs_world_entity_create(world);
  ecs_world_add_t(world, seg, PositionComp, .x = posComp->x, .y = posComp->y);
  ecs_world_add_t(world, seg, GraphicsComp, .text = string_lit("●"));
  ecs_world_add_t(world, seg, TailComp, .serial = playerComp->serial);
  ecs_world_add_empty_t(world, seg, ColliderComp);
  ecs_world_add_empty_t(world, seg, ResetableComp);

  // Delete too old tail segments.
  EcsView* tailView = ecs_world_view_t(world, UpdateTailSysEntity);
  for (EcsIterator* tailItr = ecs_view_itr(tailView); ecs_view_walk(tailItr);) {
    const TailComp* tailComp = ecs_view_read_t(tailItr, TailComp);
    const i64       serial   = tailComp->serial;
    if (serial > playerComp->serial || serial <= playerComp->serial - playerComp->tailLength) {
      ecs_world_entity_destroy(world, ecs_view_entity(tailItr));
    }
  }
}

ecs_view_define(MoveSysGlobal) { ecs_access_read(InputComp); }

ecs_view_define(MoveSysEntity) {
  ecs_access_read(VelocityComp);
  ecs_access_write(PositionComp);
}

ecs_system_define(MoveSys) {
  EcsIterator*     globalItr = ecs_view_first(ecs_world_view_t(world, MoveSysGlobal));
  const InputComp* inputComp = ecs_view_read_t(globalItr, InputComp);

  EcsView* moveSysEntity = ecs_world_view_t(world, MoveSysEntity);
  for (EcsIterator* itr = ecs_view_itr(moveSysEntity); ecs_view_walk(itr);) {

    const VelocityComp* velo = ecs_view_read_t(itr, VelocityComp);
    PositionComp*       pos  = ecs_view_write_t(itr, PositionComp);
    if (velo->dir <= Direction_Down) {
      pos->y += velo->dir == Direction_Up ? -1 : 1;
    } else {
      pos->x += velo->dir == Direction_Right ? 1 : -1;
    }
    pos->x = wrap(pos->x, inputComp->termWidth);
    pos->y = wrap(pos->y, inputComp->termHeight);
  }
}

ecs_view_define(CollisionSysGlobal) { ecs_access_write(ResultComp); }

ecs_view_define(CollisionSysPlayer) {
  ecs_access_without(DeadComp);
  ecs_access_write(PlayerComp);
  ecs_access_read(PositionComp);
}

ecs_view_define(CollisionSysCollidable) {
  ecs_access_with(ColliderComp);
  ecs_access_read(PositionComp);
  ecs_access_maybe_read(PickupComp);
}

ecs_system_define(CollisionSys) {
  EcsIterator* globalItr  = ecs_view_first(ecs_world_view_t(world, CollisionSysGlobal));
  ResultComp*  resultComp = ecs_view_write_t(globalItr, ResultComp);

  EcsIterator* playerItr = ecs_view_first(ecs_world_view_t(world, CollisionSysPlayer));
  if (!playerItr) {
    return;
  }
  PlayerComp*         playerComp = ecs_view_write_t(playerItr, PlayerComp);
  const PositionComp* playerPos  = ecs_view_read_t(playerItr, PositionComp);

  // Test if the player hits any collidables.
  EcsView* collidablesView = ecs_world_view_t(world, CollisionSysCollidable);
  for (EcsIterator* itr = ecs_view_itr(collidablesView); ecs_view_walk(itr);) {
    const EcsEntityId   colliderEntity = ecs_view_entity(itr);
    const PositionComp* colliderPos    = ecs_view_read_t(itr, PositionComp);

    if (playerPos->x == colliderPos->x && playerPos->y == colliderPos->y) {
      ecs_world_entity_destroy(world, colliderEntity);
      const PickupComp* pickup = ecs_view_read_t(itr, PickupComp);
      if (pickup) {
        // Hit a pickup; gain points.
        playerComp->tailLength += pickup->score;
        resultComp->score += pickup->score;
      } else {
        // Hit something else then a pickup; gameover.
        ecs_world_add_empty_t(world, ecs_view_entity(playerItr), DeadComp);
        ecs_world_remove_t(world, ecs_view_entity(playerItr), VelocityComp);
        resultComp->state = GameState_GameOver;
      }
    }
  }
}

static void tty_draw_border(DynString* str, const i32 width, const i32 height) {
  tty_write_set_cursor_sequence(str, 1, 1);
  dynstring_append(str, string_lit("┌"));
  for (i32 i = 1; i != width - 1; ++i) {
    dynstring_append(str, string_lit("─"));
  }
  dynstring_append(str, string_lit("┐"));

  tty_write_set_cursor_sequence(str, height, 1);
  dynstring_append(str, string_lit("└"));
  for (i32 i = 1; i != width - 1; ++i) {
    dynstring_append(str, string_lit("─"));
  }
  dynstring_append(str, string_lit("┘"));

  for (i32 i = 1; i != height - 1; ++i) {
    tty_write_set_cursor_sequence(str, i + 1, 1);
    dynstring_append(str, string_lit("│"));
  }

  for (i32 i = 1; i != height - 1; ++i) {
    tty_write_set_cursor_sequence(str, i + 1, width);
    dynstring_append(str, string_lit("│"));
  }
}

ecs_view_define(RenderSysGlobal) {
  ecs_access_read(InputComp);
  ecs_access_read(ResultComp);
}

ecs_view_define(RenderSysEntity) {
  ecs_access_read(PositionComp);
  ecs_access_read(GraphicsComp);
}

ecs_system_define(RenderSys) {
  DynString str = dynstring_create(g_alloc_scratch, usize_kibibyte);
  tty_write_clear_sequence(&str, TtyClearMode_All);
  tty_write_cursor_show_sequence(&str, false);

  EcsIterator*      globalItr  = ecs_view_first(ecs_world_view_t(world, RenderSysGlobal));
  const InputComp*  inputComp  = ecs_view_read_t(globalItr, InputComp);
  const ResultComp* resultComp = ecs_view_read_t(globalItr, ResultComp);

  const TtyBgColor borderCol =
      resultComp->state == GameState_Playing ? TtyBgColor_Green : TtyBgColor_Red;
  tty_write_style_sequence(&str, ttystyle(.fgColor = TtyFgColor_BrightWhite, .bgColor = borderCol));

  tty_draw_border(&str, inputComp->termWidth, inputComp->termHeight);
  tty_write_set_cursor_sequence(&str, 1, 5);
  fmt_write(&str, " Volo Snake Demo ─ Score: {} ", fmt_int(resultComp->score));

  tty_write_style_sequence(&str, ttystyle());

  EcsView* renderablesView = ecs_world_view_t(world, RenderSysEntity);
  for (EcsIterator* itr = ecs_view_itr(renderablesView); ecs_view_walk(itr);) {
    const PositionComp* pos     = ecs_view_read_t(itr, PositionComp);
    const GraphicsComp* graphic = ecs_view_read_t(itr, GraphicsComp);

    tty_write_set_cursor_sequence(&str, pos->y + 1, pos->x + 1);
    tty_write_style_sequence(&str, graphic->style);
    dynstring_append(&str, graphic->text);
  }

  file_write_sync(g_file_stdout, dynstring_view(&str));
  dynstring_destroy(&str);
}

ecs_module_init(snake_module) {
  ecs_register_comp_empty(ColliderComp);
  ecs_register_comp_empty(DeadComp);
  ecs_register_comp_empty(InitializeComp);
  ecs_register_comp_empty(QuitComp);
  ecs_register_comp_empty(ResetableComp);
  ecs_register_comp(GraphicsComp);
  ecs_register_comp(InputComp);
  ecs_register_comp(PickupComp);
  ecs_register_comp(PlayerComp);
  ecs_register_comp(PositionComp);
  ecs_register_comp(ResultComp);
  ecs_register_comp(TailComp);
  ecs_register_comp(VelocityComp);

  ecs_register_system(InputSys, ecs_register_view(InputSysGlobal));
  ecs_register_system(
      InitSys, ecs_register_view(InitSysGlobal), ecs_register_view(InitSysResettable));
  ecs_register_system(
      SteerSys, ecs_register_view(SteerSysGlobal), ecs_register_view(SteerSysPlayer));
  ecs_register_system(MoveSys, ecs_register_view(MoveSysGlobal), ecs_register_view(MoveSysEntity));
  ecs_register_system(
      SpawnPickupsSys,
      ecs_register_view(SpawnPickupsSysGlobal),
      ecs_register_view(SpawnPickupsSysPickup));
  ecs_register_system(
      UpdateTailSys,
      ecs_register_view(UpdateTailSysPlayer),
      ecs_register_view(UpdateTailSysEntity));
  ecs_register_system(
      RenderSys, ecs_register_view(RenderSysGlobal), ecs_register_view(RenderSysEntity));
  ecs_register_system(
      CollisionSys,
      ecs_register_view(CollisionSysGlobal),
      ecs_register_view(CollisionSysPlayer),
      ecs_register_view(CollisionSysCollidable));
}

static int run_snake(const u64 frequency, const u64 pickupDensity) {
  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  ecs_register_module(def, snake_module);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  const EcsEntityId global = ecs_world_global(world);
  ecs_world_add_t(world, global, ResultComp);
  ecs_world_add_t(world, global, InputComp, .pickupDensity = pickupDensity);
  ecs_world_add_empty_t(world, global, InitializeComp);

  ecs_world_flush(world);

  while (!signal_is_received(Signal_Interupt) && !ecs_world_has_t(world, global, QuitComp)) {
    ecs_run_sync(runner);
    thread_sleep(time_second / frequency);
  }

  ecs_runner_destroy(runner);
  ecs_world_destroy(world);
  ecs_def_destroy(def);
  return 0;
}

static void tty_setup() {
  tty_opts_set(g_file_stdin, TtyOpts_NoEcho | TtyOpts_NoBuffer);

  DynString str = dynstring_create(g_alloc_scratch, usize_kibibyte);
  tty_write_window_title_sequence(&str, string_lit("Volo Snake Demo"));
  tty_write_cursor_show_sequence(&str, false);
  tty_write_alt_screen_sequence(&str, true);

  file_write_sync(g_file_stdout, dynstring_view(&str));
  dynstring_destroy(&str);
}

static void tty_reset() {
  tty_opts_set(g_file_stdin, TtyOpts_None);

  DynString str = dynstring_create(g_alloc_scratch, usize_kibibyte);
  tty_write_cursor_show_sequence(&str, true);
  tty_write_alt_screen_sequence(&str, false);

  file_write_sync(g_file_stdout, dynstring_view(&str));
  dynstring_destroy(&str);
}

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  int exitCode = 0;

  static const String appDesc = string_static("Volo Snake Demo\n"
                                              "\n"
                                              "Controls:\n"
                                              "- Steering: arrows / wasd\n"
                                              "- Restart: r / space\n"
                                              "- Quit: esc / interupt");

  CliApp*     app      = cli_app_create(g_alloc_heap, appDesc);
  const CliId freqFlag = cli_register_flag(app, 'f', string_lit("frequency"), CliOptionFlags_Value);
  const CliId pickupsFlag =
      cli_register_flag(app, 'p', string_lit("pickups"), CliOptionFlags_Value);
  const CliId helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);

  cli_register_desc(app, helpFlag, string_lit("Display this help page."));
  cli_register_desc(app, freqFlag, string_lit("Simulation frequency (in hertz)."));
  cli_register_desc(app, pickupsFlag, string_lit("Density of pickups."));
  cli_register_exclusions(app, helpFlag, freqFlag, pickupsFlag);

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  if (cli_parse_provided(invoc, helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    goto exit;
  }

  if (!tty_isatty(g_file_stdin) || !tty_isatty(g_file_stdout)) {
    file_write_sync(g_file_stderr, string_lit("An interactive terminal is required\n"));
    exitCode = 1;
    goto exit;
  }

  tty_setup();

  const u64 frequency     = cli_read_u64(invoc, freqFlag, 15);
  const u64 pickupDensity = cli_read_u64(invoc, pickupsFlag, 5);
  exitCode                = run_snake(frequency, pickupDensity);

  tty_reset();

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
