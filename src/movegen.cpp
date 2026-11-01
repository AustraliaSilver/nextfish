#include "movegen.h"

namespace NextFish {

template <Color Us, GenType Type>
void generate_pawn_moves(const Position& pos, MoveList& list, Bitboard target) {
    constexpr Color Them = ~Us;
    constexpr int Up = (Us == WHITE) ? 8 : -8;
    constexpr Rank PromRank = (Us == WHITE) ? RANK_7 : RANK_2;
    constexpr Rank StartRank = (Us == WHITE) ? RANK_2 : RANK_7;

    Bitboard pawns = pos.pieces(Us, PAWN);
    Bitboard empty_sqs = pos.empty();
    Bitboard opp_pieces = pos.pieces(Them);

    Bitboard prom_pawns = pawns & ((Us == WHITE) ? 0x00FF000000000000ULL : 0x000000000000FF00ULL);
    Bitboard non_prom_pawns = pawns & ~prom_pawns;

    // Single pushes
    if (Type != CAPTURES) {
        Bitboard single_pushes = (Us == WHITE) ? (non_prom_pawns << 8) & empty_sqs
                                               : (non_prom_pawns >> 8) & empty_sqs;
        Bitboard valid_single = single_pushes & target;
        Bitboard b = valid_single;
        while (b) {
            Square to = pop_lsb(b);
            Square from = static_cast<Square>(to - Up);
            list.push_back(Move(from, to, NORMAL));
        }

        // Double pushes
        Bitboard double_push_rank = (Us == WHITE) ? 0x00000000FF000000ULL : 0x000000FF00000000ULL;
        Bitboard double_pushes = (Us == WHITE) ? ((single_pushes & 0x0000000000FF0000ULL) << 8) & empty_sqs & double_push_rank
                                               : ((single_pushes & 0x0000FF0000000000ULL) >> 8) & empty_sqs & double_push_rank;
        Bitboard valid_double = double_pushes & target;
        b = valid_double;
        while (b) {
            Square to = pop_lsb(b);
            Square from = static_cast<Square>(to - Up - Up);
            list.push_back(Move(from, to, NORMAL));
        }
    }

    // Pawn captures
    if (Type != QUIETS) {
        // Left capture
        Bitboard left_att = (Us == WHITE) ? ((non_prom_pawns & ~0x0101010101010101ULL) << 7)
                                          : ((non_prom_pawns & ~0x0101010101010101ULL) >> 9);
        Bitboard valid_left = left_att & opp_pieces & target;
        Bitboard b = valid_left;
        while (b) {
            Square to = pop_lsb(b);
            Square from = static_cast<Square>(Us == WHITE ? to - 7 : to + 9);
            list.push_back(Move(from, to, NORMAL));
        }

        // Right capture
        Bitboard right_att = (Us == WHITE) ? ((non_prom_pawns & ~0x8080808080808080ULL) << 9)
                                           : ((non_prom_pawns & ~0x8080808080808080ULL) >> 7);
        Bitboard valid_right = right_att & opp_pieces & target;
        b = valid_right;
        while (b) {
            Square to = pop_lsb(b);
            Square from = static_cast<Square>(Us == WHITE ? to - 9 : to + 7);
            list.push_back(Move(from, to, NORMAL));
        }

        // Promotions
        if (prom_pawns) {
            // Push promo
            Bitboard push_promo = (Us == WHITE) ? (prom_pawns << 8) & empty_sqs
                                                : (prom_pawns >> 8) & empty_sqs;
            if constexpr (Type == EVASIONS) {
                push_promo &= target;
            } else if constexpr (Type != CAPTURES) {
                push_promo &= target;
            }
            // For Type == CAPTURES, push promotions land on empty squares and must be searched
            b = push_promo;
            while (b) {
                Square to = pop_lsb(b);
                Square from = static_cast<Square>(to - Up);
                list.push_back(Move(from, to, PROMOTION, QUEEN));
                if constexpr (Type != CAPTURES) {
                    list.push_back(Move(from, to, PROMOTION, ROOK));
                    list.push_back(Move(from, to, PROMOTION, BISHOP));
                    list.push_back(Move(from, to, PROMOTION, KNIGHT));
                }
            }

            // Capture promo left
            Bitboard cap_promo_left = (Us == WHITE) ? ((prom_pawns & ~0x0101010101010101ULL) << 7) & opp_pieces
                                                    : ((prom_pawns & ~0x0101010101010101ULL) >> 9) & opp_pieces;
            cap_promo_left &= target;
            b = cap_promo_left;
            while (b) {
                Square to = pop_lsb(b);
                Square from = static_cast<Square>(Us == WHITE ? to - 7 : to + 9);
                list.push_back(Move(from, to, PROMOTION, QUEEN));
                list.push_back(Move(from, to, PROMOTION, ROOK));
                list.push_back(Move(from, to, PROMOTION, BISHOP));
                list.push_back(Move(from, to, PROMOTION, KNIGHT));
            }

            // Capture promo right
            Bitboard cap_promo_right = (Us == WHITE) ? ((prom_pawns & ~0x8080808080808080ULL) << 9) & opp_pieces
                                                     : ((prom_pawns & ~0x8080808080808080ULL) >> 7) & opp_pieces;
            cap_promo_right &= target;
            b = cap_promo_right;
            while (b) {
                Square to = pop_lsb(b);
                Square from = static_cast<Square>(Us == WHITE ? to - 9 : to + 7);
                list.push_back(Move(from, to, PROMOTION, QUEEN));
                list.push_back(Move(from, to, PROMOTION, ROOK));
                list.push_back(Move(from, to, PROMOTION, BISHOP));
                list.push_back(Move(from, to, PROMOTION, KNIGHT));
            }
        }

        // En passant
        if (pos.ep_square() != SQ_NONE) {
            Square ep = pos.ep_square();
            Bitboard ep_att = Bitboards::pawn_attacks(Them, ep) & pawns;
            while (ep_att) {
                Square from = pop_lsb(ep_att);
                list.push_back(Move(from, ep, EN_PASSANT));
            }
        }
    }
}

template <Color Us, PieceType Pt, GenType Type>
void generate_piece_moves(const Position& pos, MoveList& list, Bitboard target) {
    Bitboard pieces = pos.pieces(Us, Pt);
    Bitboard occ = pos.occupied();

    while (pieces) {
        Square from = pop_lsb(pieces);
        Bitboard att = Bitboards::attacks_bb(Pt, from, occ) & target;
        while (att) {
            Square to = pop_lsb(att);
            list.push_back(Move(from, to, NORMAL));
        }
    }
}

template <Color Us, GenType Type>
void generate_castling_moves(const Position& pos, MoveList& list) {
    if (Type == CAPTURES) return;
    if (pos.is_in_check()) return;

    int cr = pos.castling_rights();
    Bitboard occ = pos.occupied();

    if constexpr (Us == WHITE) {
        if ((cr & WHITE_OO) && !(occ & ((1ULL << SQ_F1) | (1ULL << SQ_G1)))) {
            list.push_back(Move(SQ_E1, SQ_G1, CASTLING));
        }
        if ((cr & WHITE_OOO) && !(occ & ((1ULL << SQ_B1) | (1ULL << SQ_C1) | (1ULL << SQ_D1)))) {
            list.push_back(Move(SQ_E1, SQ_C1, CASTLING));
        }
    } else {
        if ((cr & BLACK_OO) && !(occ & ((1ULL << SQ_F8) | (1ULL << SQ_G8)))) {
            list.push_back(Move(SQ_E8, SQ_G8, CASTLING));
        }
        if ((cr & BLACK_OOO) && !(occ & ((1ULL << SQ_B8) | (1ULL << SQ_C8) | (1ULL << SQ_D8)))) {
            list.push_back(Move(SQ_E8, SQ_C8, CASTLING));
        }
    }
}

template <GenType Type>
void generate_moves(const Position& pos, MoveList& list) {
    list.clear();
    Color us = pos.side_to_move();
    Color them = ~us;

    Bitboard target = ~pos.pieces(us) & ~pos.pieces(them, KING);
    if constexpr (Type == CAPTURES) {
        target = pos.pieces(them) & ~pos.pieces(them, KING);
    } else if constexpr (Type == QUIETS) {
        target = pos.empty();
    } else if constexpr (Type == EVASIONS) {
        // In check: must move king, capture checker, or block checker ray
        Square ksq = pos.king_square(us);
        Bitboard checkers = pos.checkers();
        if (popcount(checkers) > 1) {
            // Double check: ONLY king can move!
            Bitboard att = Bitboards::king_attacks(ksq) & ~pos.pieces(us) & ~pos.pieces(them, KING);
            while (att) {
                Square to = pop_lsb(att);
                list.push_back(Move(ksq, to, NORMAL));
            }
            return;
        }
        Square checker_sq = lsb(checkers);
        target = (square_bb(checker_sq) | Bitboards::between_bb(ksq, checker_sq)) & ~pos.pieces(them, KING);
    }

    if (us == WHITE) {
        generate_pawn_moves<WHITE, Type>(pos, list, target);
        generate_piece_moves<WHITE, KNIGHT, Type>(pos, list, target);
        generate_piece_moves<WHITE, BISHOP, Type>(pos, list, target);
        generate_piece_moves<WHITE, ROOK, Type>(pos, list, target);
        generate_piece_moves<WHITE, QUEEN, Type>(pos, list, target);
        generate_piece_moves<WHITE, KING, Type>(pos, list, (Type == EVASIONS ? ~pos.pieces(WHITE) : target) & ~pos.pieces(BLACK, KING));
        generate_castling_moves<WHITE, Type>(pos, list);
    } else {
        generate_pawn_moves<BLACK, Type>(pos, list, target);
        generate_piece_moves<BLACK, KNIGHT, Type>(pos, list, target);
        generate_piece_moves<BLACK, BISHOP, Type>(pos, list, target);
        generate_piece_moves<BLACK, ROOK, Type>(pos, list, target);
        generate_piece_moves<BLACK, QUEEN, Type>(pos, list, target);
        generate_piece_moves<BLACK, KING, Type>(pos, list, (Type == EVASIONS ? ~pos.pieces(BLACK) : target) & ~pos.pieces(WHITE, KING));
        generate_castling_moves<BLACK, Type>(pos, list);
    }
}

template void generate_moves<CAPTURES>(const Position& pos, MoveList& list);
template void generate_moves<QUIETS>(const Position& pos, MoveList& list);
template void generate_moves<EVASIONS>(const Position& pos, MoveList& list);
template void generate_moves<ALL>(const Position& pos, MoveList& list);

} // namespace NextFish
