// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * main.cc
 *
 * mercury_player: runs any Mercury project (see player.h).
 */
#include "player/player.h"

int main(int argc, char** argv) {
  return Mercury::RunPlayer(argc, argv, nullptr);
}
