#include "hamo.h"
#include <algorithm>
#include <cstring>

namespace NextFish {

HAMOEngine GlobalHAMO;

HAMOEngine::HAMOEngine() {
    clear();
}

void HAMOEngine::clear() {
    std::memset(killer_moves, 0, sizeof(killer_moves));
    std::memset(counter_moves, 0, sizeof(counter_moves));
    std::memset(butterfly_history, 0, sizeof(butterfly_history));
    std::memset(continuation_history, 0, sizeof(continuation_history));
    std::memset(pawn_history, 0, sizeof(pawn_history));
    std::memset(capture_history, 0, sizeof(capture_history));
}

int HAMOEngine::score_move(const Position& pos, Move m, Move tt_move, Move prev_move,
                          int ply, double tactical_complexity) const {
    if (tt_move.is_valid() && m == tt_move) {
        return 30000000;
    }

    Square from = m.from();
    Square to = m.to();
    Piece moving_pc = pos.piece_on(from);
    Piece target_pc = pos.piece_on(to);
    bool is_capture = (target_pc != NO_PIECE) || (m.type() == EN_PASSANT);

    if (is_capture || m.type() == PROMOTION) {
        Value dee_val = GlobalDEE.score_move(pos, m);
        constexpr int PieceVals[] = { 100, 320, 330, 500, 900, 20000, 0 };
        int victim_val = (target_pc != NO_PIECE) ? PieceVals[type_of(target_pc)] : 100;
        int attacker_val = (moving_pc != NO_PIECE) ? PieceVals[type_of(moving_pc)] : 100;
        int cap_hist = 0;
        if (moving_pc < 12 && target_pc != NO_PIECE) {
            cap_hist = capture_history[moving_pc][to][type_of(target_pc)];
        }
        if (victim_val >= attacker_val || pos.see(m, 0) >= 0) {
            return 20000000 + dee_val + cap_hist;
        } else {
            return -10000000 + dee_val + cap_hist;
        }
    }

    // Quiet move
    Color us = pos.side_to_move();

    // 1. Passed pawn pushes to 7th/2nd rank (immediate promotion threat)
    if (moving_pc != NO_PIECE && type_of(moving_pc) == PAWN) {
        Rank r = rank_of(to);
        if (r == RANK_7 || r == RANK_2) {
            return 18500000;
        }
    }

    // 2. Killers: proven sibling refutations at this ply
    if (ply < MAX_PLY) {
        if (m == killer_moves[ply][0]) return 17000000;
        if (m == killer_moves[ply][1]) return 16000000;
    }

    // 3. Counter move: proven refutation against opponent's previous move
    if (prev_move.is_valid()) {
        Piece prev_pc = pos.piece_on(prev_move.to());
        if (prev_pc < 12 && m == counter_moves[prev_pc][prev_move.to()]) {
            return 15000000;
        }
    }

    // 4. Quiet checking moves
    if (pos.gives_check(m)) {
        return 14500000;
    }

    // 5. Advanced passed pawn pushes (Rank 6/3 and Rank 5/4)
    if (moving_pc != NO_PIECE && type_of(moving_pc) == PAWN && pos.pawn_is_passed(us, from)) {
        Rank r = rank_of(to);
        if (r == RANK_6 || r == RANK_3) return 13500000;
        if (r == RANK_5 || r == RANK_4) return 12500000;
    }

    // 6. History-Ranked Quiet Moves (Butterfly + 2*Continuation + PawnHistory)
    int hist = butterfly_history[us][from][to];

    if (prev_move.is_valid()) {
        Piece prev_pc = pos.piece_on(prev_move.to());
        if (prev_pc < 12 && moving_pc < 12) {
            hist += 2 * continuation_history[prev_pc][prev_move.to()][moving_pc][to];
        }
    }

    int p_class = static_cast<int>(pos.pawn_hash_key() & 0x3F);
    if (moving_pc < 12) {
        hist += pawn_history[p_class][moving_pc][to];
    }

    if (tactical_complexity > 0.0) {
        double base_crit = 0.10;
        if (target_pc != NO_PIECE || m.type() == EN_PASSANT) {
            base_crit += 0.35 + 0.10 * (target_pc != NO_PIECE ? static_cast<int>(type_of(target_pc)) : 0);
        }
        if (moving_pc != NO_PIECE && type_of(moving_pc) == KING) base_crit += 0.15;
        double mcs = std::clamp(base_crit * (0.5 + 0.5 * tactical_complexity), 0.0, 1.0);
        hist += static_cast<int>(mcs * 2500.0);
    }

    return hist;
}

void HAMOEngine::generate_and_order(const Position& pos, MoveList& ordered_moves,
                                    Move tt_move, Move prev_move, int ply,
                                    double tactical_complexity) const {
    MoveList pseudo;
    if (pos.is_in_check()) {
        generate_moves<EVASIONS>(pos, pseudo);
    } else {
        generate_moves<ALL>(pos, pseudo);
    }

    struct ScoredMove {
        Move m;
        int score;
    };

    ScoredMove scored[256];
    int count = 0;

    for (int i = 0; i < pseudo.size(); ++i) {
        Move m = pseudo[i];
        if (pos.is_legal(m)) {
            scored[count].m = m;
            scored[count].score = score_move(pos, m, tt_move, prev_move, ply, tactical_complexity);
            count++;
        }
    }

    // Sort scored moves descending
    std::sort(scored, scored + count, [](const ScoredMove& a, const ScoredMove& b) {
        return a.score > b.score;
    });

    ordered_moves.clear();
    for (int i = 0; i < count; ++i) {
        ordered_moves.push_back(scored[i].m);
    }
}

void HAMOEngine::update_on_cutoff(const Position& pos, Move move, int depth, int ply,
                                  Move prev_move, const Move* tried_quiets, int tried_count) {
    Square to = move.to();
    Square from = move.from();
    Piece target = pos.piece_on(to);
    Piece moving_pc = pos.piece_on(from);
    int bonus = std::min(1200, 16 * depth * depth);

    auto update_entry = [](int& val, int delta) {
        val += delta - val * std::abs(delta) / 16384;
    };

    Color us = pos.side_to_move();
    int p_class = static_cast<int>(pos.pawn_hash_key() & 0x3F);

    if (target != NO_PIECE || move.type() == EN_PASSANT || move.type() == PROMOTION) {
        if (moving_pc < 12 && target != NO_PIECE) {
            update_entry(capture_history[moving_pc][to][type_of(target)], bonus);
        }
        // Penalize tried quiet moves that failed prior to this capture cutoff
        int penalty = -bonus;
        for (int i = 0; i < tried_count; ++i) {
            Move q = tried_quiets[i];
            if (q != move) {
                Square q_from = q.from();
                Square q_to = q.to();
                Piece q_pc = pos.piece_on(q_from);
                update_entry(butterfly_history[us][q_from][q_to], penalty);
                if (q_pc < 12) {
                    update_entry(pawn_history[p_class][q_pc][q_to], penalty);
                }
                if (prev_move.is_valid()) {
                    Piece prev_pc = pos.piece_on(prev_move.to());
                    if (prev_pc < 12 && q_pc < 12) {
                        update_entry(continuation_history[prev_pc][prev_move.to()][q_pc][q_to], penalty);
                    }
                }
            }
        }
        return;
    }

    // 1. Update Killers
    if (ply < MAX_PLY) {
        if (killer_moves[ply][0] != move) {
            killer_moves[ply][1] = killer_moves[ply][0];
            killer_moves[ply][0] = move;
        }
    }

    // 2. Update Counter Move
    if (prev_move.is_valid()) {
        Piece prev_pc = pos.piece_on(prev_move.to());
        if (prev_pc < 12) {
            counter_moves[prev_pc][prev_move.to()] = move;
        }
    }

    // 3. Update Histories
    update_entry(butterfly_history[us][from][to], bonus);
    if (moving_pc < 12) {
        update_entry(pawn_history[p_class][moving_pc][to], bonus);
    }

    if (prev_move.is_valid()) {
        Piece prev_pc = pos.piece_on(prev_move.to());
        if (prev_pc < 12 && moving_pc < 12) {
            update_entry(continuation_history[prev_pc][prev_move.to()][moving_pc][to], bonus);
        }
    }

    // Penalize tried quiet moves that failed to cause a cutoff
    int penalty = -bonus;
    for (int i = 0; i < tried_count; ++i) {
        Move q = tried_quiets[i];
        if (q != move) {
            Square q_from = q.from();
            Square q_to = q.to();
            Piece q_pc = pos.piece_on(q_from);
            update_entry(butterfly_history[us][q_from][q_to], penalty);
            if (q_pc < 12) {
                update_entry(pawn_history[p_class][q_pc][q_to], penalty);
            }
            if (prev_move.is_valid()) {
                Piece prev_pc = pos.piece_on(prev_move.to());
                if (prev_pc < 12 && q_pc < 12) {
                    update_entry(continuation_history[prev_pc][prev_move.to()][q_pc][q_to], penalty);
                }
            }
        }
    }
}

} // namespace NextFish
