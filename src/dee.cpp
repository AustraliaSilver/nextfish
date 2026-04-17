#include "dee.h"
#include "position.h"
#include "bitboard.h"
#include <algorithm>

namespace Stockfish {
namespace DEE {

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
            score += (KING - pt); 
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

int Evaluator::tension_score(const Position& pos) {
    Bitboard usAttacks, themAttacks;
    compute_both_attack_maps(pos, usAttacks, themAttacks);

    Bitboard contested = usAttacks & themAttacks & pos.pieces();
    int score = 0;

    while (contested) {
        Square sq = pop_lsb(contested);
        Piece pc = pos.piece_on(sq);
        score += int(PieceValue[pc]) / 100;
    }

    return score;
}

void Evaluator::compute_both_attack_maps(const Position& pos, Bitboard& us, Bitboard& them) {
    auto compute = [&](Color c) {
        Bitboard b = pos.attacks_by<PAWN>(c) | pos.attacks_by<KNIGHT>(c) |
                     pos.attacks_by<KING>(c);
        
        Bitboard occ = pos.pieces();
        Bitboard bishops = pos.pieces(c, BISHOP) | pos.pieces(c, QUEEN);
        while (bishops) b |= attacks_bb<BISHOP>(pop_lsb(bishops), occ);
        
        Bitboard rooks = pos.pieces(c, ROOK) | pos.pieces(c, QUEEN);
        while (rooks) b |= attacks_bb<ROOK>(pop_lsb(rooks), occ);
        
        return b;
    };

    us = compute(pos.side_to_move());
    them = compute(~pos.side_to_move());
}

} // namespace DEE
} // namespace Stockfish
