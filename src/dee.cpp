#include "dee.h"
#include "position.h"
#include <algorithm>

namespace Stockfish {
namespace DEE {

bool Evaluator::has_tension(const Position& pos) {
    Bitboard attackedByWhite = 0, attackedByBlack = 0;
    for (Square s = SQ_A1; s <= SQ_H8; ++s) {
        if (pos.attackers_to(s, WHITE)) attackedByWhite |= s;
        if (pos.attackers_to(s, BLACK)) attackedByBlack |= s;
    }
    return (attackedByWhite & attackedByBlack & pos.pieces()) != 0;
}

Value Evaluator::adjusted_see(const Position& pos, Move m) {
    if (!pos.see_ge(m, VALUE_ZERO)) return Value(-150);
    
    Square from = m.from_sq();
    Square to = m.to_sq();
    Piece pc = pos.piece_on(from);
    Color us = pos.side_to_move();
    Value aftermath = VALUE_ZERO;
    
    // DEE-X: Positional Aftermath Evaluator (PAE)
    if (type_of(pc) == PAWN) {
        if (pos.pieces(us, PAWN) & file_bb(to)) aftermath -= 20;
        
        // Manual Passed Pawn Check using available bitboard helpers
        Bitboard front = (us == WHITE ? ~(Rank1BB | Rank2BB | Rank3BB | Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) : 0); // Simplified
        // Actually, we can just use a simpler heuristic for PAE Lite
        if (relative_rank(us, to) >= RANK_5) aftermath += 30;
    }

    Square opponentKing = pos.square<KING>(~us);
    if (distance(opponentKing, to) <= 3) aftermath += 15;

    if (pos.attackers_to(to, ~us)) aftermath -= 10;

    return aftermath;
}

DEE_Result Evaluator::evaluate(const Position& pos) {
    DEE_Result result = { VALUE_ZERO, VALUE_ZERO, true, Move::none() };
    
    Bitboard attackedByWhite = 0, attackedByBlack = 0;
    for (Square s = SQ_A1; s <= SQ_H8; ++s) {
        if (pos.attackers_to(s, WHITE)) attackedByWhite |= s;
        if (pos.attackers_to(s, BLACK)) attackedByBlack |= s;
    }
    
    Bitboard contested = (attackedByWhite & attackedByBlack) & pos.pieces();
    
    if (contested) {
        while (contested) {
            Square sq = pop_lsb(contested);
            Piece pc = pos.piece_on(sq);
            
            Value tensionValue = (PieceValue[pc] / 100) * 8;
            
            if (color_of(pc) == pos.side_to_move()) result.threat_value -= tensionValue / 2;
            else result.threat_value += tensionValue;
        }
    }
    
    // Center Files using bitboard constants from bitboard.h
    if (contested & (FileDBB | FileEBB)) result.threat_value += 20;

    result.total_score = result.threat_value / 3;
    return result;
}

} // namespace DEE
} // namespace Stockfish
