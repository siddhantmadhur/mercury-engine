/*
 * player.h
 *
 * Entry point of a game built with mercury_add_game(). The generated main()
 * just calls RunPlayer:
 *
 *   game [project.mproj] [--map res://maps/other.mmap] [--dump-schema file]
 *
 * Without arguments the project the game was built from is used.
 * --dump-schema writes the fields of every compiled-in script and exits; the
 * editor runs this after each build so its Inspector knows the scripts.
 */
#ifndef MERCURY_PLAYER_PLAYER_H
#define MERCURY_PLAYER_PLAYER_H

namespace Mercury {

int RunPlayer(int argc, char** argv, const char* default_project = nullptr);

}  // namespace Mercury

#endif  // MERCURY_PLAYER_PLAYER_H
