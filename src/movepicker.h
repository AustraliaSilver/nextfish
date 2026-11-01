#pragma once

#include "types.h"
#include "position.h"
#include "movegen.h"
#include "hamo.h"

namespace NextFish {

enum MovePickerStage {
    MP_STAGE_TT,
    MP_STAGE_GEN_CAPTURES,
    MP_STAGE_CAPTURES,
    MP_STAGE_KILLER1,
    MP_STAGE_KILLER2,
    MP_STAGE_COUNTER,
    MP_STAGE_GEN_QUIETS,
    MP_STAGE_QUIETS,
    MP_STAGE_BAD_CAPTURES,
    MP_STAGE_EVASIONS,
    MP_STAGE_DONE
};

class MovePicker {
public:
    MovePicker(const Position& p, Move tt, Move k1, Move k2, Move prev, bool in_check)
        : pos(p), tt_m(tt), killer1(k1), killer2(k2), prev_m(prev), is_in_check(in_check) {
        if (is_in_check) {
            stage = MP_STAGE_EVASIONS;
            generate_moves<EVASIONS>(pos, moves);
            score_all();
        } else {
            stage = MP_STAGE_TT;
        }
    }

    Move next_move() {
        if (is_in_check) {
            while (cur_idx < moves.size()) {
                int best_i = cur_idx;
                for (int j = cur_idx + 1; j < moves.size(); ++j) {
                    if (scores[j] > scores[best_i]) best_i = j;
                }
                std::swap(moves[cur_idx], moves[best_i]);
                std::swap(scores[cur_idx], scores[best_i]);
                Move m = moves[cur_idx++];
                if (pos.is_legal(m)) return m;
            }
            return MOVE_NONE;
        }

        switch (stage) {
            case MP_STAGE_TT:
                stage = MP_STAGE_GEN_CAPTURES;
                if (tt_m.is_valid() && pos.is_pseudo_legal(tt_m) && pos.is_legal(tt_m)) {
                    return tt_m;
                }
                [[fallthrough]];

            case MP_STAGE_GEN_CAPTURES: {
                stage = MP_STAGE_CAPTURES;
                generate_moves<CAPTURES>(pos, moves);
                constexpr int PieceVals[] = { 100, 320, 330, 500, 900, 20000, 0 };
                for (int i = 0; i < moves.size(); ++i) {
                    Move m = moves[i];
                    Square to = m.to();
                    Square from = m.from();
                    Piece vic = (m.type() == EN_PASSANT) ? make_piece(~pos.side_to_move(), PAWN) : pos.piece_on(to);
                    Piece att = pos.piece_on(from);
                    int v_val = (vic != NO_PIECE) ? PieceVals[type_of(vic)] : 100;
                    int a_val = (att != NO_PIECE) ? PieceVals[type_of(att)] : 100;
                    int cap_hist = (att < 12 && vic != NO_PIECE) ? GlobalHAMO.get_capture_history(att, to, type_of(vic)) : 0;
                    scores[i] = v_val * 10 - a_val + cap_hist + (m.type() == PROMOTION ? 800 : 0);
                }
                cur_idx = 0;
                [[fallthrough]];
            }

            case MP_STAGE_CAPTURES: {
                while (cur_idx < moves.size()) {
                    int best_i = cur_idx;
                    for (int j = cur_idx + 1; j < moves.size(); ++j) {
                        if (scores[j] > scores[best_i]) best_i = j;
                    }
                    std::swap(moves[cur_idx], moves[best_i]);
                    std::swap(scores[cur_idx], scores[best_i]);
                    Move m = moves[cur_idx++];
                    if (m != tt_m && pos.is_legal(m)) {
                        // Defer losing captures (SEE < 0) until after quiet moves
                        if (m.type() != PROMOTION && pos.see(m, 0) < 0) {
                            if (bad_cap_count < 32) {
                                bad_captures[bad_cap_count++] = m;
                            }
                            continue;
                        }
                        return m;
                    }
                }
                stage = MP_STAGE_KILLER1;
                [[fallthrough]];
            }

            case MP_STAGE_KILLER1:
                stage = MP_STAGE_KILLER2;
                if (killer1.is_valid() && killer1 != tt_m && pos.is_pseudo_legal(killer1) && pos.is_legal(killer1)) {
                    return killer1;
                }
                [[fallthrough]];

            case MP_STAGE_KILLER2:
                stage = MP_STAGE_COUNTER;
                if (killer2.is_valid() && killer2 != tt_m && killer2 != killer1 && pos.is_pseudo_legal(killer2) && pos.is_legal(killer2)) {
                    return killer2;
                }
                [[fallthrough]];

            case MP_STAGE_COUNTER: {
                stage = MP_STAGE_GEN_QUIETS;
                counter_m = GlobalHAMO.get_counter_move(pos, prev_m);
                if (counter_m.is_valid() && counter_m != tt_m && counter_m != killer1 && counter_m != killer2
                    && pos.is_pseudo_legal(counter_m) && pos.is_legal(counter_m)) {
                    return counter_m;
                }
                [[fallthrough]];
            }

            case MP_STAGE_GEN_QUIETS: {
                stage = MP_STAGE_QUIETS;
                moves.clear();
                generate_moves<QUIETS>(pos, moves);
                for (int i = 0; i < moves.size(); ++i) {
                    Move m = moves[i];
                    int score = GlobalHAMO.get_history(pos, m, prev_m);
                    if (pos.gives_check(m)) score += 3000;
                    if (m.type() == CASTLING) score += 2000; // Prioritize King safety via castling!
                    scores[i] = score;
                }
                cur_idx = 0;
                [[fallthrough]];
            }

            case MP_STAGE_QUIETS: {
                while (cur_idx < moves.size()) {
                    int best_i = cur_idx;
                    for (int j = cur_idx + 1; j < moves.size(); ++j) {
                        if (scores[j] > scores[best_i]) best_i = j;
                    }
                    std::swap(moves[cur_idx], moves[best_i]);
                    std::swap(scores[cur_idx], scores[best_i]);
                    Move m = moves[cur_idx++];
                    if (m != tt_m && m != killer1 && m != killer2 && m != counter_m && pos.is_legal(m)) return m;
                }
                stage = MP_STAGE_BAD_CAPTURES;
                cur_idx = 0;
                [[fallthrough]];
            }

            case MP_STAGE_BAD_CAPTURES: {
                while (cur_idx < bad_cap_count) {
                    Move m = bad_captures[cur_idx++];
                    return m;
                }
                stage = MP_STAGE_DONE;
                return MOVE_NONE;
            }

            case MP_STAGE_DONE:
            default:
                return MOVE_NONE;
        }
    }

private:
    void score_all() {
        constexpr int PieceVals[] = { 100, 320, 330, 500, 900, 20000, 0 };
        for (int i = 0; i < moves.size(); ++i) {
            Move m = moves[i];
            if (m == tt_m) {
                scores[i] = 10000000;
            } else {
                Square to = m.to();
                Piece target = pos.piece_on(to);
                if (target != NO_PIECE) {
                    scores[i] = 100000 + PieceVals[type_of(target)] * 10 - PieceVals[type_of(pos.piece_on(m.from()))] + (m.type() == PROMOTION ? 800 : 0);
                } else if (m.type() == PROMOTION) {
                    scores[i] = 95000;
                } else if (m == killer1) {
                    scores[i] = 90000;
                } else if (m == killer2) {
                    scores[i] = 80000;
                } else {
                    scores[i] = GlobalHAMO.get_history(pos, m, prev_m);
                }
            }
        }
        cur_idx = 0;
    }

    const Position& pos;
    Move tt_m;
    Move killer1;
    Move killer2;
    Move counter_m = MOVE_NONE;
    Move prev_m;
    bool is_in_check;
    MovePickerStage stage;
    MoveList moves;
    int scores[256];
    int cur_idx = 0;
    Move bad_captures[32];
    int bad_cap_count = 0;
};

} // namespace NextFish
