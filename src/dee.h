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

#ifndef DEE_H_INCLUDED
#define DEE_H_INCLUDED

#include "types.h"
#include "position.h"

namespace Stockfish {

namespace DEE {

class Evaluator {
public:
    // Tie-breaker used in MovePicker for capture ordering
    static Value adjusted_see(const Position& pos, Move m);

    // QS Pruning Logic (V15)
    static bool should_prune_in_qs(const Position& pos, Move m, Value adjustedSee);
};

}  // namespace DEE

}  // namespace Stockfish

#endif  // DEE_H_INCLUDED
