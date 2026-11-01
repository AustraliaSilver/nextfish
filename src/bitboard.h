#pragma once

#include "types.h"
#include <vector>

namespace NextFish {

namespace Bitboards {

extern Bitboard PawnAttacks[COLOR_NB][64];
extern Bitboard PassedPawnMask[COLOR_NB][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];
extern Bitboard BetweenBB[64][64];
extern Bitboard LineBB[64][64];

struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    unsigned int shift;

    unsigned int index(Bitboard occ) const {
        return static_cast<unsigned int>(((occ & mask) * magic) >> shift);
    }
};

extern Magic BishopMagics[64];
extern Magic RookMagics[64];

void init();

inline Bitboard pawn_attacks(Color c, Square s) {
    return PawnAttacks[c][s];
}

inline Bitboard passed_pawn_mask(Color c, Square s) {
    return PassedPawnMask[c][s];
}

inline Bitboard knight_attacks(Square s) {
    return KnightAttacks[s];
}

inline Bitboard king_attacks(Square s) {
    return KingAttacks[s];
}

inline Bitboard bishop_attacks(Square s, Bitboard occ) {
    const Magic& m = BishopMagics[s];
    return m.attacks[m.index(occ)];
}

inline Bitboard rook_attacks(Square s, Bitboard occ) {
    const Magic& m = RookMagics[s];
    return m.attacks[m.index(occ)];
}

inline Bitboard queen_attacks(Square s, Bitboard occ) {
    return bishop_attacks(s, occ) | rook_attacks(s, occ);
}

inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
        case KNIGHT: return knight_attacks(s);
        case BISHOP: return bishop_attacks(s, occ);
        case ROOK:   return rook_attacks(s, occ);
        case QUEEN:  return queen_attacks(s, occ);
        case KING:   return king_attacks(s);
        default:     return 0ULL;
    }
}

inline Bitboard between_bb(Square sq1, Square sq2) {
    return BetweenBB[sq1][sq2];
}

inline Bitboard line_bb(Square sq1, Square sq2) {
    return LineBB[sq1][sq2];
}

} // namespace Bitboards

} // namespace NextFish
