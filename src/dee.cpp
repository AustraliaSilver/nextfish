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
    Bitboard contested = attackedByWhite & attackedByBlack;
    return (contested & pos.pieces()) != 0;
}

Value Evaluator::adjusted_see(const Position& pos, Move m) {
    if (!pos.see_ge(m, VALUE_ZERO)) return Value(-150);
    
    Square to = m.to_sq();
    Piece pc = pos.piece_on(m.from_sq());
    Value aftermath = VALUE_ZERO;
    
    if (type_of(pc) == PAWN) {
        if (pos.pieces(pos.side_to_move(), PAWN) & file_bb(to)) aftermath -= 15;
    }

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
        result.threat_value = popcount(contested) * 8;
    }
    
    result.total_score = result.threat_value / 4;
    return result;
}

} // namespace DEE
} // namespace Stockfish
