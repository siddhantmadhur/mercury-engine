/*
 * player.h
 *
 * The generic game executable (mercury_player). It runs any project:
 *
 *   mercury_player [project.mproj] [--map res://maps/other.mmap]
 *                  [--dump-schema file] [--write-types] [--build-scripts]
 *
 * Without a project it runs the shipped game packs next to the executable.
 * Loose projects have their TypeScript built first when it is out of date.
 * --dump-schema writes the fields of every script in the bundle and exits (the editor runs this
 * after each build so its Inspector knows the scripts). --write-types writes the types an editor
 * needs (.mercury/types and tsconfig.json) and exits; --build-scripts only builds.
 */
#ifndef MERCURY_PLAYER_PLAYER_H
#define MERCURY_PLAYER_PLAYER_H

namespace Mercury {

int RunPlayer(int argc, char** argv, const char* default_project = nullptr);

}  // namespace Mercury

#endif  // MERCURY_PLAYER_PLAYER_H
