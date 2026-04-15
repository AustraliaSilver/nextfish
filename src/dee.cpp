#include "dee.h"
#include "position.h"
#include "bitboard.h"
#include <algorithm>

namespace Stockfish {
namespace DEE {

// DEE V21: Enhanced Positional Aftermath.
// Integrating Bishop Pair and King Ring tactical awareness into exchange evaluation.

Value Evaluator::adjusted_see(const Position& pos, Move m) {
    if (!m) return VALUE_ZERO;

    Square to = m.to_sq();
    Color us = pos.side_to_move();
    
    // Neutral exchanges tie-breaker
    if (pos.see_ge(m, 50) || !pos.see_ge(m, -50))
        return VALUE_ZERO;

    Bitboard attackers = pos.attackers_to(to, pos.pieces() ^ m.from_sq());
    int ourAtt = popcount(attackers & pos.pieces(us));
    int theirAtt = popcount(attackers & pos.pieces(~us));
    
    int res = 0;
    if (ourAtt > theirAtt)
        res = (ourAtt - theirAtt) * 15 + ourAtt * 5;

    // --- Module 3.2: Positional Aftermath Evaluator (PAE) ---
    
    // 1. Breaking Bishop Pair bonus
    Piece captured = pos.piece_on(to);
    if (type_of(captured) == BISHOP && popcount(pos.pieces(~us, BISHOP)) > 1)
        res += 15;

    // 2. King Ring capture bonus (capturing near enemy king)
    Square enemyKing = pos.square<KING>(~us);
    if (attacks_bb<KING>(enemyKing) & square_bb(to))
        res += 10;

    return Value(res);
}

int Evaluator::tension_score(const Position& pos) {
    // --- Module 3.1: Global Contested Square Detection ---
    Bitboard usAttacks = pos.attacks_by<PAWN>(WHITE) | pos.attacks_by<KNIGHT>(WHITE) |
                         pos.attacks_by<BISHOP>(WHITE) | pos.attacks_by<ROOK>(WHITE) |
                         pos.attacks_by<QUEEN>(WHITE) | pos.attacks_by<KING>(WHITE);

    Bitboard themAttacks = pos.attacks_by<PAWN>(BLACK) | pos.attacks_by<KNIGHT>(BLACK) |
                           pos.attacks_by<BISHOP>(BLACK) | pos.attacks_by<ROOK>(BLACK) |
                           pos.attacks_by<QUEEN>(BLACK) | pos.attacks_by<KING>(BLACK);

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
    Color usColor = pos.side_to_move();
    us = pos.attacks_by<PAWN>(usColor) | pos.attacks_by<KNIGHT>(usColor) |
         pos.attacks_by<BISHOP>(usColor) | pos.attacks_by<ROOK>(usColor) |
         pos.attacks_by<QUEEN>(usColor) | pos.attacks_by<KING>(usColor);

    them = pos.attacks_by<PAWN>(~usColor) | pos.attacks_by<KNIGHT>(~usColor) |
           pos.attacks_by<BISHOP>(~usColor) | pos.attacks_by<ROOK>(~usColor) |
           pos.attacks_by<QUEEN>(~usColor) | pos.attacks_by<KING>(~usColor);
}

} // namespace DEE
} // namespace Stockfish
