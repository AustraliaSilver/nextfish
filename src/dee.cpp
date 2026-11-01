#include "dee.h"

namespace NextFish {

DEEEngine GlobalDEE;

DEEResult DEEEngine::evaluate_exchange(const Position& pos, Move m) const {
    DEEResult res;
    res.see_value = pos.see(m, -20000);

    Square from = m.from();
    Square to = m.to();
    Piece moving_pc = pos.piece_on(from);
    Piece target_pc = (m.type() == EN_PASSANT) ? make_piece(~pos.side_to_move(), PAWN) : pos.piece_on(to);

    Value structural = 0;
    Value dynamic = 0;

    // 1. Positional Aftermath: Structural effects
    if (target_pc != NO_PIECE) {
        // Did we capture a bishop while opponent has bishop pair?
        if (type_of(target_pc) == BISHOP) {
            Color them = ~pos.side_to_move();
            if (popcount(pos.pieces(them, BISHOP)) >= 2) {
                structural += 28; // Destroy bishop pair
            }
        }

        // Did capture open a file on our rook?
        File to_file = file_of(to);
        Bitboard file_mask = 0x0101010101010101ULL << to_file;
        if (pos.pieces(pos.side_to_move(), ROOK) & file_mask) {
            structural += 18; // Open file for rook
        }
    }

    // 2. Dynamic Energy: King safety & pressure
    Color them = ~pos.side_to_move();
    Square opp_king = pos.king_square(them);
    int dist_to_king = distance(to, opp_king);
    if (dist_to_king <= 2) {
        dynamic += 35 - 10 * dist_to_king;
    }

    // Check promotion bonus
    if (m.type() == PROMOTION) {
        structural += 40;
    }

    res.structural_delta = structural;
    res.dynamic_delta = dynamic;
    res.total_value = res.see_value + structural + dynamic;
    res.tension_energy = (target_pc != NO_PIECE) ? 0.45 : 0.10;

    return res;
}

Value DEEEngine::score_move(const Position& pos, Move m) const {
    DEEResult r = evaluate_exchange(pos, m);
    return r.total_value;
}

} // namespace NextFish
