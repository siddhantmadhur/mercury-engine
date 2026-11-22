// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * player.h
 *
 * The generic game executable (mercury_player). It runs any project:
 *
 *   mercury_player [project.mproj] [--map res://maps/other.mmap]
 *                  [--dump-schema file] [--build-scripts]
 *                  [--dump-engine-schema [file|-]] [--validate [--json]]
 *                  [--inspect-asset res://asset] [--headless] [--frames N]
 *
 * Without a project it runs the shipped game packs next to the executable.
 * Loose projects have their AngelScript compiled in-process before they run.
 * --dump-schema writes the fields of every script and exits (the editor runs
 * this after each build so its Inspector knows the scripts); --build-scripts
 * only compiles and exits. --dump-engine-schema writes the engine's own
 * component types as JSON (the MCP server and other tooling read it);
 * --validate reports project problems; --inspect-asset prints one asset as
 * JSON. --headless renders offscreen without a window, and --frames N steps
 * exactly N frames and exits 0, which lets tests and automation run a game
 * headlessly.
 */
#ifndef MERCURY_PLAYER_PLAYER_H
#define MERCURY_PLAYER_PLAYER_H

#include <string>

namespace Mercury {

/**
 * @brief The command line split into what the player itself understands
 *
 * Options a game registered an app hook for are left in `argv` and are not
 * recorded here; see @ref ParsePlayerArgs.
 */
struct PlayerArgs {
  std::string project_path;
  std::string map_path;
  std::string schema_path;
  // --dump-engine-schema: where to write the engine's component types as
  // JSON, "-" for stdout (also when the option carries no value). Empty when
  // the option was not given.
  std::string engine_schema_path;
  // --inspect-asset: the res:// path to describe as JSON. Empty when the
  // option was not given.
  std::string inspect_asset;
  // --frames N: step N frames and exit 0; 0 runs the normal frame loop.
  int frames = 0;
  bool project_given = false;
  bool build_scripts = false;
  bool validate = false;
  // --json, alongside --validate.
  bool json = false;
  bool headless = false;
  bool help = false;
  // The first unrecognised option, when `allow_unknown` is false. Empty when
  // the command line is valid.
  std::string unknown_option;
  // A known option whose value is wrong, e.g. `--frames x`. Empty when the
  // command line is valid.
  std::string option_error;
};

/**
 * @brief Parses the player's own options
 *
 * @param allow_unknown Accept `--options` the player does not know, so an app
 *   hook can read them from the original `argv`. A token after an unknown
 *   option is treated as that option's value, not as the project path, so put
 *   the project path before any game-specific options.
 * @return the parsed options; `unknown_option` is set when the command line has
 *   an option the player does not know and `allow_unknown` is false.
 */
PlayerArgs ParsePlayerArgs(int argc, char** argv, bool allow_unknown);

/**
 * @brief Run a Mercury project from a game executable
 *
 * @details
 * This is the implementation behind `mercury_player` and the editor's Play
 * button. It loads the `.mproj`, initialises the AngelScript runtime, compiles
 * every `.as` file under `scripts/`, creates the @ref Mercury::MapScene and
 * runs it with a `Mercury::DefaultApplication`. When the project path is empty
 * it looks for `game.mpack` next to the executable, so a shipped build is
 * self-contained.
 *
 * The command line is:
 * @code{.text}
 * mercury_player [project.mproj] [--map res://maps/other.mmap]
 *                [--dump-schema file] [--build-scripts]
 *                [--dump-engine-schema [file|-]] [--validate [--json]]
 *                [--inspect-asset res://asset] [--headless] [--frames N]
 * @endcode
 *
 * - `--map` starts at another map of the project.
 * - `--build-scripts` compiles the scripts, then exits.
 * - `--dump-schema` writes the fields of every script and exits (the editor
 *   uses it to build the Inspector).
 * - `--dump-engine-schema` writes the engine's own component types as JSON
 *   and exits; no project is needed. `-`, no value or a following option
 *   means stdout.
 * - `--validate` reports project problems as text (or JSON with `--json`)
 *   and exits 1 when there are errors; `--json` without `--validate` is
 *   rejected.
 * - `--inspect-asset` prints one `res://` asset as JSON and exits.
 * - `--headless` renders offscreen without a window.
 * - `--frames N` steps exactly N frames and exits 0; tests and automation
 *   combine it with `--headless`.
 * - Any other `--option` is passed to the game's app hooks (MERCURY_APP_HOOK).
 *   When no hook is registered it is an error, as before.
 *
 * @param argc Argument count from `main`.
 * @param argv Argument vector from `main`.
 * @param default_project Project to run when none is given, or nullptr.
 * @return 0 on success, 1 for a project/build/runtime/validation error, or 2
 * for a bad command line.
 *
 * @par Example
 * A dedicated executable for one game:
 * @code{.cpp}
 * #include "player/player.h"
 *
 * int main(int argc, char** argv) {
 *   return Mercury::RunPlayer(argc, argv, "my_game/my_game.mproj");
 * }
 * @endcode
 *
 * @see Mercury::Project, Mercury::MapScene, Mercury::Behaviour
 */
int RunPlayer(int argc, char** argv, const char* default_project = nullptr);

}  // namespace Mercury

#endif  // MERCURY_PLAYER_PLAYER_H
