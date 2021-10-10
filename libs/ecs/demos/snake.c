#include "cli.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_init.h"
#include "core_time.h"
#include "ecs.h"
#include "log.h"

// Temp:
#include "core_thread.h"

typedef enum {
  Direction_Up,
  Direction_Down,
  Direction_Left,
  Direction_Right,

  Direction_Count,
} Direction;

typedef struct {
  i32 x, y;
} Position;

typedef enum {
  GameStateType_Initial,
  GameStateType_Playing,
  GameStateType_GameOver,
} GameStateType;

ecs_comp_define(GameState) {
  GameStateType type;
  u64           score;
};

ecs_comp_define(Transform) {
  Position  pos;
  Direction dir;
};

ecs_comp_define(Graphics) {
  String characters[Direction_Count];
  bool   blink;
};

ecs_module_init(snake_module) {
  ecs_register_comp(GameState);
  ecs_register_comp(Transform);
  ecs_register_comp(Graphics);
}

static int run_snake() {
  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  ecs_register_module(def, snake_module);

  EcsWorld* world = ecs_world_create(g_alloc_heap, def);

  thread_sleep(time_seconds(5));

  ecs_world_destroy(world);
  ecs_def_destroy(def);
  return 0;
}

int main(const int argc, const char** argv) {
  core_init();
  log_init();

  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  int exitCode = 0;

  CliApp*     app      = cli_app_create(g_alloc_heap, string_lit("Volo Ecs Snake Demo"));
  const CliId helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);

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

  tty_set_window_title(string_lit("Volo Ecs Snake Demo"));

  exitCode = run_snake();

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  core_teardown();
  return exitCode;
}
