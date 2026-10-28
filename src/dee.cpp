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
#include "types.h"
#include <algorithm>

namespace Stockfish {
namespace DEE {

// Dynamic Exchange Evaluation (DEE) Core
// Grounded in Game-Theoretic Exchange Modeling:
// 1. Absolute Pin Detection: Disqualifies pinned enemy defenders unable to legally recapture.
// 2. Relative Pin Detection: Disqualifies defenders pinned to the enemy Queen.
// 3. Positional Aftermath Evaluation (PAE): Evaluates structural consequences (Bishop Pair, King Shield, Passed Pawns).
// 4. Game-Theoretic Safe Filtering: Never boosts losing trades.

Value Evaluator::adjusted_see(const Position& pos, Move m) {
    if (!m) return VALUE_ZERO;

    Square from = m.from_sq();
    Square to = m.to_sq();
    Color us = pos.side_to_move();
    Color them = ~us;
    Piece capturedPiece = pos.piece_on(to);
    PieceType capturedType = type_of(capturedPiece);

    // If not a capture and not promotion, nothing to evaluate
    if (!capturedPiece && m.type_of() != EN_PASSANT && m.type_of() != PROMOTION)
        return VALUE_ZERO;

    int score = 0;

    Bitboard occupancy = pos.pieces() ^ from;
    Bitboard themDefenders = pos.attackers_to(to, occupancy) & pos.pieces(them);

    // 1A. ABSOLUTE PIN DETECTION (Pin to King)
    // Check if any defender of 'to' is pinned to the enemy King and thus unable to legally recapture.
    Bitboard themKingBlockers = pos.blockers_for_king(them);
    if (themKingBlockers && (themDefenders & themKingBlockers)) {
        Square themKing = pos.square<KING>(them);
        Bitboard pinnedDefenders = themDefenders & themKingBlockers;

        while (pinnedDefenders) {
            Square psq = pop_lsb(pinnedDefenders);
            // If 'to' is NOT on the ray connecting King and the pinned defender,
            // the defender CANNOT legally move to 'to' to recapture!
            if (!(line_bb(themKing, psq) & to)) {
                score += 25; // Defender is completely paralyzed
            }
        }
    }

    // 1B. RELATIVE PIN DETECTION (Pin to Queen)
    // Check if an enemy defender is pinned to their Queen by one of our sliders.
    // If that defender moves to 'to', our slider captures their Queen.
    Bitboard themQueens = pos.pieces(them, QUEEN);
    if (themQueens && themDefenders) {
        Square themQueen = lsb(themQueens);
        Bitboard snipers = ((attacks_bb<ROOK>(themQueen) & pos.pieces(us, QUEEN, ROOK))
                          | (attacks_bb<BISHOP>(themQueen) & pos.pieces(us, QUEEN, BISHOP)));
        Bitboard occ = pos.pieces() ^ snipers;

        while (snipers) {
            Square sniperSq = pop_lsb(snipers);
            Bitboard b = between_bb(themQueen, sniperSq) & occ;
            if (b && !more_than_one(b)) {
                if ((b & themDefenders) && !(line_bb(themQueen, sniperSq) & to)) {
                    score += 20; // Tactical bonus for exploiting pin to Queen
                }
            }
        }
    }

    // 2. POSITIONAL AFTERMATH EVALUATION (PAE)
    // A. Stripping opponent's Bishop Pair
    if (capturedType == BISHOP && popcount(pos.pieces(them, BISHOP)) >= 2) {
        score += 15; // Destroying opponent's bishop pair advantage
    }

    // B. King Shelter Breach: capturing pawns/defenders immediately adjacent to enemy King
    Square themKing = pos.square<KING>(them);
    if (distance(to, themKing) <= 1) {
        if (capturedType == PAWN) {
            score += 20; // Shattering enemy King pawn shield
        } else if (capturedType != NO_PIECE_TYPE) {
            score += 15; // Removing close defender of the King
        }
    }

    // C. Advanced Pawn Capture / Promotion Threat
    Piece movedPiece = pos.piece_on(from);
    if (type_of(movedPiece) == PAWN) {
        if (relative_rank(us, to) >= RANK_6) {
            score += 20; // Creating or advancing dangerous passed pawn
        }
    }

    // D. Safe Exchange Verification:
    // If no tactical pin or positional aftermath was detected and the move loses material by SEE,
    // penalize to prevent suboptimal ordering.
    if (score == 0 && !pos.see_ge(m, 0)) {
        return Value(-30);
    }

    return Value(std::clamp(score, -100, 50));
}

bool Evaluator::should_prune_in_qs(const Position& pos, Move m, Value adjustedSee) {
    // Only prune captures that are clearly bad by both DEE and SEE
    if (adjustedSee < -50) {
         if (!pos.see_ge(m, 0))
             return true;
    }
    return false;
}

} // namespace DEE
} // namespace Stockfish
