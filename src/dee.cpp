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

#include "dee.h"
#include "position.h"
#include "bitboard.h"
#include <algorithm>

namespace Stockfish {
namespace DEE {

// Relative value of a piece that defends the target square, in pawns.
// A stronger defender makes a capture on that square more risky, so the
// pressure score has to grow with the value of the defending piece.
constexpr int PressureValue[PIECE_TYPE_NB] = {0, 1, 3, 3, 5, 9, 0, 0};

// DEE V44: Aggressive Tactical Pruning Core.
// Refining negative imbalance detection to enable more effective QS pruning.

Value Evaluator::adjusted_see(const Position& pos, Move m) {
    if (!m) return VALUE_ZERO;

    Square to = m.to_sq();
    Color us = pos.side_to_move();

    if (pos.see_ge(m, 50) || !pos.see_ge(m, -50))
        return VALUE_ZERO;

    Bitboard occupancy = pos.pieces() ^ m.from_sq();
    Bitboard attackers = pos.attackers_to(to, occupancy);

    auto get_pressure_score = [&](Color c) {
        int score = 0;
        Bitboard b = attackers & pos.pieces(c);
        while (b) {
            PieceType pt = type_of(pos.piece_on(pop_lsb(b)));
            score += PressureValue[pt];
        }
        return score;
    };

    int ourP = get_pressure_score(us);
    int theirP = get_pressure_score(~us);

    if (ourP > theirP)
        return Value((ourP - theirP) * 12 + popcount(attackers & pos.pieces(us)) * 4);

    if (theirP > ourP)
        return Value(-((theirP - ourP) * 10)); // Increased penalty for defender disadvantage

    return VALUE_ZERO;
}

bool Evaluator::should_prune_in_qs(const Position& pos, Move m, Value adjustedSee) {
    // V16: Lowered threshold for more aggressive pruning
    if (adjustedSee < -15) {
         if (!pos.see_ge(m, 0))
             return true;
    }
    return false;
}

} // namespace DEE
} // namespace Stockfish
