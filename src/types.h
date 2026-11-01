#pragma once

#include <cstdint>
#include <string>
#include <iostream>
#include <algorithm>
#include <immintrin.h>

namespace NextFish {

using Bitboard = uint64_t;
using Key = uint64_t;
using Value = int32_t;

enum Color : int {
    WHITE = 0,
    BLACK = 1,
    COLOR_NB = 2
};

inline constexpr Color operator~(Color c) {
    return static_cast<Color>(c ^ 1);
}

enum PieceType : int {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    NO_PIECE_TYPE = 6,
    PIECE_TYPE_NB = 6
};

enum Piece : int {
    W_PAWN = 0, W_KNIGHT = 1, W_BISHOP = 2, W_ROOK = 3, W_QUEEN = 4, W_KING = 5,
    B_PAWN = 6, B_KNIGHT = 7, B_BISHOP = 8, B_ROOK = 9, B_QUEEN = 10, B_KING = 11,
    NO_PIECE = 12,
    PIECE_NB = 12
};

inline constexpr Piece make_piece(Color c, PieceType pt) {
    return static_cast<Piece>((c * 6) + pt);
}

inline constexpr Color color_of(Piece pc) {
    return static_cast<Color>(pc / 6);
}

inline constexpr PieceType type_of(Piece pc) {
    return static_cast<PieceType>(pc % 6);
}

enum Square : int {
    SQ_A1 = 0,  SQ_B1 = 1,  SQ_C1 = 2,  SQ_D1 = 3,  SQ_E1 = 4,  SQ_F1 = 5,  SQ_G1 = 6,  SQ_H1 = 7,
    SQ_A2 = 8,  SQ_B2 = 9,  SQ_C2 = 10, SQ_D2 = 11, SQ_E2 = 12, SQ_F2 = 13, SQ_G2 = 14, SQ_H2 = 15,
    SQ_A3 = 16, SQ_B3 = 17, SQ_C3 = 18, SQ_D3 = 19, SQ_E3 = 20, SQ_F3 = 21, SQ_G3 = 22, SQ_H3 = 23,
    SQ_A4 = 24, SQ_B4 = 25, SQ_C4 = 26, SQ_D4 = 27, SQ_E4 = 28, SQ_F4 = 29, SQ_G4 = 30, SQ_H4 = 31,
    SQ_A5 = 32, SQ_B5 = 33, SQ_C5 = 34, SQ_D5 = 35, SQ_E5 = 36, SQ_F5 = 37, SQ_G5 = 38, SQ_H5 = 39,
    SQ_A6 = 40, SQ_B6 = 41, SQ_C6 = 42, SQ_D6 = 43, SQ_E6 = 44, SQ_F6 = 45, SQ_G6 = 46, SQ_H6 = 47,
    SQ_A7 = 48, SQ_B7 = 49, SQ_C7 = 50, SQ_D7 = 51, SQ_E7 = 52, SQ_F7 = 53, SQ_G7 = 54, SQ_H7 = 55,
    SQ_A8 = 56, SQ_B8 = 57, SQ_C8 = 58, SQ_D8 = 59, SQ_E8 = 60, SQ_F8 = 61, SQ_G8 = 62, SQ_H8 = 63,
    SQ_NONE = 64
};

enum File : int {
    FILE_A = 0, FILE_B = 1, FILE_C = 2, FILE_D = 3,
    FILE_E = 4, FILE_F = 5, FILE_G = 6, FILE_H = 7
};

enum Rank : int {
    RANK_1 = 0, RANK_2 = 1, RANK_3 = 2, RANK_4 = 3,
    RANK_5 = 4, RANK_6 = 5, RANK_7 = 6, RANK_8 = 7
};

inline constexpr Square make_square(File f, Rank r) {
    return static_cast<Square>((r << 3) | f);
}

inline constexpr File file_of(Square s) {
    return static_cast<File>(s & 7);
}

inline constexpr Rank rank_of(Square s) {
    return static_cast<Rank>(s >> 3);
}

inline constexpr Square flip_rank(Square s) {
    return static_cast<Square>(s ^ 56);
}

enum CastlingRights : int {
    NO_CASTLING = 0,
    WHITE_OO = 1,
    WHITE_OOO = 2,
    BLACK_OO = 4,
    BLACK_OOO = 8,
    WHITE_CASTLING = WHITE_OO | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO | BLACK_OOO,
    ALL_CASTLING = WHITE_CASTLING | BLACK_CASTLING
};

// Move representation (16 bits)
// 0..5: from square (0..63)
// 6..11: to square (0..63)
// 12..13: promo piece type - KNIGHT (0: Knight, 1: Bishop, 2: Rook, 3: Queen)
// 14..15: move type (0: normal, 1: promotion, 2: en passant, 3: castling)
enum MoveType : int {
    NORMAL = 0,
    PROMOTION = 1,
    EN_PASSANT = 2,
    CASTLING = 3
};

struct Move {
    uint16_t data;

    constexpr Move() : data(0) {}
    constexpr explicit Move(uint16_t d) : data(d) {}
    constexpr Move(Square from, Square to, MoveType mt = NORMAL, PieceType promo = KNIGHT)
        : data(static_cast<uint16_t>(from | (to << 6) | ((promo - KNIGHT) << 12) | (mt << 14))) {}

    constexpr Square from() const { return static_cast<Square>(data & 0x3F); }
    constexpr Square to() const { return static_cast<Square>((data >> 6) & 0x3F); }
    constexpr MoveType type() const { return static_cast<MoveType>((data >> 14) & 0x3); }
    constexpr PieceType promo_type() const {
        return static_cast<PieceType>(((data >> 12) & 0x3) + KNIGHT);
    }

    constexpr bool is_valid() const { return data != 0; }
    constexpr bool operator==(const Move& other) const { return data == other.data; }
    constexpr bool operator!=(const Move& other) const { return data != other.data; }
};

inline const Move MOVE_NONE = Move(0);
inline const Move MOVE_NULL = Move(65);

// Values
constexpr int MAX_PLY = 128;
constexpr Value VALUE_ZERO = 0;
constexpr Value VALUE_DRAW = 0;
constexpr Value VALUE_MATE = 30000;
constexpr Value VALUE_INFINITE = 31000;
constexpr Value VALUE_MATE_THRESHOLD = 29000;
constexpr Value VALUE_NONE = 32002;

inline Value mate_in(int ply) { return VALUE_MATE - ply; }
inline Value mated_in(int ply) { return -VALUE_MATE + ply; }
inline bool is_mate_score(Value v) { return std::abs(v) >= VALUE_MATE_THRESHOLD; }

// Bitboard intrinsic helpers
inline int popcount(Bitboard b) {
    return __builtin_popcountll(b);
}

inline Square lsb(Bitboard b) {
    return static_cast<Square>(__builtin_ctzll(b));
}

inline Square msb(Bitboard b) {
    return static_cast<Square>(63 - __builtin_clzll(b));
}

inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}

inline constexpr Bitboard square_bb(Square s) {
    return 1ULL << s;
}

// Distance helpers
inline int file_distance(Square a, Square b) {
    return std::abs(file_of(a) - file_of(b));
}

inline int rank_distance(Square a, Square b) {
    return std::abs(rank_of(a) - rank_of(b));
}

inline int distance(Square a, Square b) {
    return std::max(file_distance(a, b), rank_distance(a, b));
}

inline std::string square_to_string(Square s) {
    if (s < 0 || s >= 64) return "-";
    std::string str;
    str += static_cast<char>('a' + file_of(s));
    str += static_cast<char>('1' + rank_of(s));
    return str;
}

inline std::string move_to_string(Move m) {
    if (!m.is_valid()) return "none";
    std::string str = square_to_string(m.from()) + square_to_string(m.to());
    if (m.type() == PROMOTION) {
        switch (m.promo_type()) {
            case KNIGHT: str += 'n'; break;
            case BISHOP: str += 'b'; break;
            case ROOK:   str += 'r'; break;
            case QUEEN:  str += 'q'; break;
            default: break;
        }
    }
    return str;
}

} // namespace NextFish
