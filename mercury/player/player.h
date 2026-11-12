/*
 * player.h
 *
 * The generic game executable (mercury_player). It runs any project:
 *
 *   mercury_player [project.mproj] [--map res://maps/other.mmap]
 *                  [--dump-schema file] [--build-scripts]
 *
 * Without a project it runs the shipped game packs next to the executable.
 * Loose projects have their AngelScript compiled in-process before they run.
 * --dump-schema writes the fields of every script and exits (the editor runs
 * this after each build so its Inspector knows the scripts); --build-scripts
 * only compiles and exits.
 */
#ifndef MERCURY_PLAYER_PLAYER_H
#define MERCURY_PLAYER_PLAYER_H

namespace Mercury {

int RunPlayer(int argc, char** argv, const char* default_project = nullptr);

}  // namespace Mercury

#endif  // MERCURY_PLAYER_PLAYER_H
