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

#ifndef HARENN_CTRL_H_INCLUDED
#define HARENN_CTRL_H_INCLUDED

#include "types.h"
#include "position.h"
#include "harenn.h"

#include <string>

namespace Stockfish {

class OptionsMap;

namespace HARENN {

class Controller {
public:
    static void init(const std::string& binaryDirectory);

    // Load tunable parameters from UCI options (call at start of search)
    static void refresh_params(const OptionsMap& options);

    // Neural analysis of a position (Tau, Rho, Rs, Eval)
    static EvalResult get_analysis(const Position& pos, NumaReplicatedAccessToken numaToken);

    // AI-based selective depth extension. The result of the neural analysis
    // is passed in so a single query can be shared by every candidate move
    // of a node.
    static int get_search_extension(const EvalResult& res, Color sideToMove);

    // Time management
    static int get_time_multiplier(const Position& pos);
};

} // namespace HARENN

} // namespace Stockfish

#endif // HARENN_CTRL_H_INCLUDED
