/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <memory>
#include <cstdlib>

#include "bitboard.h"
#include "misc.h"
#include "position.h"
#include "tune.h"
#include "uci.h"

using namespace Stockfish;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((force_align_arg_pointer))
#endif
int main(int argc, char* argv[]) {
    std::cout << "DEBUG: main starting" << std::endl;
    std::cout << engine_info() << std::endl;

    std::cout << "DEBUG: Initializing Bitboards" << std::endl;
    Bitboards::init();
    std::cout << "DEBUG: Bitboards initialized" << std::endl;

    std::cout << "DEBUG: Initializing Position" << std::endl;
    Position::init();
    std::cout << "DEBUG: Position initialized" << std::endl;

    std::cout << "DEBUG: Creating UCIEngine" << std::endl;
    auto uci = std::make_unique<UCIEngine>(argc, argv);
    std::cout << "DEBUG: UCIEngine created" << std::endl;

    std::cout << "DEBUG: Initializing Tune" << std::endl;
    Tune::init(uci->engine_options());
    std::cout << "DEBUG: Tune initialized" << std::endl;

    std::cout << "DEBUG: Entering uci->loop()" << std::endl;
    uci->loop();
    std::cout << "DEBUG: Exited uci->loop()" << std::endl;

    std::_Exit(0);
}
