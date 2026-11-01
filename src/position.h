#pragma once

#include "types.h"
#include "bitboard.h"
#include <string>
#include <vector>

namespace NextFish {

struct StateInfo {
    Key key = 0;
    Key pawn_key = 0;
    Key material_key = 0;
    int castling_rights = NO_CASTLING;
    Square ep_square = SQ_NONE;
    int halfmove_clock = 0;
    Piece captured_piece = NO_PIECE;
    Piece moved_piece = NO_PIECE;
    Bitboard checkers = 0;
    Bitboard blockers_for_king[2] = {0, 0};
    Move last_move = Move();
    StateInfo* previous = nullptr;

    // Incremental Accumulator
    alignas(64) int32_t accumulator[1024];
    bool acc_computed = false;
};

class Position {
public:
    Position();
    explicit Position(const std::string& fen);

    void set(const std::string& fen);
    std::string fen() const;

    // Piece and color accessors
    Bitboard pieces(Color c) const { return by_color[c]; }
    Bitboard pieces(PieceType pt) const { return by_type[pt]; }
    Bitboard pieces(Color c, PieceType pt) const { return by_color[c] & by_type[pt]; }
    Bitboard occupied() const { return by_color[WHITE] | by_color[BLACK]; }
    Bitboard empty() const { return ~occupied(); }

    Piece piece_on(Square s) const { return board[s]; }
    Color side_to_move() const { return stm; }
    Square king_square(Color c) const {
        Bitboard b = pieces(c, KING);
        return b ? lsb(b) : SQ_NONE;
    }

    int castling_rights() const { return state->castling_rights; }
    Square ep_square() const { return state->ep_square; }
    int halfmove_clock() const { return state->halfmove_clock; }
    int game_ply() const { return ply; }
    Key hash_key() const { return state->key; }
    Key pawn_hash_key() const { return state->pawn_key; }
    Key material_hash_key() const { return state->material_key; }
    const int32_t* accumulator() const;

    bool is_in_check() const { return state->checkers != 0; }
    Bitboard checkers() const { return state->checkers; }
    Bitboard blockers_for_king(Color c) const;

    // Attack queries
    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, occupied()); }

    // Legality
    bool is_legal(Move m) const;
    bool is_pseudo_legal(Move m) const;
    bool gives_check(Move m) const;

    // Move execution
    void make_move(Move m, StateInfo& new_st);
    void unmake_move(Move m);
    void make_null_move(StateInfo& new_st);
    void unmake_null_move();

    // Material and evaluation helpers
    int non_pawn_material(Color c) const;
    Value material_balance() const {
        int white_mat = 100 * popcount(pieces(WHITE, PAWN))
                      + 320 * popcount(pieces(WHITE, KNIGHT))
                      + 330 * popcount(pieces(WHITE, BISHOP))
                      + 500 * popcount(pieces(WHITE, ROOK))
                      + 900 * popcount(pieces(WHITE, QUEEN));
        int black_mat = 100 * popcount(pieces(BLACK, PAWN))
                      + 320 * popcount(pieces(BLACK, KNIGHT))
                      + 330 * popcount(pieces(BLACK, BISHOP))
                      + 500 * popcount(pieces(BLACK, ROOK))
                      + 900 * popcount(pieces(BLACK, QUEEN));
        return (stm == WHITE) ? static_cast<Value>(white_mat - black_mat) : static_cast<Value>(black_mat - white_mat);
    }
    bool has_repeated(int count = 2) const;
    bool is_draw_by_rule() const;
    bool pawn_is_passed(Color c, Square s) const {
        return (pieces(~c, PAWN) & Bitboards::passed_pawn_mask(c, s)) == 0;
    }
    Bitboard passed_pawns(Color c) const {
        Bitboard pawns = pieces(c, PAWN);
        Bitboard opp_pawns = pieces(~c, PAWN);
        Bitboard res = 0;
        while (pawns) {
            Square s = pop_lsb(pawns);
            if ((opp_pawns & Bitboards::passed_pawn_mask(c, s)) == 0) {
                res |= square_bb(s);
            }
        }
        return res;
    }

    // Fast static exchange evaluation (classic bitboard SEE)
    Value see(Move m, Value threshold = 0) const;

private:
    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    void set_state();

    Bitboard by_color[COLOR_NB];
    Bitboard by_type[PIECE_TYPE_NB];
    Piece board[64];

    Color stm;
    int ply;
    StateInfo* state;
    StateInfo start_state;
};

// Global Zobrist initialization
void init_zobrist();

} // namespace NextFish
