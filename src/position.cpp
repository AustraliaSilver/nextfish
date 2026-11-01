#include "position.h"
#include "harenn.h"
#include <sstream>
#include <cctype>
#include <random>
#include <cstring>

namespace NextFish {

static Key ZobristPiece[12][64];
static Key ZobristCastling[16];
static Key ZobristEp[65];
static Key ZobristSide;
static Key ZobristPawn[12][64];
static Key ZobristMaterial[12][16];

// Castling path masks
constexpr Bitboard WHITE_OO_PATH = (1ULL << SQ_F1) | (1ULL << SQ_G1);
constexpr Bitboard WHITE_OOO_PATH = (1ULL << SQ_B1) | (1ULL << SQ_C1) | (1ULL << SQ_D1);
constexpr Bitboard BLACK_OO_PATH = (1ULL << SQ_F8) | (1ULL << SQ_G8);
constexpr Bitboard BLACK_OOO_PATH = (1ULL << SQ_B8) | (1ULL << SQ_C8) | (1ULL << SQ_D8);

// Castling rights update mask for square changes
static int CastlingSquareMask[64];

void init_zobrist() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    std::mt19937_64 rng(1070372ULL);

    for (int pc = 0; pc < 12; ++pc) {
        for (int sq = 0; sq < 64; ++sq) {
            ZobristPiece[pc][sq] = rng();
            ZobristPawn[pc][sq] = (pc == W_PAWN || pc == B_PAWN) ? rng() : 0;
        }
    }

    for (int i = 0; i < 16; ++i) {
        ZobristCastling[i] = rng();
    }

    for (int i = 0; i <= 64; ++i) {
        ZobristEp[i] = rng();
    }

    ZobristSide = rng();

    for (int pc = 0; pc < 12; ++pc) {
        ZobristMaterial[pc][0] = 0;
        for (int count = 1; count < 16; ++count) {
            ZobristMaterial[pc][count] = rng();
        }
    }

    for (int sq = 0; sq < 64; ++sq) {
        CastlingSquareMask[sq] = ALL_CASTLING;
    }
    CastlingSquareMask[SQ_A1] &= ~WHITE_OOO;
    CastlingSquareMask[SQ_E1] &= ~WHITE_CASTLING;
    CastlingSquareMask[SQ_H1] &= ~WHITE_OO;
    CastlingSquareMask[SQ_A8] &= ~BLACK_OOO;
    CastlingSquareMask[SQ_E8] &= ~BLACK_CASTLING;
    CastlingSquareMask[SQ_H8] &= ~BLACK_OO;
}

Position::Position() {
    init_zobrist();
    set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

Position::Position(const std::string& fen) {
    init_zobrist();
    set(fen);
}

void Position::put_piece(Piece pc, Square s) {
    board[s] = pc;
    by_color[color_of(pc)] |= square_bb(s);
    by_type[type_of(pc)] |= square_bb(s);
}

void Position::remove_piece(Square s) {
    Piece pc = board[s];
    board[s] = NO_PIECE;
    by_color[color_of(pc)] &= ~square_bb(s);
    by_type[type_of(pc)] &= ~square_bb(s);
}

void Position::move_piece(Square from, Square to) {
    Piece pc = board[from];
    board[from] = NO_PIECE;
    board[to] = pc;
    Bitboard from_to = square_bb(from) | square_bb(to);
    by_color[color_of(pc)] ^= from_to;
    by_type[type_of(pc)] ^= from_to;
}

void Position::set(const std::string& fen_str) {
    for (int i = 0; i < COLOR_NB; ++i) by_color[i] = 0;
    for (int i = 0; i < PIECE_TYPE_NB; ++i) by_type[i] = 0;
    for (int i = 0; i < 64; ++i) board[i] = NO_PIECE;

    std::istringstream ss(fen_str);
    std::string pieces_part, side_part, castling_part, ep_part;
    int halfmove = 0, fullmove = 1;

    ss >> pieces_part >> side_part >> castling_part >> ep_part >> halfmove >> fullmove;

    int r = 7, f = 0;
    for (char c : pieces_part) {
        if (c == '/') {
            r--;
            f = 0;
        } else if (std::isdigit(c)) {
            f += c - '0';
        } else {
            Square sq = make_square(static_cast<File>(f), static_cast<Rank>(r));
            Color c_color = std::isupper(c) ? WHITE : BLACK;
            char lower = std::tolower(c);
            PieceType pt = NO_PIECE_TYPE;
            switch (lower) {
                case 'p': pt = PAWN; break;
                case 'n': pt = KNIGHT; break;
                case 'b': pt = BISHOP; break;
                case 'r': pt = ROOK; break;
                case 'q': pt = QUEEN; break;
                case 'k': pt = KING; break;
            }
            put_piece(make_piece(c_color, pt), sq);
            f++;
        }
    }

    stm = (side_part == "b") ? BLACK : WHITE;

    int castling = NO_CASTLING;
    if (castling_part.find('K') != std::string::npos) castling |= WHITE_OO;
    if (castling_part.find('Q') != std::string::npos) castling |= WHITE_OOO;
    if (castling_part.find('k') != std::string::npos) castling |= BLACK_OO;
    if (castling_part.find('q') != std::string::npos) castling |= BLACK_OOO;

    Square ep_sq = SQ_NONE;
    if (ep_part.length() == 2 && ep_part[0] >= 'a' && ep_part[0] <= 'h') {
        File ep_f = static_cast<File>(ep_part[0] - 'a');
        Rank ep_r = static_cast<Rank>(ep_part[1] - '1');
        ep_sq = make_square(ep_f, ep_r);
    }

    ply = (fullmove - 1) * 2 + (stm == BLACK ? 1 : 0);

    state = &start_state;
    state->castling_rights = castling;
    state->ep_square = ep_sq;
    state->halfmove_clock = halfmove;
    state->captured_piece = NO_PIECE;
    state->previous = nullptr;
    if (GlobalHARENN.is_loaded()) {
        if (GlobalHARENN.is_halfka()) {
            GlobalHARENN.compute_accumulator_halfka(*this, &state->accumulator[0], &state->accumulator[512]);
        } else {
            GlobalHARENN.compute_accumulator(*this, state->accumulator);
        }
        state->acc_computed = true;
    } else {
        state->acc_computed = false;
    }

    set_state();
}

const int32_t* Position::accumulator() const {
    if (!state->acc_computed) {
        if (GlobalHARENN.is_loaded()) {
            if (GlobalHARENN.is_halfka()) {
                StateInfo* path[16];
                int path_len = 0;
                StateInfo* cur = const_cast<StateInfo*>(state);
                bool has_king_move = false;

                while (cur && !cur->acc_computed && path_len < 12) {
                    if (cur->moved_piece != NO_PIECE && type_of(cur->moved_piece) == KING) {
                        has_king_move = true;
                    }
                    path[path_len++] = cur;
                    cur = cur->previous;
                }

                if (cur && cur->acc_computed && !has_king_move) {
                    alignas(64) int32_t acc[1024];
                    std::memcpy(acc, cur->accumulator, 1024 * sizeof(int32_t));
                    int32_t* acc_w = &acc[0];
                    int32_t* acc_b = &acc[512];

                    Square w_ksq = king_square(WHITE);
                    Square b_ksq = king_square(BLACK);

                    for (int i = path_len - 1; i >= 0; --i) {
                        StateInfo* st = path[i];
                        Move m = st->last_move;
                        if (m.is_valid()) {
                            Square from = m.from();
                            Square to = m.to();
                            Piece pc = st->moved_piece;
                            Color mover = color_of(pc);
                            bool use_hkb = GlobalHARENN.is_hkb();

                            GlobalHARENN.sub_feature_halfka(acc_w, HARENNEvaluator::make_feature(w_ksq, pc, from, WHITE, use_hkb));
                            GlobalHARENN.sub_feature_halfka(acc_b, HARENNEvaluator::make_feature(b_ksq, pc, from, BLACK, use_hkb));

                            if (st->captured_piece != NO_PIECE) {
                                Square cap_sq = to;
                                if (m.type() == EN_PASSANT) {
                                    cap_sq = make_square(file_of(to), rank_of(from));
                                }
                                GlobalHARENN.sub_feature_halfka(acc_w, HARENNEvaluator::make_feature(w_ksq, st->captured_piece, cap_sq, WHITE, use_hkb));
                                GlobalHARENN.sub_feature_halfka(acc_b, HARENNEvaluator::make_feature(b_ksq, st->captured_piece, cap_sq, BLACK, use_hkb));
                            }

                            Piece to_pc = (m.type() == PROMOTION) ? make_piece(mover, m.promo_type()) : pc;
                            GlobalHARENN.add_feature_halfka(acc_w, HARENNEvaluator::make_feature(w_ksq, to_pc, to, WHITE, use_hkb));
                            GlobalHARENN.add_feature_halfka(acc_b, HARENNEvaluator::make_feature(b_ksq, to_pc, to, BLACK, use_hkb));
                        }
                        std::memcpy(st->accumulator, acc, 1024 * sizeof(int32_t));
                        st->acc_computed = true;
                    }
                } else {
                    GlobalHARENN.compute_accumulator_halfka(*this, &state->accumulator[0], &state->accumulator[512]);
                    state->acc_computed = true;
                }
            } else {
                StateInfo* path[16];
                int path_len = 0;
                StateInfo* cur = const_cast<StateInfo*>(state);
                while (cur && !cur->acc_computed && path_len < 12) {
                    path[path_len++] = cur;
                    cur = cur->previous;
                }

                if (cur && cur->acc_computed) {
                    alignas(64) int32_t acc[1024];
                    std::memcpy(acc, cur->accumulator, 1024 * sizeof(int32_t));

                    for (int i = path_len - 1; i >= 0; --i) {
                        StateInfo* st = path[i];
                        Move m = st->last_move;
                        if (m.is_valid()) {
                            Square from = m.from();
                            Square to = m.to();
                            Piece pc = st->moved_piece;
                            Color mover = color_of(pc);
                            Square py_from = flip_rank(from);
                            int feat_removed = py_from * 12 + static_cast<int>(pc);
                            GlobalHARENN.sub_feature(acc, feat_removed);

                            if (st->captured_piece != NO_PIECE) {
                                Square cap_sq = to;
                                if (m.type() == EN_PASSANT) {
                                    cap_sq = make_square(file_of(to), rank_of(from));
                                }
                                Square py_cap = flip_rank(cap_sq);
                                int feat_cap = py_cap * 12 + static_cast<int>(st->captured_piece);
                                GlobalHARENN.sub_feature(acc, feat_cap);
                            }

                            Square py_to = flip_rank(to);
                            Piece to_pc = (m.type() == PROMOTION) ? make_piece(mover, m.promo_type()) : pc;
                            int feat_added = py_to * 12 + static_cast<int>(to_pc);
                            GlobalHARENN.add_feature(acc, feat_added);

                            if (m.type() == CASTLING) {
                                Square r_from, r_to;
                                if (to > from) {
                                    r_from = make_square(FILE_H, rank_of(from));
                                    r_to = make_square(FILE_F, rank_of(from));
                                } else {
                                    r_from = make_square(FILE_A, rank_of(from));
                                    r_to = make_square(FILE_D, rank_of(from));
                                }
                                Piece rook = make_piece(mover, ROOK);
                                GlobalHARENN.sub_feature(acc, flip_rank(r_from) * 12 + static_cast<int>(rook));
                                GlobalHARENN.add_feature(acc, flip_rank(r_to) * 12 + static_cast<int>(rook));
                            }
                        }
                        std::memcpy(st->accumulator, acc, 1024 * sizeof(int32_t));
                        st->acc_computed = true;
                    }
                } else {
                    GlobalHARENN.compute_accumulator(*this, state->accumulator);
                    state->acc_computed = true;
                }
            }
        } else {
            std::memset(state->accumulator, 0, 1024 * sizeof(int32_t));
            state->acc_computed = true;
        }
    }
    return state->accumulator;
}

void Position::set_state() {
    Key k = 0, pk = 0, mk = 0;
    int counts[12] = {0};

    for (int sq = 0; sq < 64; ++sq) {
        Piece pc = board[sq];
        if (pc != NO_PIECE) {
            k ^= ZobristPiece[pc][sq];
            if (pc == W_PAWN || pc == B_PAWN) {
                pk ^= ZobristPawn[pc][sq];
            }
            counts[pc]++;
        }
    }

    for (int pc = 0; pc < 12; ++pc) {
        if (counts[pc] > 0) {
            mk ^= ZobristMaterial[pc][std::min(15, counts[pc])];
        }
    }

    if (stm == BLACK) k ^= ZobristSide;
    k ^= ZobristCastling[state->castling_rights];
    if (state->ep_square != SQ_NONE) k ^= ZobristEp[state->ep_square];

    state->key = k;
    state->pawn_key = pk;
    state->material_key = mk;

    // Checkers
    Square ksq = king_square(stm);
    state->checkers = attackers_to(ksq, occupied()) & pieces(~stm);
    state->blockers_for_king[stm] = blockers_for_king(stm);
    state->blockers_for_king[~stm] = blockers_for_king(~stm);
}

std::string Position::fen() const {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        int empty_cnt = 0;
        for (int f = 0; f < 8; ++f) {
            Square sq = make_square(static_cast<File>(f), static_cast<Rank>(r));
            Piece pc = board[sq];
            if (pc == NO_PIECE) {
                empty_cnt++;
            } else {
                if (empty_cnt > 0) {
                    s += std::to_string(empty_cnt);
                    empty_cnt = 0;
                }
                const char names[] = "PNBRQKpnbrqk";
                s += names[pc];
            }
        }
        if (empty_cnt > 0) s += std::to_string(empty_cnt);
        if (r > 0) s += '/';
    }

    s += (stm == WHITE ? " w " : " b ");

    std::string castling;
    if (state->castling_rights & WHITE_OO) castling += 'K';
    if (state->castling_rights & WHITE_OOO) castling += 'Q';
    if (state->castling_rights & BLACK_OO) castling += 'k';
    if (state->castling_rights & BLACK_OOO) castling += 'q';
    if (castling.empty()) castling = "-";
    s += castling;

    s += " ";
    s += (state->ep_square != SQ_NONE ? square_to_string(state->ep_square) : "-");
    s += " " + std::to_string(state->halfmove_clock);
    s += " " + std::to_string((ply / 2) + 1);

    return s;
}

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    if (s < 0 || s >= 64) return 0ULL;
    return (Bitboards::pawn_attacks(WHITE, s) & pieces(BLACK, PAWN))
         | (Bitboards::pawn_attacks(BLACK, s) & pieces(WHITE, PAWN))
         | (Bitboards::knight_attacks(s) & pieces(KNIGHT))
         | (Bitboards::bishop_attacks(s, occ) & (pieces(BISHOP) | pieces(QUEEN)))
         | (Bitboards::rook_attacks(s, occ) & (pieces(ROOK) | pieces(QUEEN)))
         | (Bitboards::king_attacks(s) & pieces(KING));
}

Bitboard Position::blockers_for_king(Color c) const {
    Square ksq = king_square(c);
    Bitboard blockers = 0;
    Bitboard snipers = (Bitboards::bishop_attacks(ksq, 0) & (pieces(~c, BISHOP) | pieces(~c, QUEEN)))
                     | (Bitboards::rook_attacks(ksq, 0) & (pieces(~c, ROOK) | pieces(~c, QUEEN)));

    Bitboard occ = occupied();
    while (snipers) {
        Square sniper_sq = pop_lsb(snipers);
        Bitboard b = Bitboards::between_bb(ksq, sniper_sq) & occ;
        if (popcount(b) == 1) {
            blockers |= b;
        }
    }
    return blockers;
}

bool Position::is_legal(Move m) const {
    if (!m.is_valid()) return false;
    Square from = m.from();
    Square to = m.to();
    if (from >= SQ_NONE || to >= SQ_NONE || from == to) return false;
    Piece pc = board[from];
    Color us = stm;
    if (pc == NO_PIECE || color_of(pc) != us) return false;
    Square ksq = king_square(us);
    Square opp_k = king_square(~us);
    if (to == opp_k) return false;

    if (m.type() == EN_PASSANT) {
        Square cap_sq = make_square(file_of(to), rank_of(from));
        Bitboard occ = (occupied() ^ square_bb(from) ^ square_bb(cap_sq)) | square_bb(to);
        Bitboard opp_pawns = pieces(~us, PAWN) ^ square_bb(cap_sq);
        Bitboard pawn_att = (us == WHITE) ? (Bitboards::pawn_attacks(WHITE, ksq) & opp_pawns)
                                          : (Bitboards::pawn_attacks(BLACK, ksq) & opp_pawns);
        if (pawn_att) return false;
        if (Bitboards::knight_attacks(ksq) & pieces(~us, KNIGHT)) return false;
        if (Bitboards::bishop_attacks(ksq, occ) & (pieces(~us, BISHOP) | pieces(~us, QUEEN))) return false;
        if (Bitboards::rook_attacks(ksq, occ) & (pieces(~us, ROOK) | pieces(~us, QUEEN))) return false;
        if (Bitboards::king_attacks(ksq) & pieces(~us, KING)) return false;
        return true;
    }

    if (m.type() == CASTLING) {
        // Cannot castle out of check
        if (is_in_check()) return false;
        // The rook must actually be present on its original square
        Square r_from = (to > from) ? make_square(FILE_H, rank_of(from)) : make_square(FILE_A, rank_of(from));
        if (board[r_from] != make_piece(us, ROOK)) return false;
        // Cannot castle through check
        Square mid = static_cast<Square>((from + to) / 2);
        if (attackers_to(mid, occupied()) & pieces(~us)) return false;
        if (attackers_to(to, occupied()) & pieces(~us)) return false;
        return true;
    }

    if (type_of(pc) == KING) {
        Bitboard occ_without_king = occupied() ^ square_bb(from);
        return !(attackers_to(to, occ_without_king) & pieces(~us));
    }

    Bitboard pinned = state->blockers_for_king[us] & pieces(us);
    if (pinned & square_bb(from)) {
        return Bitboards::line_bb(ksq, to) & square_bb(from);
    }

    if (is_in_check()) {
        // Must block check or capture checking piece
        if (popcount(state->checkers) > 1) {
            return false; // Double check requires king move
        }
        Square check_sq = lsb(state->checkers);
        Bitboard target_mask = square_bb(check_sq) | Bitboards::between_bb(ksq, check_sq);
        return (square_bb(to) & target_mask) != 0;
    }

    return true;
}

bool Position::is_pseudo_legal(Move m) const {
    if (!m.is_valid()) return false;
    Square from = m.from();
    Square to = m.to();
    if (from >= SQ_NONE || to >= SQ_NONE || from == to) return false;
    Piece pc = board[from];
    if (pc == NO_PIECE || color_of(pc) != stm) return false;

    Piece dest = board[to];
    if (dest != NO_PIECE && color_of(dest) == stm) return false;

    PieceType pt = type_of(pc);
    MoveType mt = m.type();

    if (mt == NORMAL) {
        if (pt == PAWN) {
            if (stm == WHITE) {
                if (rank_of(to) == RANK_8) return false; // Must be promotion
                if (to == from + 8) return dest == NO_PIECE;
                if (to == from + 16 && rank_of(from) == RANK_2) {
                    return dest == NO_PIECE && board[from + 8] == NO_PIECE;
                }
                if ((to == from + 7 || to == from + 9) && dest != NO_PIECE) {
                    return std::abs(file_of(to) - file_of(from)) == 1;
                }
            } else {
                if (rank_of(to) == RANK_1) return false; // Must be promotion
                if (to == from - 8) return dest == NO_PIECE;
                if (to == from - 16 && rank_of(from) == RANK_7) {
                    return dest == NO_PIECE && board[from - 8] == NO_PIECE;
                }
                if ((to == from - 7 || to == from - 9) && dest != NO_PIECE) {
                    return std::abs(file_of(to) - file_of(from)) == 1;
                }
            }
            return false;
        }
        return (Bitboards::attacks_bb(pt, from, occupied()) & square_bb(to)) != 0;
    } else if (mt == PROMOTION) {
        if (pt != PAWN) return false;
        if (stm == WHITE) {
            if (rank_of(from) != RANK_7 || rank_of(to) != RANK_8) return false;
            if (to == from + 8) return dest == NO_PIECE;
            if (to == from + 7 || to == from + 9) {
                return dest != NO_PIECE && std::abs(file_of(to) - file_of(from)) == 1;
            }
        } else {
            if (rank_of(from) != RANK_2 || rank_of(to) != RANK_1) return false;
            if (to == from - 8) return dest == NO_PIECE;
            if (to == from - 7 || to == from - 9) {
                return dest != NO_PIECE && std::abs(file_of(to) - file_of(from)) == 1;
            }
        }
        return false;
    } else if (mt == EN_PASSANT) {
        if (pt != PAWN) return false;
        if (to != state->ep_square || dest != NO_PIECE) return false;
        if (stm == WHITE) {
            return rank_of(from) == RANK_5 && (to == from + 7 || to == from + 9)
                   && std::abs(file_of(to) - file_of(from)) == 1;
        } else {
            return rank_of(from) == RANK_4 && (to == from - 7 || to == from - 9)
                   && std::abs(file_of(to) - file_of(from)) == 1;
        }
    } else if (mt == CASTLING) {
        if (pt != KING || is_in_check()) return false;
        if (stm == WHITE) {
            if (from != SQ_E1) return false;
            if (to == SQ_G1) {
                return (state->castling_rights & WHITE_OO) && board[SQ_H1] == W_ROOK && !(occupied() & WHITE_OO_PATH);
            } else if (to == SQ_C1) {
                return (state->castling_rights & WHITE_OOO) && board[SQ_A1] == W_ROOK && !(occupied() & WHITE_OOO_PATH);
            }
        } else {
            if (from != SQ_E8) return false;
            if (to == SQ_G8) {
                return (state->castling_rights & BLACK_OO) && board[SQ_H8] == B_ROOK && !(occupied() & BLACK_OO_PATH);
            } else if (to == SQ_C8) {
                return (state->castling_rights & BLACK_OOO) && board[SQ_A8] == B_ROOK && !(occupied() & BLACK_OOO_PATH);
            }
        }
        return false;
    }
    return false;
}

bool Position::gives_check(Move m) const {
    Square from = m.from();
    Square to = m.to();
    Square opp_k = king_square(~stm);
    if (opp_k == SQ_NONE) return false;

    PieceType pt = type_of(board[from]);
    if (m.type() == PROMOTION) pt = m.promo_type();

    // Build the occupancy after the move.
    Bitboard occ_after = occupied() ^ square_bb(from);
    if (m.type() == EN_PASSANT) {
        occ_after ^= square_bb(make_square(file_of(to), rank_of(from)));
    }
    occ_after |= square_bb(to);

    if (m.type() == CASTLING) {
        Square r_from = (to > from) ? make_square(FILE_H, rank_of(from)) : make_square(FILE_A, rank_of(from));
        Square r_to   = (to > from) ? make_square(FILE_F, rank_of(from)) : make_square(FILE_D, rank_of(from));
        occ_after ^= square_bb(r_from);
        occ_after |= square_bb(r_to);
        return (Bitboards::rook_attacks(r_to, occ_after) & square_bb(opp_k)) != 0;
    }

    // Direct check by the moving piece.
    if (pt == PAWN) {
        if (Bitboards::pawn_attacks(stm, to) & square_bb(opp_k)) return true;
    } else {
        if (Bitboards::attacks_bb(pt, to, occ_after) & square_bb(opp_k)) return true;
    }

    // Discovered check by one of our sliders.
    Bitboard sliders = (Bitboards::bishop_attacks(opp_k, occ_after) & (pieces(stm, BISHOP) | pieces(stm, QUEEN)))
                     | (Bitboards::rook_attacks(opp_k, occ_after) & (pieces(stm, ROOK) | pieces(stm, QUEEN)));
    return sliders != 0;
}

void Position::make_move(Move m, StateInfo& new_st) {
    Square from = m.from();
    Square to = m.to();
    Piece pc = board[from];
    Piece captured = (m.type() == EN_PASSANT) ? make_piece(~stm, PAWN) : board[to];

    new_st.previous = state;
    new_st.key = state->key ^ ZobristSide;
    new_st.pawn_key = state->pawn_key;
    new_st.material_key = state->material_key;
    new_st.castling_rights = state->castling_rights & CastlingSquareMask[from] & CastlingSquareMask[to];
    new_st.ep_square = SQ_NONE;
    new_st.halfmove_clock = (type_of(pc) == PAWN || captured != NO_PIECE) ? 0 : state->halfmove_clock + 1;
    new_st.captured_piece = captured;

    // Update castling hash
    new_st.key ^= ZobristCastling[state->castling_rights] ^ ZobristCastling[new_st.castling_rights];

    // Remove previous ep
    if (state->ep_square != SQ_NONE) {
        new_st.key ^= ZobristEp[state->ep_square];
    }

    // Capture piece
    if (captured != NO_PIECE) {
        Square cap_sq = to;
        int old_cnt = popcount(pieces(~stm, type_of(captured)));
        if (m.type() == EN_PASSANT) {
            cap_sq = make_square(file_of(to), rank_of(from));
            remove_piece(cap_sq);
        } else {
            remove_piece(to);
        }
        new_st.key ^= ZobristPiece[captured][cap_sq];
        new_st.material_key ^= ZobristMaterial[captured][std::min(15, old_cnt)]
                             ^ ZobristMaterial[captured][std::min(15, old_cnt - 1)];
        if (type_of(captured) == PAWN) {
            new_st.pawn_key ^= ZobristPawn[captured][cap_sq];
        }
    }

    // Move moving piece
    move_piece(from, to);
    new_st.key ^= ZobristPiece[pc][from] ^ ZobristPiece[pc][to];
    if (type_of(pc) == PAWN) {
        new_st.pawn_key ^= ZobristPawn[pc][from] ^ ZobristPawn[pc][to];
    }

    // Special moves
    if (m.type() == PROMOTION) {
        Piece promo = make_piece(stm, m.promo_type());
        int old_pawn_cnt = popcount(pieces(stm, PAWN));
        int old_promo_cnt = popcount(pieces(stm, m.promo_type()));
        remove_piece(to);
        put_piece(promo, to);
        new_st.key ^= ZobristPiece[pc][to] ^ ZobristPiece[promo][to];
        new_st.pawn_key ^= ZobristPawn[pc][to];
        new_st.material_key ^= ZobristMaterial[pc][std::min(15, old_pawn_cnt)]
                             ^ ZobristMaterial[pc][std::min(15, old_pawn_cnt - 1)]
                             ^ ZobristMaterial[promo][std::min(15, old_promo_cnt)]
                             ^ ZobristMaterial[promo][std::min(15, old_promo_cnt + 1)];
    } else if (m.type() == CASTLING) {
        Square r_from, r_to;
        if (to > from) { // Kingside
            r_from = make_square(FILE_H, rank_of(from));
            r_to = make_square(FILE_F, rank_of(from));
        } else { // Queenside
            r_from = make_square(FILE_A, rank_of(from));
            r_to = make_square(FILE_D, rank_of(from));
        }
        Piece rook = make_piece(stm, ROOK);
        move_piece(r_from, r_to);
        new_st.key ^= ZobristPiece[rook][r_from] ^ ZobristPiece[rook][r_to];
    } else if (type_of(pc) == PAWN && std::abs(rank_of(to) - rank_of(from)) == 2) {
        Square ep_sq = static_cast<Square>((from + to) / 2);
        // Only set ep square if opponent can potentially capture
        if (Bitboards::pawn_attacks(stm, ep_sq) & pieces(~stm, PAWN)) {
            new_st.ep_square = ep_sq;
            new_st.key ^= ZobristEp[ep_sq];
        }
    }

    // Lazy Accumulator: Do not copy 4KB or compute deltas eagerly.
    // Computed on-demand in Position::accumulator() only if evaluate() is called!
    new_st.last_move = m;
    new_st.moved_piece = pc;
    new_st.acc_computed = false;

    stm = ~stm;
    ply++;
    state = &new_st;

    // Checkers
    Square ksq = king_square(stm);
    state->checkers = attackers_to(ksq, occupied()) & pieces(~stm);
    state->blockers_for_king[stm] = blockers_for_king(stm);
    state->blockers_for_king[~stm] = blockers_for_king(~stm);
}

void Position::unmake_move(Move m) {
    stm = ~stm;
    ply--;

    Square from = m.from();
    Square to = m.to();
    Piece pc = board[to];

    if (m.type() == PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(stm, PAWN), to);
    }

    move_piece(to, from);

    if (state->captured_piece != NO_PIECE) {
        Square cap_sq = to;
        if (m.type() == EN_PASSANT) {
            cap_sq = make_square(file_of(to), rank_of(from));
        }
        put_piece(state->captured_piece, cap_sq);
    }

    if (m.type() == CASTLING) {
        Square r_from, r_to;
        if (to > from) {
            r_from = make_square(FILE_H, rank_of(from));
            r_to = make_square(FILE_F, rank_of(from));
        } else {
            r_from = make_square(FILE_A, rank_of(from));
            r_to = make_square(FILE_D, rank_of(from));
        }
        move_piece(r_to, r_from);
    }

    state = state->previous;
}

void Position::make_null_move(StateInfo& new_st) {
    new_st.previous = state;
    new_st.key = state->key ^ ZobristSide;
    new_st.pawn_key = state->pawn_key;
    new_st.material_key = state->material_key;
    new_st.castling_rights = state->castling_rights;
    new_st.ep_square = SQ_NONE;
    new_st.halfmove_clock = state->halfmove_clock + 1;
    new_st.captured_piece = NO_PIECE;
    new_st.checkers = 0;

    if (state->ep_square != SQ_NONE) {
        new_st.key ^= ZobristEp[state->ep_square];
    }

    new_st.last_move = Move();
    new_st.acc_computed = false;
    new_st.blockers_for_king[WHITE] = state->blockers_for_king[WHITE];
    new_st.blockers_for_king[BLACK] = state->blockers_for_king[BLACK];

    stm = ~stm;
    ply++;
    state = &new_st;
}

void Position::unmake_null_move() {
    stm = ~stm;
    ply--;
    state = state->previous;
}

int Position::non_pawn_material(Color c) const {
    Bitboard b = pieces(c) & ~pieces(c, PAWN) & ~pieces(c, KING);
    int mat = 0;
    while (b) {
        Square sq = pop_lsb(b);
        switch (type_of(board[sq])) {
            case KNIGHT: mat += 320; break;
            case BISHOP: mat += 330; break;
            case ROOK:   mat += 500; break;
            case QUEEN:  mat += 900; break;
            default: break;
        }
    }
    return mat;
}

bool Position::has_repeated(int count) const {
    int reps = 0;
    int limit = std::min(state->halfmove_clock, ply);
    const StateInfo* cur = state;
    for (int d = 1; d <= limit; ++d) {
        cur = cur->previous;
        if (!cur) break;
        if ((d & 1) == 0 && cur->key == state->key && ++reps >= count)
            return true;
    }
    return false;
}

bool Position::is_draw_by_rule() const {
    if (state->halfmove_clock >= 100) return true;
    if (has_repeated(2)) return true;

    // Insufficient material
    if (popcount(occupied()) == 2) return true; // KvK
    if (popcount(occupied()) == 3) {
        if (pieces(KNIGHT) || pieces(BISHOP)) return true; // KvKB or KvKN
    }
    return false;
}

// Classical Static Exchange Evaluation
Value Position::see(Move m, Value threshold) const {
    Square from = m.from();
    Square to = m.to();

    Piece pc = board[from];
    Piece target = (m.type() == EN_PASSANT) ? make_piece(~stm, PAWN) : board[to];

    constexpr int PieceVals[] = { 100, 320, 330, 500, 900, 20000, 0 };
    int gain[32];
    int d = 0;

    gain[d] = (target != NO_PIECE) ? PieceVals[type_of(target)] : 0;
    if (m.type() == PROMOTION) {
        gain[d] += PieceVals[m.promo_type()] - PieceVals[PAWN];
    }

    if (gain[0] < threshold) return -1;

    Bitboard from_bb = square_bb(from);
    Bitboard occ = occupied() ^ from_bb;

    Color side = ~stm;
    Bitboard attackers = attackers_to(to, occ);

    while (true) {
        d++;
        if (d >= 31) break;
        gain[d] = PieceVals[type_of(pc)] - gain[d - 1];

        if (std::max(-gain[d - 1], gain[d]) < threshold) break;

        Bitboard side_att = attackers & pieces(side);
        if (!side_att) break;

        // Pick least valuable attacker
        PieceType next_pt = NO_PIECE_TYPE;
        for (PieceType pt = PAWN; pt <= KING; pt = static_cast<PieceType>(pt + 1)) {
            Bitboard b = side_att & pieces(pt);
            if (b) {
                from_bb = square_bb(lsb(b));
                next_pt = pt;
                break;
            }
        }

        pc = make_piece(side, next_pt);
        occ ^= from_bb;

        // Add newly discovered sliding attackers behind the moved piece
        attackers |= (Bitboards::bishop_attacks(to, occ) & (pieces(BISHOP) | pieces(QUEEN)))
                   | (Bitboards::rook_attacks(to, occ) & (pieces(ROOK) | pieces(QUEEN)));
        attackers &= occ;

        side = ~side;
    }

    while (--d > 0) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    }

    return gain[0];
}

} // namespace NextFish
