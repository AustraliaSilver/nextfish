#include "ssfzre.h"
#include "harenn.h"
#include "movegen.h"
#include "pawn_table.h"
#include <cmath>
#include <algorithm>

namespace NextFish {

SSFZREEngine GlobalSSFZRE;

static inline Bitboard step_king(Bitboard b) {
    Bitboard res = b | ((b << 8) | (b >> 8));
    res |= ((res << 1) & ~0x0101010101010101ULL) | ((res >> 1) & ~0x8080808080808080ULL);
    return res;
}

struct PSQTScore {
    int16_t mg;
    int16_t eg;
};

#define S(mg, eg) { (mg), (eg) }

static constexpr PSQTScore PSQT[6][64] = {
  { // PAWN = 0
   S(  0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(  0, 0),
   S(-28,-8), S(-6,-8), S( 4,-8), S(14,-8), S(14,-8), S( 4,-8), S(-6,-8), S(-28,-8),
   S(-28,-8), S(-6,-8), S( 9,-8), S(36,-8), S(36,-8), S( 9,-8), S(-6,-8), S(-28,-8),
   S(-28,-8), S(-6,-8), S(17,-8), S(58,-8), S(58,-8), S(17,-8), S(-6,-8), S(-28,-8),
   S(-28,-8), S(-6,-8), S(17,-8), S(36,-8), S(36,-8), S(17,-8), S(-6,-8), S(-28,-8),
   S(-28,-8), S(-6,-8), S( 9,-8), S(14,-8), S(14,-8), S( 9,-8), S(-6,-8), S(-28,-8),
   S(-28,-8), S(-6,-8), S( 4,-8), S(14,-8), S(14,-8), S( 4,-8), S(-6,-8), S(-28,-8),
   S(  0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(  0, 0)
  },
  { // KNIGHT = 1
   S(-135,-104), S(-107,-79), S(-80,-55), S(-67,-42), S(-67,-42), S(-80,-55), S(-107,-79), S(-135,-104),
   S( -93, -79), S( -67,-55), S(-39,-30), S(-25,-17), S(-25,-17), S(-39,-30), S( -67,-55), S( -93, -79),
   S( -53, -55), S( -25,-30), S(  1, -6), S( 13,  5), S( 13,  5), S(  1, -6), S( -25,-30), S( -53, -55),
   S( -25, -42), S(   1,-17), S( 27,  5), S( 41, 18), S( 41, 18), S( 27,  5), S(   1,-17), S( -25, -42),
   S( -11, -42), S(  13,-17), S( 41,  5), S( 55, 18), S( 55, 18), S( 41,  5), S(  13,-17), S( -11, -42),
   S( -11, -55), S(  13,-30), S( 41, -6), S( 55,  5), S( 55,  5), S( 41, -6), S(  13,-30), S( -11, -55),
   S( -53, -79), S( -25,-55), S(  1,-30), S( 13,-17), S( 13,-17), S(  1,-30), S( -25,-55), S( -53, -79),
   S(-193,-104), S( -67,-79), S(-39,-55), S(-25,-42), S(-25,-42), S(-39,-55), S( -67,-79), S(-193,-104)
  },
  { // BISHOP = 2
   S(-40,-59), S(-40,-42), S(-35,-35), S(-30,-26), S(-30,-26), S(-35,-35), S(-40,-42), S(-40,-59),
   S(-17,-42), S(  0,-26), S( -4,-18), S(  0,-11), S(  0,-11), S( -4,-18), S(  0,-26), S(-17,-42),
   S(-13,-35), S( -4,-18), S(  8,-11), S(  4, -4), S(  4, -4), S(  8,-11), S( -4,-18), S(-13,-35),
   S( -8,-26), S(  0,-11), S(  4, -4), S( 17,  4), S( 17,  4), S(  4, -4), S(  0,-11), S( -8,-26),
   S( -8,-26), S(  0,-11), S(  4, -4), S( 17,  4), S( 17,  4), S(  4, -4), S(  0,-11), S( -8,-26),
   S(-13,-35), S( -4,-18), S(  8,-11), S(  4, -4), S(  4, -4), S(  8,-11), S( -4,-18), S(-13,-35),
   S(-17,-42), S(  0,-26), S( -4,-18), S(  0,-11), S(  0,-11), S( -4,-18), S(  0,-26), S(-17,-42),
   S(-17,-59), S(-17,-42), S(-13,-35), S( -8,-26), S( -8,-26), S(-13,-35), S(-17,-42), S(-17,-59)
  },
  { // ROOK = 3
   S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
   S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
   S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
   S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
   S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
   S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
   S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
   S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3)
  },
  { // QUEEN = 4
   S(8,-80), S(8,-54), S(8,-42), S(8,-30), S(8,-30), S(8,-42), S(8,-54), S(8,-80),
   S(8,-54), S(8,-30), S(8,-18), S(8, -6), S(8, -6), S(8,-18), S(8,-30), S(8,-54),
   S(8,-42), S(8,-18), S(8, -6), S(8,  6), S(8,  6), S(8, -6), S(8,-18), S(8,-42),
   S(8,-30), S(8, -6), S(8,  6), S(8, 18), S(8, 18), S(8,  6), S(8, -6), S(8,-30),
   S(8,-30), S(8, -6), S(8,  6), S(8, 18), S(8, 18), S(8,  6), S(8, -6), S(8,-30),
   S(8,-42), S(8,-18), S(8, -6), S(8,  6), S(8,  6), S(8, -6), S(8,-18), S(8,-42),
   S(8,-54), S(8,-30), S(8,-18), S(8, -6), S(8, -6), S(8,-18), S(8,-30), S(8,-54),
   S(8,-80), S(8,-54), S(8,-42), S(8,-30), S(8,-30), S(8,-42), S(8,-54), S(8,-80)
  },
  { // KING = 5
   S(287, 18), S(311, 77), S(262,105), S(214,135), S(214,135), S(262,105), S(311, 77), S(287, 18),
   S(262, 77), S(287,135), S(238,165), S(190,193), S(190,193), S(238,165), S(287,135), S(262, 77),
   S(214,105), S(238,165), S(190,193), S(142,222), S(142,222), S(190,193), S(238,165), S(214,105),
   S(190,135), S(214,193), S(167,222), S(119,251), S(119,251), S(167,222), S(214,193), S(190,135),
   S(167,135), S(190,193), S(142,222), S( 94,251), S( 94,251), S(142,222), S(190,193), S(167,135),
   S(142,105), S(167,165), S(119,193), S( 69,222), S( 69,222), S(119,193), S(167,165), S(142,105),
   S(119, 77), S(142,135), S( 94,165), S( 46,193), S( 46,193), S( 94,165), S(142,135), S(119, 77),
   S(94,  18), S(119, 77), S( 69,105), S( 21,135), S( 21,135), S( 69,105), S(119, 77), S( 94, 18)
  }
};
#undef S

// Bitboard flood fill used by the fortress detector. Only called in low material
// positions, so its cost is negligible.
double SSFZREEngine::compute_barrier_cutset(const Position& pos, Color attacking_side) const {
    Color defending_side = ~attacking_side;

    Bitboard def_pawns = pos.pieces(defending_side, PAWN);
    if (!def_pawns) return 0.0;

    Bitboard def_pawn_attacks = (defending_side == WHITE)
        ? (((def_pawns & ~0x0101010101010101ULL) << 7) | ((def_pawns & ~0x8080808080808080ULL) << 9))
        : (((def_pawns & ~0x0101010101010101ULL) >> 9) | ((def_pawns & ~0x8080808080808080ULL) >> 7));

    Bitboard impermeable = def_pawns | def_pawn_attacks | pos.pieces(defending_side);
    Bitboard passable = ~impermeable;

    Square att_king = pos.king_square(attacking_side);
    if (att_king == SQ_NONE) return 0.0;
    Bitboard target_zone = (defending_side == BLACK) ? 0xE0E0E0E0E0E0E0E0ULL : 0x0707070707070707ULL;

    Bitboard reached = square_bb(att_king);
    for (int step = 0; step < 12; ++step) {
        Bitboard next_step = step_king(reached) & passable;
        if (next_step == reached) break;
        reached = next_step;
    }

    bool king_blocked = (reached & target_zone) == 0;

    bool opp_bishops = false;
    if (pos.pieces(BISHOP) && !pos.pieces(ROOK) && !pos.pieces(QUEEN)) {
        Bitboard wb = pos.pieces(WHITE, BISHOP);
        Bitboard bb = pos.pieces(BLACK, BISHOP);
        if (popcount(wb) == 1 && popcount(bb) == 1) {
            constexpr Bitboard DARK_SQUARES = 0xAA55AA55AA55AA55ULL;
            bool w_dark = (wb & DARK_SQUARES) != 0;
            bool b_dark = (bb & DARK_SQUARES) != 0;
            if (w_dark != b_dark) opp_bishops = true;
        }
    }

    double barrier = 0.0;
    if (king_blocked) barrier += 0.55;
    if (opp_bishops) barrier += 0.35;
    return std::clamp(barrier, 0.0, 1.0);
}

double SSFZREEngine::zugzwang_risk(const Position& pos) const {
    Color us = pos.side_to_move();
    int non_pawn = pos.non_pawn_material(us);
    if (non_pawn == 0) return 1.0;
    if (non_pawn <= 330) return (popcount(pos.pieces(us)) <= 4) ? 0.80 : 0.35;
    return 0.0;
}

double SSFZREEngine::compute_zugzwang_risk(const Position& pos) const {
    // Exact safe-mobility version. Kept for diagnostics / callers that want it,
    // but the search uses the cheap material based estimate above.
    Color us = pos.side_to_move();
    if (pos.non_pawn_material(us) > 600) return 0.0;

    MoveList legal;
    generate_all_legal(pos, legal);
    if (legal.empty()) return 0.0;

    int safe_moves = 0;
    for (int i = 0; i < legal.size(); ++i) {
        if (pos.see(legal[i], 0) >= 0) safe_moves++;
    }
    double exhaustion = 1.0 - static_cast<double>(safe_moves) / legal.size();
    int non_pawn = pos.non_pawn_material(us);
    double mat_factor = (non_pawn == 0) ? 0.45 : (non_pawn <= 330 ? 0.25 : 0.05);
    return std::clamp(0.70 * exhaustion + mat_factor, 0.0, 1.0);
}

Value SSFZREEngine::evaluate_static(const Position& pos) const {
    Value raw = GlobalHARENN.evaluate_eval_only(pos);
    Value mat = pos.material_balance();
    Value blended = static_cast<Value>(std::lround(0.60 * raw + 0.40 * mat));
    return correct(pos, blended).corrected_eval;
}

SSFZREOutput SSFZREEngine::correct(const Position& pos, Value eval_cp) const {
    SSFZREOutput out;
    out.corrected_eval = eval_cp;
    out.fortress_prob = 0.0;
    out.zugzwang_risk = zugzwang_risk(pos);

    Color us = pos.side_to_move();
    Color them = ~us;
    int mat_balance = pos.material_balance();

    // Material reality guard: never hallucinate an advantage while down a minor
    // piece or more.
    if (mat_balance <= -200) {
        out.corrected_eval = std::min(out.corrected_eval, static_cast<Value>(mat_balance + 150));
    }

    // ── Fortress / stagnation damping ────────────────────────────────────────
    // Position only (halfmove clock based) so the value is TT safe.
    //
    // A real fortress needs almost no material to break it: no queens, and at
    // most a minor's worth of non-pawn material for the stronger side. The
    // previous version damped any position with <= 1600 non-pawn material and
    // let the halfmove clock dominate the logit, so a rook-up endgame at
    // halfmove 80 was scored +63 instead of +820 and queen-vs-rook fell from
    // +948 to +492. That made the engine unable to convert won endings. The
    // detector is now gated on material and the damping is capped at half of
    // the advantage, because a fortress is a probabilistic judgement and never
    // a proof of a draw.
    int total_nonpawn = pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK);
    if (pos.pieces(QUEEN) == 0 && total_nonpawn <= 1000 && std::abs(eval_cp) >= 120) {
        Color stronger = (eval_cp > 0) ? us : them;
        int stronger_nonpawn = pos.non_pawn_material(stronger);
        if (stronger_nonpawn <= 400) {
            double barrier = compute_barrier_cutset(pos, stronger);
            if (barrier >= 0.50) {
                // Stagnation saturates quickly and only confirms the barrier.
                double stagnation = std::min(0.6, 0.015 * static_cast<double>(pos.halfmove_clock()));
                double logit = 4.5 * barrier + 3.0 * stagnation - 4.5;
                double p_fortress = 1.0 / (1.0 + std::exp(-logit));
                out.fortress_prob = p_fortress;
                if (p_fortress > 0.20) {
                    double damp = 0.50 * p_fortress;
                    out.corrected_eval = static_cast<Value>(std::lround(eval_cp * (1.0 - damp)));
                }
            }
        }
    }

    // ── Passed pawns (Stockfish 2.3.1 quadratic scaling & king distance) ─────
    {
        int phase = std::clamp((total_nonpawn - 800) * 128 / (6400 - 800), 0, 128);

        auto eval_passers_for = [&](Color c) -> int {
            Color them_c = ~c;
            Square ksq_us = pos.king_square(c);
            Square ksq_them = pos.king_square(them_c);
            Bitboard passers = pos.passed_pawns(c);
            int total_bonus = 0;

            while (passers) {
                Square s = pop_lsb(passers);
                int r = (c == WHITE) ? rank_of(s) : (7 - rank_of(s)); // 1 to 6 (rank 2 to 7)
                int rel_r = r - 1; // 0 (rank 2) to 5 (rank 7)
                int rr = rel_r * (rel_r - 1); // 0, 0, 2, 6, 12, 20

                int mg = 8 * rr;
                int eg = 5 * (rr + rel_r + 1);

                if (rel_r >= 1) { // Rank 3 or higher
                    Square blockSq = (c == WHITE) ? static_cast<Square>(s + 8) : static_cast<Square>(s - 8);
                    Square queeningSq = make_square(file_of(s), (c == WHITE ? RANK_8 : RANK_1));

                    // King distance in endgame: opponent king far is good, friendly king close is good
                    if (ksq_them != SQ_NONE) {
                        eg += distance(ksq_them, blockSq) * 2 * std::max(1, rr);
                    }
                    if (ksq_us != SQ_NONE) {
                        eg -= distance(ksq_us, blockSq) * 1 * std::max(1, rr);
                    }

                    // Path to queening square
                    Bitboard path = 0;
                    if (c == WHITE) {
                        for (int rk = rank_of(s) + 1; rk <= 7; ++rk) {
                            path |= square_bb(make_square(file_of(s), static_cast<Rank>(rk)));
                        }
                    } else {
                        for (int rk = rank_of(s) - 1; rk >= 0; --rk) {
                            path |= square_bb(make_square(file_of(s), static_cast<Rank>(rk)));
                        }
                    }

                    bool path_clear = (pos.occupied() & path) == 0;
                    if (path_clear) {
                        eg += 8 * std::max(1, rr);
                        mg += 5 * std::max(1, rr);

                        // Rule of the square in endgame with no enemy non-pawn pieces
                        if (pos.non_pawn_material(them_c) == 0 && ksq_them != SQ_NONE) {
                            int moves_to_queen = (c == WHITE ? (7 - rank_of(s)) : rank_of(s));
                            if ((c == WHITE && rank_of(s) == RANK_2) || (c == BLACK && rank_of(s) == RANK_7)) {
                                moves_to_queen--;
                            }
                            int king_dist = distance(ksq_them, queeningSq);
                            int effective_king_dist = king_dist - (pos.side_to_move() == them_c ? 0 : 1);
                            if (effective_king_dist > moves_to_queen) {
                                eg += 300; // Unstoppable passer in pure pawn ending
                            }
                        }
                    } else {
                        // Blocked by enemy piece
                        if (pos.pieces(them_c) & square_bb(blockSq)) {
                            eg -= 5 * std::max(1, rr);
                            mg -= 4 * std::max(1, rr);
                        }
                    }

                    // Supporting pawns: defended by friendly pawn
                    Bitboard my_pawns = pos.pieces(c, PAWN);
                    if (my_pawns & Bitboards::pawn_attacks(~c, s)) {
                        mg += 6 * rel_r;
                        eg += 10 * rel_r;
                    }

                    // Connected passed pawns
                    File f = file_of(s);
                    Bitboard adj_files = 0;
                    if (f > 0) adj_files |= (0x0101010101010101ULL << (f - 1));
                    if (f < 7) adj_files |= (0x0101010101010101ULL << (f + 1));
                    if (pos.passed_pawns(c) & adj_files) {
                        mg += 8 * rel_r;
                        eg += 15 * rel_r;
                    }
                }

                int blended = (mg * phase + eg * (128 - phase)) / 128;
                total_bonus += blended;
            }
            return total_bonus;
        };

        int white_bonus = eval_passers_for(WHITE);
        int black_bonus = eval_passers_for(BLACK);
        int net = (us == WHITE) ? (white_bonus - black_bonus) : (black_bonus - white_bonus);
        out.corrected_eval += net;
    }

    // ── Pawn structure: isolated, doubled, and connected pawns (PawnTable Cached) ─
    {
        int phase = std::clamp((total_nonpawn - 800) * 128 / (6400 - 800), 0, 128);
        Key pkey = pos.pawn_hash_key();
        int16_t w_mg = 0, w_eg = 0, b_mg = 0, b_eg = 0;

        if (!GlobalPawnTable.probe(pkey, w_mg, w_eg, b_mg, b_eg)) {
            auto eval_pawn_structure = [&](Color c) -> std::pair<int, int> {
                Bitboard our_pawns = pos.pieces(c, PAWN);
                int mg = 0, eg = 0;
                Bitboard p = our_pawns;
                while (p) {
                    Square s = pop_lsb(p);
                    File f = file_of(s);
                    Rank r = rank_of(s);

                    Bitboard adj = 0;
                    if (f > 0) adj |= (0x0101010101010101ULL << (f - 1));
                    if (f < 7) adj |= (0x0101010101010101ULL << (f + 1));

                    // Isolated pawn: no friendly pawns on adjacent files
                    if ((our_pawns & adj) == 0) {
                        mg -= 18;
                        eg -= 20;
                    }

                    // Doubled pawn: friendly pawn ahead on the same file
                    Bitboard file_ahead = (0x0101010101010101ULL << f);
                    if (c == WHITE) {
                        file_ahead &= ~((1ULL << (8 * (r + 1))) - 1);
                    } else {
                        file_ahead &= ((1ULL << (8 * r)) - 1);
                    }
                    if (our_pawns & file_ahead) {
                        mg -= 14;
                        eg -= 22;
                    }

                    // Connected pawn / pawn chain
                    Bitboard rank_mask = (0xFFULL << (8 * r));
                    if (c == WHITE && r > 0) rank_mask |= (0xFFULL << (8 * (r - 1)));
                    if (c == BLACK && r < 7) rank_mask |= (0xFFULL << (8 * (r + 1)));
                    Bitboard chain_bb = adj & rank_mask;
                    if (our_pawns & chain_bb) {
                        mg += 8;
                        eg += 4;
                    }
                }
                return {mg, eg};
            };

            auto [w1, w2] = eval_pawn_structure(WHITE);
            auto [b1, b2] = eval_pawn_structure(BLACK);
            w_mg = static_cast<int16_t>(w1);
            w_eg = static_cast<int16_t>(w2);
            b_mg = static_cast<int16_t>(b1);
            b_eg = static_cast<int16_t>(b2);
            GlobalPawnTable.store(pkey, w_mg, w_eg, b_mg, b_eg);
        }

        int net_mg = w_mg - b_mg;
        int net_eg = w_eg - b_eg;
        int struct_val = (net_mg * phase + net_eg * (128 - phase)) / 128;
        out.corrected_eval += (us == WHITE) ? struct_val : -struct_val;
    }

    // ── Tempo bonus for side to move ─────────────────────────────────────────
    {
        int phase = std::clamp((total_nonpawn - 800) * 128 / (6400 - 800), 0, 128);
        int tempo = (24 * phase + 11 * (128 - phase)) / 128;
        out.corrected_eval += tempo;
    }

    // ── Bishop pair ──────────────────────────────────────────────────────────
    {
        int phase = std::clamp((total_nonpawn - 800) * 128 / (6400 - 800), 0, 128);
        int pair = 0;
        if (popcount(pos.pieces(WHITE, BISHOP)) >= 2) pair += (30 * phase + 48 * (128 - phase)) / 128;
        if (popcount(pos.pieces(BLACK, BISHOP)) >= 2) pair -= (30 * phase + 48 * (128 - phase)) / 128;
        out.corrected_eval += (us == WHITE) ? pair : -pair;
    }

    // ── Rooks on open files, 7th rank, and X-ray on Queen (Stockfish tuned) ──
    {
        int phase = std::clamp((total_nonpawn - 800) * 128 / (6400 - 800), 0, 128);
        auto rook_bonus = [&](Color c) -> int {
            int bonus = 0;
            Bitboard rooks = pos.pieces(c, ROOK);
            Bitboard my_p = pos.pieces(c, PAWN);
            Bitboard opp_p = pos.pieces(~c, PAWN);
            Bitboard opp_queens = pos.pieces(~c, QUEEN);

            while (rooks) {
                Square rsq = pop_lsb(rooks);
                Bitboard fmask = 0x0101010101010101ULL << file_of(rsq);

                if ((my_p & fmask) == 0) {
                    if ((opp_p & fmask) == 0) {
                        // Fully open file
                        bonus += (43 * phase + 21 * (128 - phase)) / 128;
                    } else {
                        // Semi-open file
                        bonus += (19 * phase + 10 * (128 - phase)) / 128;
                    }
                    // Threat / X-ray battery on enemy Queen
                    if (opp_queens & fmask) bonus += 35;
                }

                int rel_rk = (c == WHITE) ? rank_of(rsq) : (7 - rank_of(rsq));
                if (rel_rk >= RANK_5) {
                    if (rel_rk == RANK_7) {
                        // General 7th-rank absolute infiltration bonus
                        bonus += (20 * phase + 45 * (128 - phase)) / 128;
                        Square opp_k = pos.king_square(~c);
                        int opp_k_rel = (opp_k != SQ_NONE) ? ((c == WHITE) ? rank_of(opp_k) : (7 - rank_of(opp_k))) : 0;
                        if (opp_k_rel == RANK_8) {
                            bonus += (15 * phase + 30 * (128 - phase)) / 128;
                        }
                    }

                    // Major piece attacking pawns on same rank in enemy territory (Stockfish 2.3.1 RookOnPawnBonus)
                    Bitboard pawns_on_rank = opp_p & (0xFFULL << (8 * rank_of(rsq)));
                    if (pawns_on_rank) {
                        bonus += popcount(pawns_on_rank) * (3 * phase + 48 * (128 - phase)) / 128;
                    }
                }
            }

            Bitboard queens = pos.pieces(c, QUEEN);
            while (queens) {
                Square qsq = pop_lsb(queens);
                int rel_rk = (c == WHITE) ? rank_of(qsq) : (7 - rank_of(qsq));
                if (rel_rk >= RANK_5) {
                    Square opp_k = pos.king_square(~c);
                    int opp_k_rel = (opp_k != SQ_NONE) ? ((c == WHITE) ? rank_of(opp_k) : (7 - rank_of(opp_k))) : 0;
                    if (rel_rk == RANK_7) {
                        bonus += (15 * phase + 30 * (128 - phase)) / 128;
                        if (opp_k_rel == RANK_8) {
                            bonus += (10 * phase + 20 * (128 - phase)) / 128;
                        }
                    }
                    Bitboard q_pawns_on_rank = opp_p & (0xFFULL << (8 * rank_of(qsq)));
                    if (q_pawns_on_rank) {
                        bonus += popcount(q_pawns_on_rank) * (1 * phase + 40 * (128 - phase)) / 128;
                    }
                }
            }
            return bonus;
        };
        int net = rook_bonus(WHITE) - rook_bonus(BLACK);
        out.corrected_eval += (us == WHITE) ? net : -net;
    }

    // Pawn threats against pieces
    {
        Bitboard w_pawns = pos.pieces(WHITE, PAWN);
        Bitboard b_pawns = pos.pieces(BLACK, PAWN);
        Bitboard w_att = ((w_pawns & ~0x0101010101010101ULL) << 7) | ((w_pawns & ~0x8080808080808080ULL) << 9);
        Bitboard b_att = ((b_pawns & ~0x8080808080808080ULL) >> 7) | ((b_pawns & ~0x0101010101010101ULL) >> 9);

        int white_loss = 0, black_loss = 0;
        Bitboard w_minors = pos.pieces(WHITE, KNIGHT) | pos.pieces(WHITE, BISHOP);
        Bitboard b_minors = pos.pieces(BLACK, KNIGHT) | pos.pieces(BLACK, BISHOP);
        white_loss += 16 * popcount(w_minors & b_att);
        black_loss += 16 * popcount(b_minors & w_att);
        white_loss += 26 * popcount(pos.pieces(WHITE, ROOK) & b_att);
        black_loss += 26 * popcount(pos.pieces(BLACK, ROOK) & w_att);
        white_loss += 36 * popcount(pos.pieces(WHITE, QUEEN) & b_att);
        black_loss += 36 * popcount(pos.pieces(BLACK, QUEEN) & w_att);

        int w_threat = (us == WHITE) ? (white_loss * 6 / 10) : white_loss;
        int b_threat = (us == BLACK) ? (black_loss * 6 / 10) : black_loss;
        int net = (us == WHITE) ? (b_threat - w_threat) : (w_threat - b_threat);
        out.corrected_eval += net;
    }

    // ── King safety (Stockfish 2.3.1 quadratic danger & shelter) ────────────
    {
        Bitboard occ = pos.occupied();
        static constexpr int InitKingDanger[64] = {
             2,  0,  2,  5,  5,  2,  0,  2,
             2,  2,  4,  8,  8,  4,  2,  2,
             7, 10, 12, 12, 12, 12, 10,  7,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15
        };

        auto king_danger = [&](Color c) -> int {
            Square ksq = pos.king_square(c);
            if (ksq == SQ_NONE) return 0;

            Bitboard opp_queens = pos.pieces(~c, QUEEN);
            Bitboard opp_rooks = pos.pieces(~c, ROOK);
            if (!opp_queens && !opp_rooks) return 0; // King safety only matters with heavy pieces

            Square rel_ksq = (c == WHITE) ? ksq : static_cast<Square>(ksq ^ 56);
            int danger = InitKingDanger[rel_ksq];

            static constexpr int ShelterWeakness[2][8] = {
                { 141, 0, 38, 102, 128, 141, 141, 141 }, // file of king
                {  61, 0, 16,  44,  56,  61,  61,  61 }  // adjacent files
            };

            int shelter_defect = 0;
            File kf = file_of(ksq);
            File ckf = (kf == FILE_A) ? FILE_B : (kf == FILE_H ? FILE_G : kf);
            Bitboard my_pawns = pos.pieces(c, PAWN);
            Bitboard opp_pawns = pos.pieces(~c, PAWN);

            for (int f = ckf - 1; f <= ckf + 1; ++f) {
                Bitboard fmask = 0x0101010101010101ULL << f;
                Bitboard our_f_pawns = my_pawns & fmask;
                int rk_us = 0;
                if (our_f_pawns) {
                    Square s = (c == WHITE) ? lsb(our_f_pawns) : msb(our_f_pawns);
                    rk_us = (c == WHITE) ? rank_of(s) : (7 - rank_of(s));
                }
                shelter_defect += ShelterWeakness[f != ckf][rk_us];

                // Storm danger: enemy pawn marching towards our king
                Bitboard opp_f_pawns = opp_pawns & fmask;
                if (opp_f_pawns) {
                    Square s = (c == WHITE) ? lsb(opp_f_pawns) : msb(opp_f_pawns);
                    int rk_them = (c == WHITE) ? (7 - rank_of(s)) : rank_of(s);
                    if (rk_them <= 3) {
                        shelter_defect += (rk_them == 2 ? 40 : (rk_them == 3 ? 20 : 10));
                    }
                }
            }

            int open_file_penalty = (shelter_defect * 35) / 100;
            danger += shelter_defect / 16;

            // Uncastled king in center with heavy pieces on board
            if (c == WHITE && (ksq == SQ_E1 || ksq == SQ_D1) && total_nonpawn >= 2400) {
                open_file_penalty += 20;
            } else if (c == BLACK && (ksq == SQ_E8 || ksq == SQ_D8) && total_nonpawn >= 2400) {
                open_file_penalty += 20;
            }

            // King Ring attackers
            Bitboard ring = Bitboards::king_attacks(ksq);
            if (c == WHITE && rank_of(ksq) <= RANK_3) ring |= (ring << 8);
            if (c == BLACK && rank_of(ksq) >= RANK_6) ring |= (ring >> 8);

            int attack_units = 0;
            int attackers = 0;

            Bitboard kn = pos.pieces(~c, KNIGHT);
            while (kn) {
                Square s = pop_lsb(kn);
                if (Bitboards::knight_attacks(s) & ring) { attack_units += 2; attackers++; }
            }
            Bitboard bi = pos.pieces(~c, BISHOP);
            while (bi) {
                Square s = pop_lsb(bi);
                if (Bitboards::bishop_attacks(s, occ) & ring) { attack_units += 2; attackers++; }
            }
            Bitboard rk = opp_rooks;
            while (rk) {
                Square s = pop_lsb(rk);
                if (Bitboards::rook_attacks(s, occ) & ring) { attack_units += 3; attackers++; }
            }
            Bitboard qq = opp_queens;
            while (qq) {
                Square s = pop_lsb(qq);
                if (Bitboards::queen_attacks(s, occ) & ring) { attack_units += 5; attackers++; }
            }

            int total_danger = open_file_penalty;
            if (attackers >= 2) {
                danger += attack_units * 2;
                total_danger += std::min(160, static_cast<int>(0.08 * danger * danger));
            }
            return std::min(180, total_danger);
        };

        int white_danger = king_danger(WHITE);
        int black_danger = king_danger(BLACK);
        int net = (us == WHITE) ? (black_danger - white_danger) : (white_danger - black_danger);
        out.corrected_eval += net;
    }

    // ── Piece Mobility (Stockfish 2.3.1 tuned mobility bonuses) ──────────────
    {
        static constexpr int KnightMobility[9][2] = {
            {-38, -33}, {-25, -23}, {-12, -13}, {0, -3}, {12, 7}, {25, 17}, {31, 22}, {38, 27}, {38, 27}
        };
        static constexpr int BishopMobility[16][2] = {
            {-25, -30}, {-11, -16}, {3, -2}, {17, 12}, {31, 26}, {45, 40}, {57, 52}, {65, 60},
            {71, 65}, {74, 69}, {76, 71}, {78, 73}, {79, 74}, {80, 75}, {81, 76}, {81, 76}
        };
        static constexpr int RookMobility[16][2] = {
            {-20, -36}, {-14, -19}, {-8, -3}, {-2, 13}, {4, 29}, {10, 46}, {14, 62}, {19, 79},
            {23, 95}, {26, 106}, {27, 111}, {28, 114}, {29, 116}, {30, 117}, {31, 118}, {32, 118}
        };
        static constexpr int QueenMobility[29][2] = {
            {-10, -18}, {-8, -13}, {-6, -7}, {-3, -2}, {-1, 3}, {1, 8}, {3, 13}, {5, 19},
            {8, 23}, {10, 27}, {12, 32}, {15, 34}, {16, 35}, {17, 35}, {18, 35}, {20, 35},
            {20, 35}, {20, 35}, {20, 35}, {20, 35}, {20, 35}, {20, 35}, {20, 35}, {20, 35},
            {20, 35}, {20, 35}, {20, 35}, {20, 35}, {20, 35}
        };

        Bitboard occ = pos.occupied();
        Bitboard w_pawns = pos.pieces(WHITE, PAWN);
        Bitboard b_pawns = pos.pieces(BLACK, PAWN);
        Bitboard w_pawn_att = ((w_pawns & ~0x0101010101010101ULL) << 7) | ((w_pawns & ~0x8080808080808080ULL) << 9);
        Bitboard b_pawn_att = ((b_pawns & ~0x8080808080808080ULL) >> 7) | ((b_pawns & ~0x0101010101010101ULL) >> 9);

        Bitboard w_mob_area = ~(b_pawn_att | pos.pieces(WHITE));
        Bitboard b_mob_area = ~(w_pawn_att | pos.pieces(BLACK));

        auto compute_mob = [&](Color c, Bitboard area) -> std::pair<int, int> {
            int mg = 0, eg = 0;
            Bitboard kn = pos.pieces(c, KNIGHT);
            while (kn) {
                Square s = pop_lsb(kn);
                int m = std::min(8, popcount(Bitboards::knight_attacks(s) & area));
                mg += KnightMobility[m][0];
                eg += KnightMobility[m][1];
            }
            Bitboard bi = pos.pieces(c, BISHOP);
            while (bi) {
                Square s = pop_lsb(bi);
                int m = std::min(15, popcount(Bitboards::bishop_attacks(s, occ) & area));
                mg += BishopMobility[m][0];
                eg += BishopMobility[m][1];
            }
            Bitboard rk = pos.pieces(c, ROOK);
            while (rk) {
                Square s = pop_lsb(rk);
                int m = std::min(15, popcount(Bitboards::rook_attacks(s, occ) & area));
                mg += RookMobility[m][0];
                eg += RookMobility[m][1];
            }
            Bitboard qq = pos.pieces(c, QUEEN);
            while (qq) {
                Square s = pop_lsb(qq);
                int m = std::min(28, popcount(Bitboards::queen_attacks(s, occ) & area));
                mg += QueenMobility[m][0];
                eg += QueenMobility[m][1];
            }
            return {mg, eg};
        };

        auto [w_mg, w_eg] = compute_mob(WHITE, w_mob_area);
        auto [b_mg, b_eg] = compute_mob(BLACK, b_mob_area);

        int non_pawn = pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK);
        int phase = std::clamp((non_pawn - 800) * 128 / (6400 - 800), 0, 128);
        int mg_diff = (w_mg - b_mg) * 100 / 198;
        int eg_diff = (w_eg - b_eg) * 100 / 258;
        int mob_val = (mg_diff * phase + eg_diff * (128 - phase)) / 128;
        out.corrected_eval += (us == WHITE) ? mob_val : -mob_val;
    }

    // ── Known Endgame Rules (KNNK, Lone King Mating Distance) ────────────────
    // Known Endgame Rules & Scale Factors (Stockfish 2.3.1)
    {
        int w_mat = pos.non_pawn_material(WHITE);
        int b_mat = pos.non_pawn_material(BLACK);
        int w_pawns = popcount(pos.pieces(WHITE, PAWN));
        int b_pawns = popcount(pos.pieces(BLACK, PAWN));
        int total_pawns = w_pawns + b_pawns;

        // 1. Opposite-Colored Bishops Endgame Scaling (Stockfish 2.3.1 scale factors 8 to 36 / 128)
        Bitboard w_bishops = pos.pieces(WHITE, BISHOP);
        Bitboard b_bishops = pos.pieces(BLACK, BISHOP);
        if (popcount(w_bishops) == 1 && popcount(b_bishops) == 1) {
            constexpr Bitboard DarkSquares = 0xAA55AA55AA55AA55ULL;
            bool w_dark = (w_bishops & DarkSquares) != 0;
            bool b_dark = (b_bishops & DarkSquares) != 0;
            if (w_dark != b_dark) {
                if (w_mat == 330 && b_mat == 330) {
                    int sf = (total_pawns <= 1) ? 8 : (total_pawns <= 3 ? 24 : 36);
                    out.corrected_eval = static_cast<Value>(out.corrected_eval * sf / 128);
                } else if (total_nonpawn <= 2000) {
                    out.corrected_eval = static_cast<Value>(out.corrected_eval * 50 / 128);
                }
            }
        }

        // 2. Insufficient Material & Pawnless Endgame Rules
        if (total_pawns == 0) {
            if ((w_mat == 0 && popcount(pos.pieces(BLACK, KNIGHT)) == 2 && b_mat <= 660) ||
                (b_mat == 0 && popcount(pos.pieces(WHITE, KNIGHT)) == 2 && w_mat <= 660)) {
                out.corrected_eval = 0; // KNNK forced draw
            } else if ((w_mat <= 330 && b_mat == 0) || (b_mat <= 330 && w_mat == 0)) {
                out.corrected_eval = 0; // KB vs K or KN vs K forced draw
            } else if (w_mat <= 330 && b_mat <= 330) {
                out.corrected_eval = 0; // Minor vs Minor (KB vs KN, etc.) forced draw
            } else if ((w_mat == 500 && b_mat == 500) || (w_mat == 900 && b_mat == 900)) {
                out.corrected_eval = static_cast<Value>(out.corrected_eval * 16 / 128);
            } else {
                int diff = std::abs(w_mat - b_mat);
                if (diff <= 330) {
                    out.corrected_eval = static_cast<Value>(out.corrected_eval * 16 / 128);
                }
            }
        }

        if ((total_pawns == 0 || total_nonpawn <= 1200) && std::abs(w_mat - b_mat) >= 300) {
            Color strong = (w_mat > b_mat) ? WHITE : BLACK;
            Color weak = ~strong;
            Square weak_k = pos.king_square(weak);
            Square strong_k = pos.king_square(strong);
            if (weak_k != SQ_NONE && strong_k != SQ_NONE) {
                int dist_center = std::max(std::abs(2 * rank_of(weak_k) - 7), std::abs(2 * file_of(weak_k) - 7));
                int king_dist = std::max(std::abs(rank_of(weak_k) - rank_of(strong_k)), std::abs(file_of(weak_k) - file_of(strong_k)));
                int mating_bonus = dist_center * 8 - king_dist * 4;
                if (us == strong) out.corrected_eval += mating_bonus;
                else out.corrected_eval -= mating_bonus;
            }
        }
    }

    // ── Piece-Square Tables (Stockfish 2.3.1 tuned positional placement) ─────
    {
        int mg_w = 0, eg_w = 0;
        int mg_b = 0, eg_b = 0;

        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard wb = pos.pieces(WHITE, static_cast<PieceType>(pt));
            while (wb) {
                Square s = pop_lsb(wb);
                mg_w += PSQT[pt][s].mg;
                eg_w += PSQT[pt][s].eg;
            }
            Bitboard bb = pos.pieces(BLACK, static_cast<PieceType>(pt));
            while (bb) {
                Square s = pop_lsb(bb);
                Square flipped = static_cast<Square>(s ^ 56);
                mg_b += PSQT[pt][flipped].mg;
                eg_b += PSQT[pt][flipped].eg;
            }
        }

        int non_pawn = pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK);
        int phase = std::clamp((non_pawn - 800) * 128 / (6400 - 800), 0, 128);
        int mg_diff = mg_w - mg_b;
        int eg_diff = eg_w - eg_b;
        int psqt_diff = (mg_diff * phase + eg_diff * (128 - phase)) / (2 * 128);
        int psqt_side = (us == WHITE) ? psqt_diff : -psqt_diff;
        out.corrected_eval += psqt_side;
    }

    // ── Minor Piece Outposts (central activity & pawn support) ───────────────
    {
        auto outpost_bonus = [&](Color c) -> int {
            int bonus = 0;
            Bitboard minors = pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP);
            Bitboard my_pawns = pos.pieces(c, PAWN);
            Bitboard opp_pawns = pos.pieces(~c, PAWN);
            while (minors) {
                Square s = pop_lsb(minors);
                PieceType pt = type_of(pos.piece_on(s));
                int r = (c == WHITE) ? rank_of(s) : (7 - rank_of(s));
                if (r >= RANK_4 && r <= RANK_6) {
                    Bitboard defending = my_pawns & Bitboards::pawn_attacks(~c, s);
                    if (defending) {
                        int f = file_of(s);
                        Bitboard adj_files = 0;
                        if (f > 0) adj_files |= (0x0101010101010101ULL << (f - 1));
                        if (f < 7) adj_files |= (0x0101010101010101ULL << (f + 1));
                        Bitboard forward_mask = (c == WHITE)
                            ? ~((1ULL << (8 * (rank_of(s) + 1))) - 1)
                            : ((1ULL << (8 * rank_of(s))) - 1);
                        bool unassailable = (opp_pawns & adj_files & forward_mask) == 0;
                        if (pt == KNIGHT) {
                            if (unassailable) {
                                bonus += (r == RANK_6) ? 55 : (r == RANK_5 ? 35 : 18);
                            } else {
                                bonus += 12;
                            }
                        } else {
                            if (unassailable) {
                                bonus += (r == RANK_6) ? 35 : (r == RANK_5 ? 24 : 12);
                            } else {
                                bonus += 8;
                            }
                        }
                    }
                }
            }
            return bonus;
        };
        int outpost_net = outpost_bonus(WHITE) - outpost_bonus(BLACK);
        out.corrected_eval += (us == WHITE) ? outpost_net : -outpost_net;
    }

    // ── Trapped Bishops (Stockfish 2.3.1) ────────────────────────────────────
    {
        auto trapped_bishop_penalty = [&](Color c) -> int {
            int pen = 0;
            Bitboard bishops = pos.pieces(c, BISHOP);
            Bitboard my_pawns = pos.pieces(c, PAWN);
            while (bishops) {
                Square s = pop_lsb(bishops);
                if (c == WHITE) {
                    if (s == SQ_A2 && (my_pawns & square_bb(SQ_B3))) pen += 70;
                    else if (s == SQ_B1 && (my_pawns & square_bb(SQ_C2))) pen += 50;
                    else if (s == SQ_H2 && (my_pawns & square_bb(SQ_G3))) pen += 70;
                } else {
                    if (s == SQ_A7 && (my_pawns & square_bb(SQ_B6))) pen += 70;
                    else if (s == SQ_B8 && (my_pawns & square_bb(SQ_C7))) pen += 50;
                    else if (s == SQ_H7 && (my_pawns & square_bb(SQ_G6))) pen += 70;
                }
            }
            return pen;
        };
        int trap_net = trapped_bishop_penalty(BLACK) - trapped_bishop_penalty(WHITE);
        out.corrected_eval += (us == WHITE) ? trap_net : -trap_net;
    }



    return out;
}

SSFZREOutput SSFZREEngine::evaluate_and_correct(const Position& pos, Value raw_eval_cp,
                                                Key root_pawn_key, Key root_material_key,
                                                int current_ply) const {
    (void)root_pawn_key;
    (void)root_material_key;
    (void)current_ply;
    return correct(pos, raw_eval_cp);
}

} // namespace NextFish
