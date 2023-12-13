#include "asset_script.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "script_binder.h"
#include "script_diag.h"
#include "script_read.h"
#include "script_sig.h"

#include "repo_internal.h"

static ScriptBinder* g_scriptBinder;

static void bind(
    ScriptBinder*      binder,
    const String       name,
    const String       doc,
    const ScriptMask   retMask,
    const ScriptSigArg args[],
    const u8           argCount) {
  const ScriptSig* sig = script_sig_create(g_alloc_scratch, retMask, args, argCount);
  script_binder_declare(binder, name, doc, sig, null);
}

static void asset_binder_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_scriptBinder)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_scriptBinder) {
    ScriptBinder* binder = script_binder_create(g_alloc_persist);
    // clang-format off
    {
      const String     name = string_lit("self");
      const String     doc  = string_lit("Return the entity that is executing the current script.");
      const ScriptMask ret  = script_mask_entity;
      bind(binder, name, doc, ret, null, 0);
    }
    {
      const String       name   = string_lit("exists");
      const String       doc    = string_lit("Test if the given entity still exists.\n\n*Note*: Returns false if input value is null.");
      const ScriptMask   ret    = script_mask_bool;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("position");
      const String       doc    = string_lit("Lookup the position of the given entity.");
      const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("velocity");
      const String       doc    = string_lit("Lookup the velocity of the given entity.");
      const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("rotation");
      const String       doc    = string_lit("Lookup the rotation of the given entity.");
      const ScriptMask   ret    = script_mask_quat | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("scale");
      const String       doc    = string_lit("Lookup the scale of the given entity.");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("name");
      const String       doc    = string_lit("Lookup the name of the given entity.");
      const ScriptMask   ret    = script_mask_str | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("faction");
      const String       doc    = string_lit("Lookup the faction of the given entity.");
      const ScriptMask   ret    = script_mask_str | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("health");
      const String       doc    = string_lit("Lookup the health points of the given entity.");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("visible");
      const String       doc    = string_lit("Check if the given position is visible for this faction.");
      const ScriptMask   ret    = script_mask_bool;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("faction"), script_mask_str | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("time");
      const String       doc    = string_lit("Lookup the current time.\n\nSupported clocks:\n\n-`Time` (default)\n\n-`RealTime`\n\n-`Delta`\n\n-`RealDelta`\n\n-`Ticks`");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("clock"), script_mask_str | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("set");
      const String       doc    = string_lit("Change or query if the target entity is contained in the given set.");
      const ScriptMask   ret    = script_mask_bool | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("set"), script_mask_str},
          {string_lit("add"), script_mask_bool | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("query_set");
      const String       doc    = string_lit("Find all entities in the given set.\n\n*Note*: Returns a query handle.");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("set"), script_mask_str},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("query_sphere");
      const String       doc    = string_lit("Find all the entities that are touching the given sphere.\n\n*Note*: Returns a query handle.\n\nSupported layers:\n\n-`Environment`\n\n-`Destructible`\n\n-`Infantry`\n\n-`Structure`\n\n-`Unit`\n\n-`Debug`\n\n-`AllIncludingDebug`\n\n-`AllNonDebug` (default)");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("radius"), script_mask_num},
          {string_lit("layers"), script_mask_str | script_mask_null, ScriptSigArgFlags_Multi},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("query_box");
      const String       doc    = string_lit("Find all the entities that are touching the given box.\n\n*Note*: Returns a query handle.\n\nSupported layers:\n\n-`Environment`\n\n-`Destructible`\n\n-`Infantry`\n\n-`Structure`\n\n-`Unit`\n\n-`Debug`\n\n-`AllIncludingDebug`\n\n-`AllNonDebug` (default)");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("size"), script_mask_vec3},
          {string_lit("rot"), script_mask_quat | script_mask_null},
          {string_lit("layers"), script_mask_str | script_mask_null, ScriptSigArgFlags_Multi},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("query_pop");
      const String       doc    = string_lit("Pops the first query value, returns null when reaching the end of the query.");
      const ScriptMask   ret    = script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("query"), script_mask_num},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("query_random");
      const String       doc    = string_lit("Return a random remaining value in the given query, returns null when the current query is empty.");
      const ScriptMask   ret    = script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("query"), script_mask_num},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("nav_find");
      const String       doc    = string_lit("Find a navigation position.\n\nSupported types:\n\n-`ClosestCell` (default)\n\n-`UnblockedCell`\n\n-`FreeCell`");
      const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("type"), script_mask_str | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("nav_target");
      const String       doc    = string_lit("Lookup the current navigation target of the given entity. Either a position or an entity.");
      const ScriptMask   ret    = script_mask_vec3 | script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("line_of_sight");
      const String       doc    = string_lit("Test if there is a clear line of sight between the given entities.");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("src"), script_mask_entity},
          {string_lit("dst"), script_mask_entity},
          {string_lit("radius"), script_mask_num | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("capable");
      const String       doc    = string_lit("Test if the given entity has a specific capability.\n\nSupported capabilities:\n\n-`NavTravel`\n\n-`Attack`\n\n-`Status`");
      const ScriptMask   ret    = script_mask_bool | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("capability"), script_mask_str},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("active");
      const String       doc    = string_lit("Test if the given entity is performing an activity.\n\nSupported activities:\n\n-`Moving`\n\n-`Traveling`\n\n-`Attacking`\n\n-`Firing`");
      const ScriptMask   ret    = script_mask_bool | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("activity"), script_mask_str},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("target_primary");
      const String       doc    = string_lit("Lookup the primary target of the given entity.");
      const ScriptMask   ret    = script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("target_range_min");
      const String       doc    = string_lit("Lookup the minimum targeting range of the given entity.");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("target_range_max");
      const String       doc    = string_lit("Lookup the maximum targeting range of the given entity.");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("tell");
      const String       doc    = string_lit("Set a knowledge value for the given entity.\n\n*Note*: The updated knowledge is visible to scripts in the next frame.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("target"), script_mask_entity},
          {string_lit("key"), script_mask_str},
          {string_lit("value"), script_mask_any},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("ask");
      const String       doc    = string_lit("Ask a target entity for a knowledge value.\n\n*Note*: The result value is visible to this entity under the same key in the next frame.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("this"), script_mask_entity},
          {string_lit("target"), script_mask_entity},
          {string_lit("key"), script_mask_str},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("spawn");
      const String       doc    = string_lit("Spawn a prefab.");
      const ScriptMask   ret    = script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("prefabId"), script_mask_str},
          {string_lit("pos"), script_mask_vec3 | script_mask_null},
          {string_lit("rot"), script_mask_quat | script_mask_null},
          {string_lit("scale"), script_mask_num | script_mask_null},
          {string_lit("faction"), script_mask_str | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("destroy");
      const String       doc    = string_lit("Destroy the given entity.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("destroy_after");
      const String       doc    = string_lit("Destroy the given entity after a delay.\n\n*Note*: When providing an entity it will wait until the entity no longer exists.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("delay"), script_mask_entity | script_mask_time},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("teleport");
      const String       doc    = string_lit("Teleport the given entity.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("pos"), script_mask_vec3 | script_mask_null},
          {string_lit("rot"), script_mask_quat | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("nav_travel");
      const String       doc    = string_lit("Instruct the given entity to travel to a target location or entity.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("target"), script_mask_entity | script_mask_vec3},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("nav_stop");
      const String       doc    = string_lit("Instruct the given entity to stop traveling.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("attach");
      const String       doc    = string_lit("Attach the given entity to another entity (optionally at a specific joint).");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("target"), script_mask_entity},
          {string_lit("jointName"), script_mask_str | script_mask_null},
          {string_lit("offset"), script_mask_vec3 | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("detach");
      const String       doc    = string_lit("Detach the given entity from all other entities.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("damage");
      const String       doc    = string_lit("Deal damage to the given entity.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("amount"), script_mask_num},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("status");
      const String       doc    = string_lit("Change or query if an entity is affected by the specified status.\n\nSupported parameters:\n\n-`Burning`");
      const ScriptMask   ret    = script_mask_bool | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("type"), script_mask_str},
          {string_lit("enable"), script_mask_bool | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("attack");
      const String       doc    = string_lit("Instruct the given entity to attack another entity.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("target"), script_mask_entity | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("bark");
      const String       doc    = string_lit("Request a bark to be played.\n\nSupported types:\n\n-`Death`\n\n-`Confirm`");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("type"), script_mask_str},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("rend_param");
      const String       doc    = string_lit("Change or query a renderable parameter on the given entity.\n\nSupported parameters:\n\n-`Color`\n\n-`Alpha`\n\n-`Emissive`");
      const ScriptMask   ret    = script_mask_bool | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("param"), script_mask_str},
          {string_lit("value"), script_mask_num | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("vfx_system");
      const String       doc    = string_lit("Place a vfx system.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'vfx_param()' api.");
      const ScriptMask   ret    = script_mask_entity;
      const ScriptSigArg args[] = {
          {string_lit("asset"), script_mask_entity},
          {string_lit("pos"), script_mask_vec3},
          {string_lit("rot"), script_mask_quat},
          {string_lit("alpha"), script_mask_num | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("vfx_decal");
      const String       doc    = string_lit("Place a decal.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'vfx_param()' api.");
      const ScriptMask   ret    = script_mask_entity;
      const ScriptSigArg args[] = {
          {string_lit("asset"), script_mask_entity},
          {string_lit("pos"), script_mask_vec3},
          {string_lit("rot"), script_mask_quat},
          {string_lit("alpha"), script_mask_num | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("vfx_param");
      const String       doc    = string_lit("Change or query a vfx parameter on the given entity.\n\nSupported parameters:\n\n-`Alpha`");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("param"), script_mask_str},
          {string_lit("value"), script_mask_num | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("collision_box");
      const String       doc    = string_lit("Create a collision box.\n\n*Note*: Resulting entity is not automatically destroyed.");
      const ScriptMask   ret    = script_mask_entity;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("size"), script_mask_vec3},
          {string_lit("rot"), script_mask_quat | script_mask_null},
          {string_lit("layer"), script_mask_str | script_mask_null},
          {string_lit("navBlocker"), script_mask_bool  | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("light_point");
      const String       doc    = string_lit("Place a point light.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'light_param()' api.");
      const ScriptMask   ret    = script_mask_entity;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("radiance"), script_mask_color},
          {string_lit("radius"), script_mask_num},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("light_param");
      const String       doc    = string_lit("Change or query a light parameter on the given entity.\n\nSupported parameters:\n\n-`Radiance`");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("param"), script_mask_str},
          {string_lit("value"), script_mask_color | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("sound_play");
      const String       doc    = string_lit("Start playing a sound.\n\n*Note*: Resulting entity is not automatically destroyed.\n\n*Note*: It takes one frame before it can be used with the 'sound_param()' api.");
      const ScriptMask   ret    = script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("asset"), script_mask_entity},
          {string_lit("pos"), script_mask_vec3 | script_mask_null},
          {string_lit("gain"), script_mask_num | script_mask_null},
          {string_lit("pitch"), script_mask_num | script_mask_null},
          {string_lit("looping"), script_mask_bool | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("sound_param");
      const String       doc    = string_lit("Change or query a sound parameter on the given entity.\n\nSupported parameters:\n\n-`Gain`\n\n-`Pitch`");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("param"), script_mask_str},
          {string_lit("value"), script_mask_num | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("anim_param");
      const String       doc    = string_lit("Change or query an animation parameter on the given entity.\n\nSupported parameters:\n\n-`Time`\n\n-`Speed`\n\n-`Weight`");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("layer"), script_mask_str},
          {string_lit("param"), script_mask_str},
          {string_lit("value"), script_mask_num | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("random_of");
      const String       doc    = string_lit("Return a random (non-null) value from the given arguments.");
      const ScriptMask   ret    = script_mask_any;
      const ScriptSigArg args[] = {
          {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_log");
      const String       doc    = string_lit("Log the given values.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_line");
      const String       doc    = string_lit("Draw a 3D debug line between the two given points.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("start"), script_mask_vec3},
          {string_lit("end"), script_mask_vec3},
          {string_lit("color"), script_mask_color | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_sphere");
      const String       doc    = string_lit("Draw a 3D debug sphere.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("radius"), script_mask_num | script_mask_null},
          {string_lit("color"), script_mask_color | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_box");
      const String       doc    = string_lit("Draw a 3D debug box.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("size"), script_mask_vec3},
          {string_lit("rot"), script_mask_quat | script_mask_null},
          {string_lit("color"), script_mask_color | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_arrow");
      const String       doc    = string_lit("Draw a 3D debug arrow.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("start"), script_mask_vec3},
          {string_lit("end"), script_mask_vec3},
          {string_lit("radius"), script_mask_num | script_mask_null},
          {string_lit("color"), script_mask_color | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_orientation");
      const String       doc    = string_lit("Draw a 3D orientation gizmos.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("rot"), script_mask_quat},
          {string_lit("size"), script_mask_num | script_mask_null},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_text");
      const String       doc    = string_lit("Draw debug text at a position in 3D space.\n\n*Note*: Size is in UI canvas pixels.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("color"), script_mask_color},
          {string_lit("size"), script_mask_num},
          {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_trace");
      const String       doc    = string_lit("Emit a debug-trace for this entity with the given values.");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
      };
      bind(binder, name, doc, ret, args, array_elems(args));
    }
    {
      const String     name = string_lit("debug_break");
      const String     doc  = string_lit("Break into the debugger if there is one attached.");
      const ScriptMask ret  = script_mask_null;
      bind(binder, name, doc, ret, null, 0);
    }
    // clang-format on

    script_binder_finalize(binder);
    g_scriptBinder = binder;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetScriptComp);

static void ecs_destruct_script_comp(void* data) {
  AssetScriptComp* comp = data;
  string_maybe_free(g_alloc_heap, comp->sourceText);
  script_destroy((ScriptDoc*)comp->doc);
}

ecs_view_define(ScriptUnloadView) {
  ecs_access_with(AssetScriptComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any script-asset component for unloaded assets.
 */
ecs_system_define(ScriptUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, ScriptUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetScriptComp);
  }
}

ecs_module_init(asset_script_module) {
  asset_binder_init();

  ecs_register_comp(AssetScriptComp, .destructor = ecs_destruct_script_comp);

  ecs_register_view(ScriptUnloadView);

  ecs_register_system(ScriptUnloadAssetSys, ecs_view_id(ScriptUnloadView));
}

void asset_load_script(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  Allocator* tempAlloc = alloc_bump_create_stack(2 * usize_kibibyte);

  ScriptDoc*     doc      = script_create(g_alloc_heap);
  ScriptDiagBag* diags    = script_diag_bag_create(tempAlloc, ScriptDiagFilter_Error);
  ScriptSymBag*  symsNull = null;

  const ScriptExpr expr = script_read(doc, g_scriptBinder, src->data, diags, symsNull);

  const u32 diagCount = script_diag_count(diags, ScriptDiagFilter_All);
  for (u32 i = 0; i != diagCount; ++i) {
    const ScriptDiag* diag = script_diag_data(diags) + i;
    const String      msg  = script_diag_pretty_scratch(src->data, diag);
    log_e("Script load error", log_param("error", fmt_text(msg)));
  }

  script_diag_bag_destroy(diags);

  if (UNLIKELY(sentinel_check(expr) || diagCount > 0)) {
    goto Error;
  }

  ecs_world_add_t(
      world,
      entity,
      AssetScriptComp,
      .sourceText = string_maybe_dup(g_alloc_heap, src->data),
      .doc        = doc,
      .expr       = expr);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  script_destroy(doc);

Cleanup:
  asset_repo_source_close(src);
}

void asset_script_binder_write(DynString* str) {
  asset_binder_init();
  script_binder_write(str, g_scriptBinder);
}
