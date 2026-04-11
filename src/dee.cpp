#include "dee.h"
#include "position.h"
#include "bitboard.h"
#include <algorithm>

namespace Stockfish {
namespace DEE {

// DEE V19: The Proof.
// Reverting to the successful V17 lightweight logic for maximum stability.

Value Evaluator::adjusted_see(const Position& pos, Move m) {
    if (!m) return VALUE_ZERO;

    Square to = m.to_sq();
    Color us = pos.side_to_move();
    
    // Efficiency: Neutral exchanges only (-50 to +50 SEE)
    if (pos.see_ge(m, 50) || !pos.see_ge(m, -50))
        return VALUE_ZERO;

    // Detect numerical pressure gap (fastest possible calculation)
    Bitboard attackers = pos.attackers_to(to, pos.pieces() ^ m.from_sq());
    int ourAtt = popcount(attackers & pos.pieces(us));
    int theirAtt = popcount(attackers & pos.pieces(~us));
    
    if (ourAtt > theirAtt)
        return Value(ourAtt * 10);

    return VALUE_ZERO;
}

int Evaluator::tension_score(const Position& pos) { (void)pos; return 0; }
void Evaluator::compute_both_attack_maps(const Position& pos, Bitboard& us, Bitboard& them) {
    (void)pos; (void)us; (void)them;
}

} // namespace DEE
} // namespace Stockfish
