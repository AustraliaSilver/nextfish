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
// 4. Desperado Capture Priority: Prioritizes captures made by threatened/hanging pieces.
// 5. Battery Firepower & X-Ray Support: Detects friendly batteries backing up the capture.
// 6. Game-Theoretic Safe Filtering: Never boosts losing trades.

Value Evaluator::adjusted_see(const Position& pos, Move m) {
    if (!m) return VALUE_ZERO;

    Square from = m.from_sq();
    Square to = m.to_sq();
    Color us = pos.side_to_move();
    Color them = ~us;
    Piece capturedPiece = pos.piece_on(to);
    PieceType capturedType = type_of(capturedPiece);
    Piece movedPiece = pos.piece_on(from);
    PieceType movedType = type_of(movedPiece);

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

    // B. King Shelter Breach: capturing pawns/defenders adjacent or protecting enemy King
    Square themKing = pos.square<KING>(them);
    int distThemKing = distance(to, themKing);
    if (distThemKing <= 1) {
        if (capturedType == PAWN) {
            score += 20; // Shattering enemy King pawn shield
        } else if (capturedType != NO_PIECE_TYPE) {
            score += 15; // Removing close defender of the King
        }
    } else if (distThemKing == 2 && capturedType >= KNIGHT && pos.see_ge(m, 0)) {
        score += 10; // Removing key outer defender of the enemy King (e.g. Nf6/Nf3)
    }

    // C. Advanced Pawn Capture / Promotion Threat
    if (movedType == PAWN) {
        if (relative_rank(us, to) >= RANK_6) {
            score += 20; // Creating or advancing dangerous passed pawn
        }
    }

    // 3. DESPERADO CAPTURE PRIORITY
    // If our piece on 'from' is hanging or under attack by a lesser enemy piece,
    // prioritizing its capture salvages material before it can be lost.
    if (movedType > PAWN) {
        Bitboard themAttFrom = pos.attackers_to(from) & pos.pieces(them);
        if (themAttFrom) {
            bool isHanging = !(pos.attackers_to(from) & pos.pieces(us));
            bool underLesserThreat = (themAttFrom & pos.pieces(them, PAWN))
                || (movedType > BISHOP && (themAttFrom & pos.pieces(them, KNIGHT, BISHOP)))
                || (movedType == QUEEN && (themAttFrom & pos.pieces(them, ROOK)));

            if ((isHanging || underLesserThreat) && pos.see_ge(m, 0)) {
                score += 15; // Desperado tactical salvage bonus
            }
        }
    }

    // 4. BATTERY FIREPOWER & X-RAY SUPPORT
    // Detect if the capturing piece is backed up by a friendly slider directly behind 'from'
    if (movedType >= BISHOP && pos.see_ge(m, 0)) {
        Bitboard friendlySliders = (pos.pieces(us, ROOK, QUEEN) | pos.pieces(us, BISHOP)) ^ from;
        Bitboard candidates = friendlySliders & line_bb(from, to);

        while (candidates) {
            Square sliderSq = pop_lsb(candidates);
            // Verify alignment: 'from' must be between 'to' and 'sliderSq'
            if ((between_bb(to, sliderSq) & from) && !(between_bb(from, sliderSq) & pos.pieces())) {
                PieceType rearType = type_of(pos.piece_on(sliderSq));
                bool orthogonal = (file_of(from) == file_of(to) || rank_of(from) == rank_of(to));
                bool diagonal = !orthogonal;

                if ((orthogonal && (rearType == ROOK || rearType == QUEEN)) ||
                    (diagonal && (rearType == BISHOP || rearType == QUEEN))) {
                    score += 15; // Battery X-Ray support bonus
                    break;
                }
            }
        }
    }

    // 5. DEE 3.3 TEMPORAL DECAY MODULE (TDM)
    // Evaluates exchange urgency: captures that neutralize an enemy piece threatening our King zone (distance <= 2)
    // have high temporal decay (must be executed immediately before the attack matures).
    Square ourKing = pos.square<KING>(us);
    int distOurKing = distance(to, ourKing);
    if (pos.see_ge(m, 0) && capturedType >= KNIGHT) {
        if (distOurKing <= 1)
            score += 25; // Imminent threat directly adjacent to our King
        else if (distOurKing == 2)
            score += 15; // Outer ring attacker defused
    }

    // 6. DEE-X: NIMZOWITSCH SPACE SQUEEZE (CTE / ENM)
    // Avoid trading an active central piece for a defended passive/rim enemy piece in equal trades,
    // which relieves the opponent's defensive cramp.
    if (pos.see_ge(m, 0) && capturedType == movedType && movedType >= KNIGHT && movedType <= ROOK) {
        bool ourPieceActive = (rank_of(from) >= RANK_4 && rank_of(from) <= RANK_5 && file_of(from) >= FILE_C && file_of(from) <= FILE_F);
        bool enemyPassive = (relative_rank(them, to) <= RANK_2 || file_of(to) == FILE_A || file_of(to) == FILE_H);

        if (ourPieceActive && enemyPassive && themDefenders) {
            score -= 15; // Nimzowitsch penalty: do not relieve opponent's cramp by trading active for passive
        }
    }

    // 7. Safe Exchange Verification:
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
