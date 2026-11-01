#include "dqrs.h"
#include "search.h"
#include "smtta.h"
#include "ssfzre.h"
#include "movegen.h"
#include <algorithm>

namespace NextFish {

DQRSEngine GlobalDQRS;

Value DQRSEngine::search(Position& pos, Value alpha, Value beta, int depth_budget, int ply, uint64_t& nodes) {
    nodes++;

    if ((nodes & 127) == 0) {
        GlobalSearch.check_time();
    }
    if (GlobalSearch.is_stopped()) {
        return 0;
    }

    if (pos.is_draw_by_rule()) {
        return VALUE_DRAW;
    }

    if (ply >= MAX_PLY - 1) {
        return GlobalSSFZRE.evaluate_static(pos);
    }

    // 1. Probe Transposition Table
    SMTTAProbeResult tt_res = GlobalTT.probe(pos.hash_key(), 0, alpha, beta, ply, pos.halfmove_clock(), false);
    if (tt_res.cutoff) {
        return tt_res.score;
    }

    bool in_check = pos.is_in_check();

    // 2. Stand-pat if not in check
    Value stand_pat = -VALUE_INFINITE;
    Value orig_alpha = alpha;
    Move best_move = MOVE_NONE;
    bool full_eval_computed = false;

    if (!in_check) {
        if (tt_res.cached_neural.valid && tt_res.cached_neural.eval_cp != 0) {
            stand_pat = tt_res.cached_neural.eval_cp;
            full_eval_computed = true;
        } else {
            Value mat = pos.material_balance();
            // Fast lazy cutoff: if material alone is crushing (e.g. >= beta + 400),
            // stand-pat will exceed beta without calling expensive neural + mobility eval
            if (mat >= beta + 400) {
                stand_pat = mat;
            } else {
                stand_pat = GlobalSSFZRE.evaluate_static(pos);
                full_eval_computed = true;
            }
        }
        if (stand_pat >= beta) {
            GlobalTT.store(pos.hash_key(), beta, full_eval_computed ? stand_pat : 0, MOVE_NONE, 0, BOUND_LOWER,
                           false, ply, 1, 0.5, 0.3, 0.5, 0, pos.halfmove_clock(), false);
            return beta;
        }
        if (stand_pat > alpha) {
            alpha = stand_pat;
        }
    }

    // 3. Depth budget cutoff. In check we always resolve evasions, but never
    // declare a mate just because the budget ran out.
    if (depth_budget <= 0 && !in_check) {
        return stand_pat;
    }
    if (depth_budget <= -8) {
        return in_check ? GlobalSSFZRE.evaluate_static(pos) : stand_pat;
    }

    // 4. Generate moves: all evasions if in check, else captures + promotions.
    MoveList moves;
    if (in_check) {
        generate_moves<EVASIONS>(pos, moves);
    } else {
        generate_moves<CAPTURES>(pos, moves);
    }

    // 5. Score moves with MVV-LVA and TT move
    struct ScoredMove {
        Move m;
        int score;
    };
    ScoredMove scored[256];
    int count = moves.size();

    constexpr int PieceVals[] = { 100, 320, 330, 500, 900, 20000, 0 };

    for (int i = 0; i < count; ++i) {
        Move m = moves[i];
        scored[i].m = m;
        if (m == tt_res.best_move) {
            scored[i].score = 100000;
        } else {
            Square from = m.from();
            Square to = m.to();
            Piece victim = (m.type() == EN_PASSANT) ? make_piece(~pos.side_to_move(), PAWN) : pos.piece_on(to);
            Piece attacker = pos.piece_on(from);
            int v_val = (victim != NO_PIECE) ? PieceVals[type_of(victim)] : 0;
            int a_val = (attacker != NO_PIECE) ? PieceVals[type_of(attacker)] : 0;
            int promo_val = (m.type() == PROMOTION) ? 800 : 0;
            scored[i].score = v_val * 10 - a_val + promo_val;
        }
    }

    // 6. Search loop with on-demand selection sort
    int legal_moves_tried = 0;
    for (int i = 0; i < count; ++i) {
        int best_idx = i;
        for (int j = i + 1; j < count; ++j) {
            if (scored[j].score > scored[best_idx].score) {
                best_idx = j;
            }
        }
        if (best_idx != i) {
            std::swap(scored[i], scored[best_idx]);
        }

        Move m = scored[i].m;

        if (!in_check) {
            Square to = m.to();
            Piece target = (m.type() == EN_PASSANT) ? make_piece(~pos.side_to_move(), PAWN) : pos.piece_on(to);
            int cap_val = (target != NO_PIECE) ? PieceVals[type_of(target)] : 0;
            if (m.type() == PROMOTION) cap_val += 800;

            // Delta pruning: prune captures that cannot possibly raise alpha
            if (stand_pat + cap_val + 200 < alpha && !is_mate_score(alpha)) {
                continue;
            }

            // Skip clearly losing exchanges (SEE < 0)
            Piece attacker = pos.piece_on(m.from());
            int a_val = (attacker != NO_PIECE) ? PieceVals[type_of(attacker)] : 100;
            if (cap_val < a_val && pos.see(m, 0) < 0) {
                continue;
            }
        }

        if (!pos.is_legal(m)) {
            continue;
        }
        legal_moves_tried++;

        StateInfo& st = q_states[ply];
        pos.make_move(m, st);
        Value score = -search(pos, -beta, -alpha, depth_budget - 1, ply + 1, nodes);
        pos.unmake_move(m);

        if (GlobalSearch.is_stopped()) {
            return 0;
        }

        if (score >= beta) {
            Value tt_eval = (in_check || !full_eval_computed) ? 0 : stand_pat;
            GlobalTT.store(pos.hash_key(), beta, tt_eval, m, 0, BOUND_LOWER,
                           false, ply, 1, 0.5, 0.3, 0.5, 0, pos.halfmove_clock(), false);
            return beta;
        }
        if (score > alpha) {
            alpha = score;
            best_move = m;
        }
    }

    if (in_check && legal_moves_tried == 0) {
        return mated_in(ply);
    }

    BoundType b_type = (alpha > orig_alpha) ? BOUND_EXACT : BOUND_UPPER;
    Value tt_eval = (in_check || !full_eval_computed) ? 0 : stand_pat;
    GlobalTT.store(pos.hash_key(), alpha, tt_eval, best_move, 0, b_type,
                   false, ply, 1, 0.5, 0.3, 0.5, 0, pos.halfmove_clock(), false);

    return alpha;
}

} // namespace NextFish
