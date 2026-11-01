#include "bitboard.h"
#include <random>
#include <cstring>

namespace NextFish {

namespace Bitboards {

Bitboard PawnAttacks[COLOR_NB][64];
Bitboard PassedPawnMask[COLOR_NB][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];

Magic BishopMagics[64];
Magic RookMagics[64];

static Bitboard BishopTable[16384];
static Bitboard RookTable[131072];

// Direction offsets
constexpr int DirN = 8, DirS = -8, DirE = 1, DirW = -1;
constexpr int DirNE = 9, DirNW = 7, DirSE = -7, DirSW = -9;

static Bitboard sliding_attacks_slow(Square sq, Bitboard occ, const int* dirs, int num_dirs) {
    Bitboard attacks = 0;
    for (int d = 0; d < num_dirs; ++d) {
        int dir = dirs[d];
        Square s = sq;
        while (true) {
            int prev_f = file_of(s);
            int prev_r = rank_of(s);
            s = static_cast<Square>(s + dir);
            if (s < 0 || s >= 64) break;
            int cur_f = file_of(s);
            int cur_r = rank_of(s);
            if (std::abs(cur_f - prev_f) > 1 || std::abs(cur_r - prev_r) > 1) break;

            attacks |= square_bb(s);
            if (occ & square_bb(s)) break;
        }
    }
    return attacks;
}

static Bitboard bishop_mask(Square sq) {
    Bitboard mask = 0;
    int r = rank_of(sq), f = file_of(sq);
    for (int dr : {-1, 1}) {
        for (int df : {-1, 1}) {
            int cur_r = r + dr, cur_f = f + df;
            while (cur_r > 0 && cur_r < 7 && cur_f > 0 && cur_f < 7) {
                mask |= square_bb(make_square(static_cast<File>(cur_f), static_cast<Rank>(cur_r)));
                cur_r += dr;
                cur_f += df;
            }
        }
    }
    return mask;
}

static Bitboard rook_mask(Square sq) {
    Bitboard mask = 0;
    int r = rank_of(sq), f = file_of(sq);
    for (int cur_r = r + 1; cur_r < 7; ++cur_r) mask |= square_bb(make_square(static_cast<File>(f), static_cast<Rank>(cur_r)));
    for (int cur_r = r - 1; cur_r > 0; --cur_r) mask |= square_bb(make_square(static_cast<File>(f), static_cast<Rank>(cur_r)));
    for (int cur_f = f + 1; cur_f < 7; ++cur_f) mask |= square_bb(make_square(static_cast<File>(cur_f), static_cast<Rank>(r)));
    for (int cur_f = f - 1; cur_f > 0; --cur_f) mask |= square_bb(make_square(static_cast<File>(cur_f), static_cast<Rank>(r)));
    return mask;
}

static Bitboard index_to_occupancy(int index, int bits, Bitboard mask) {
    Bitboard occ = 0;
    for (int i = 0; i < bits; ++i) {
        Square s = pop_lsb(mask);
        if (index & (1 << i)) {
            occ |= square_bb(s);
        }
    }
    return occ;
}

static uint64_t random_uint64(std::mt19937_64& rng) {
    return rng() & rng() & rng();
}

static void init_magics(bool is_bishop) {
    const int bishop_dirs[] = { DirNE, DirNW, DirSE, DirSW };
    const int rook_dirs[] = { DirN, DirS, DirE, DirW };
    const int* dirs = is_bishop ? bishop_dirs : rook_dirs;
    int num_dirs = 4;

    Bitboard* table_ptr = is_bishop ? BishopTable : RookTable;
    std::mt19937_64 rng(5489ULL);

    for (int sq = 0; sq < 64; ++sq) {
        Square s = static_cast<Square>(sq);
        Bitboard mask = is_bishop ? bishop_mask(s) : rook_mask(s);
        int relevant_bits = popcount(mask);
        int num_occupancies = 1 << relevant_bits;

        std::vector<Bitboard> occupancies(num_occupancies);
        std::vector<Bitboard> attacks(num_occupancies);

        for (int i = 0; i < num_occupancies; ++i) {
            occupancies[i] = index_to_occupancy(i, relevant_bits, mask);
            attacks[i] = sliding_attacks_slow(s, occupancies[i], dirs, num_dirs);
        }

        Magic& magic_entry = is_bishop ? BishopMagics[s] : RookMagics[s];
        magic_entry.mask = mask;
        magic_entry.shift = 64 - relevant_bits;
        magic_entry.attacks = table_ptr;
        table_ptr += num_occupancies;

        std::vector<Bitboard> used(num_occupancies, 0);

        while (true) {
            Bitboard magic = random_uint64(rng);
            if (popcount((mask * magic) & 0xFF00000000000000ULL) < 6) continue;

            std::fill(used.begin(), used.end(), 0);
            bool fail = false;

            for (int i = 0; i < num_occupancies; ++i) {
                unsigned int idx = static_cast<unsigned int>(((occupancies[i] & mask) * magic) >> magic_entry.shift);
                if (used[idx] != 0 && used[idx] != attacks[i]) {
                    fail = true;
                    break;
                }
                used[idx] = attacks[i];
            }

            if (!fail) {
                magic_entry.magic = magic;
                for (int i = 0; i < num_occupancies; ++i) {
                    unsigned int idx = magic_entry.index(occupancies[i]);
                    magic_entry.attacks[idx] = attacks[i];
                }
                break;
            }
        }
    }
}

void init() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // Pawn attacks
    for (int sq = 0; sq < 64; ++sq) {
        Square s = static_cast<Square>(sq);
        Bitboard b = square_bb(s);
        // White attacks NE and NW
        PawnAttacks[WHITE][sq] = ((b << 9) & ~0x0101010101010101ULL) | ((b << 7) & ~0x8080808080808080ULL);
        // Black attacks SE and SW
        PawnAttacks[BLACK][sq] = ((b >> 7) & ~0x0101010101010101ULL) | ((b >> 9) & ~0x8080808080808080ULL);
    }

    // Passed pawn masks
    for (int sq = 0; sq < 64; ++sq) {
        Square s = static_cast<Square>(sq);
        int r = rank_of(s), f = file_of(s);

        PassedPawnMask[WHITE][sq] = 0;
        PassedPawnMask[BLACK][sq] = 0;

        for (int cur_r = r + 1; cur_r < 8; ++cur_r) {
            for (int cur_f = std::max(0, f - 1); cur_f <= std::min(7, f + 1); ++cur_f) {
                PassedPawnMask[WHITE][sq] |= square_bb(make_square(static_cast<File>(cur_f), static_cast<Rank>(cur_r)));
            }
        }

        for (int cur_r = r - 1; cur_r >= 0; --cur_r) {
            for (int cur_f = std::max(0, f - 1); cur_f <= std::min(7, f + 1); ++cur_f) {
                PassedPawnMask[BLACK][sq] |= square_bb(make_square(static_cast<File>(cur_f), static_cast<Rank>(cur_r)));
            }
        }
    }

    // Knight attacks
    const int knight_dr[] = { 1, 2, 2, 1, -1, -2, -2, -1 };
    const int knight_df[] = { 2, 1, -1, -2, -2, -1, 1, 2 };
    for (int sq = 0; sq < 64; ++sq) {
        Square s = static_cast<Square>(sq);
        int r = rank_of(s), f = file_of(s);
        KnightAttacks[sq] = 0;
        for (int i = 0; i < 8; ++i) {
            int nr = r + knight_dr[i];
            int nf = f + knight_df[i];
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                KnightAttacks[sq] |= square_bb(make_square(static_cast<File>(nf), static_cast<Rank>(nr)));
            }
        }
    }

    // King attacks
    for (int sq = 0; sq < 64; ++sq) {
        Square s = static_cast<Square>(sq);
        int r = rank_of(s), f = file_of(s);
        KingAttacks[sq] = 0;
        for (int dr = -1; dr <= 1; ++dr) {
            for (int df = -1; df <= 1; ++df) {
                if (dr == 0 && df == 0) continue;
                int nr = r + dr, nf = f + df;
                if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    KingAttacks[sq] |= square_bb(make_square(static_cast<File>(nf), static_cast<Rank>(nr)));
                }
            }
        }
    }

    // Magics for Bishops and Rooks
    init_magics(true);
    init_magics(false);

    // Between and Line tables
    for (int sq1 = 0; sq1 < 64; ++sq1) {
        for (int sq2 = 0; sq2 < 64; ++sq2) {
            Square s1 = static_cast<Square>(sq1);
            Square s2 = static_cast<Square>(sq2);
            BetweenBB[sq1][sq2] = 0;
            LineBB[sq1][sq2] = 0;

            if (s1 == s2) continue;

            Bitboard b_att = bishop_attacks(s1, 0);
            Bitboard r_att = rook_attacks(s1, 0);

            if (b_att & square_bb(s2)) {
                LineBB[sq1][sq2] = (bishop_attacks(s1, 0) & bishop_attacks(s2, 0)) | square_bb(s1) | square_bb(s2);
                Bitboard occ = (square_bb(s1) | square_bb(s2));
                BetweenBB[sq1][sq2] = bishop_attacks(s1, square_bb(s2)) & bishop_attacks(s2, square_bb(s1));
            } else if (r_att & square_bb(s2)) {
                LineBB[sq1][sq2] = (rook_attacks(s1, 0) & rook_attacks(s2, 0)) | square_bb(s1) | square_bb(s2);
                BetweenBB[sq1][sq2] = rook_attacks(s1, square_bb(s2)) & rook_attacks(s2, square_bb(s1));
            }
        }
    }
}

} // namespace Bitboards

} // namespace NextFish
