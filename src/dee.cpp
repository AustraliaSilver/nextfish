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

int Evaluator::tension_score(const Position& pos) {
    Bitboard wAtt = 0, bAtt = 0;
    compute_both_attack_maps(pos, wAtt, bAtt);

    // Contested squares (both sides attack). Count occupied ones higher.
    const Bitboard contested = wAtt & bAtt;
    const int score          = popcount(contested) + 2 * popcount(contested & pos.pieces());

    return std::clamp(score, 0, 100);
}
void Evaluator::compute_both_attack_maps(const Position& pos, Bitboard& us, Bitboard& them) {
    Bitboard w = 0, b = 0;

    w |= pos.attacks_by<PAWN>(WHITE);
    w |= pos.attacks_by<KNIGHT>(WHITE);
    w |= pos.attacks_by<BISHOP>(WHITE);
    w |= pos.attacks_by<ROOK>(WHITE);
    w |= pos.attacks_by<QUEEN>(WHITE);
    w |= pos.attacks_by<KING>(WHITE);

    b |= pos.attacks_by<PAWN>(BLACK);
    b |= pos.attacks_by<KNIGHT>(BLACK);
    b |= pos.attacks_by<BISHOP>(BLACK);
    b |= pos.attacks_by<ROOK>(BLACK);
    b |= pos.attacks_by<QUEEN>(BLACK);
    b |= pos.attacks_by<KING>(BLACK);

    us   = w;
    them = b;
}

} // namespace DEE
} // namespace Stockfish
