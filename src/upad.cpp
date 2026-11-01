#include "upad.h"

namespace NextFish {

UPADEngine GlobalUPAD;

UPADNodeDecision UPADEngine::make_node_decision(const Position& pos, int depth, Value beta,
                                                Value static_eval, double tau, double rho,
                                                double zugzwang_risk, bool is_pv, bool improving) const {
    UPADNodeDecision dec;

    // Do not prune at PV nodes, when in check, in pure pawn endgames, or when in high zugzwang danger
    if (is_pv || pos.is_in_check() || zugzwang_risk > 0.70 || pos.non_pawn_material(pos.side_to_move()) == 0) {
        return dec;
    }

    // Reverse Futility Pruning (Static Null Move Pruning)
    if (depth <= 6 && std::abs(beta) < VALUE_MATE_THRESHOLD) {
        Value margin = static_cast<Value>((improving ? 65 : 85) * depth * (1.15 - 0.35 * rho));
        if (static_eval - margin >= beta) {
            dec.action = NODE_HARD_PRUNE;
            dec.return_score = beta;
            return dec;
        }
    }

    return dec;
}

UPADMoveDecision UPADEngine::make_move_decision(const Position& pos, Move m, int move_idx,
                                                int depth, Value alpha, Value static_eval,
                                                bool gives_check, bool is_capture, bool in_check, bool is_pv,
                                                bool improving, bool is_killer, bool attacks_king_or_queen) const {
    UPADMoveDecision dec;

    // Never prune on PV nodes, on the first move, or for killer moves
    if (is_pv || move_idx == 0 || is_killer) {
        return dec;
    }

    // Never prune when in check, or for moves giving check, promotions, passed pawn pushes, or king attacks
    Piece moving_pc = pos.piece_on(m.from());
    Color us = pos.side_to_move();
    bool is_passed_push = (type_of(moving_pc) == PAWN && pos.pawn_is_passed(us, m.from()));

    Square enemy_king = pos.king_square(~us);
    Bitboard king_zone = 0;
    if (enemy_king != SQ_NONE) {
        king_zone = Bitboards::king_attacks(enemy_king);
        king_zone |= (us == WHITE) ? (king_zone >> 8) : (king_zone << 8);
    }
    bool attacks_king = (king_zone & (1ULL << m.to())) != 0;

    if (in_check || gives_check || is_killer || m.type() == PROMOTION || is_passed_push || attacks_king || type_of(moving_pc) == QUEEN) {
        return dec;
    }

    // Bad capture pruning
    if (is_capture) {
        if (depth <= 4 && pos.see(m, -120 * depth) < 0) {
            dec.action = MOVE_HARD_PRUNE;
            return dec;
        }
        return dec;
    }

    // --- Quiet Moves Pruning ---

    // 1. Late Move Pruning (LMP) - calibrated per Stockfish FutilityMoveCounts
    int lmp_threshold = (improving ? (3 + depth * depth / 3) : (5 + depth * depth / 2));
    if (depth <= 6 && move_idx >= lmp_threshold) {
        dec.action = MOVE_HARD_PRUNE;
        return dec;
    }

    // 2. Futility Pruning (FP)
    if (depth <= 5) {
        Value margin = (improving ? 80 : 110) * depth;
        if (static_eval + margin <= alpha) {
            dec.action = MOVE_HARD_PRUNE;
            return dec;
        }
    }

    // 3. Negative SEE Pruning for quiet moves
    if (depth <= 6 && pos.see(m, -35 * depth) < 0) {
        dec.action = MOVE_HARD_PRUNE;
        return dec;
    }

    return dec;
}

} // namespace NextFish
