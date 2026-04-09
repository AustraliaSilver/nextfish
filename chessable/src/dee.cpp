#include "dee.h"
#include "position.h"
#include "bitboard.h"

namespace Stockfish {
namespace DEE {

void Evaluator::compute_both_attack_maps(const Position& pos,
                                         Bitboard&       usAttacks,
                                         Bitboard&       themAttacks) {
    Bitboard occ  = pos.pieces();
    Color    us   = pos.side_to_move();
    Color    them = ~us;

    usAttacks   = 0;
    themAttacks = 0;

    usAttacks |= pos.attacks_by<PAWN>(us);
    themAttacks |= pos.attacks_by<PAWN>(them);

    Bitboard ourKnights   = pos.pieces(us, KNIGHT);
    Bitboard theirKnights = pos.pieces(them, KNIGHT);
    while (ourKnights)
        usAttacks |= attacks_bb<KNIGHT>(pop_lsb(ourKnights));
    while (theirKnights)
        themAttacks |= attacks_bb<KNIGHT>(pop_lsb(theirKnights));

    Bitboard ourDiag   = pos.pieces(us, BISHOP) | pos.pieces(us, QUEEN);
    Bitboard theirDiag = pos.pieces(them, BISHOP) | pos.pieces(them, QUEEN);
    while (ourDiag)
        usAttacks |= attacks_bb<BISHOP>(pop_lsb(ourDiag), occ);
    while (theirDiag)
        themAttacks |= attacks_bb<BISHOP>(pop_lsb(theirDiag), occ);

    Bitboard ourOrtho   = pos.pieces(us, ROOK) | pos.pieces(us, QUEEN);
    Bitboard theirOrtho = pos.pieces(them, ROOK) | pos.pieces(them, QUEEN);
    while (ourOrtho)
        usAttacks |= attacks_bb<ROOK>(pop_lsb(ourOrtho), occ);
    while (theirOrtho)
        themAttacks |= attacks_bb<ROOK>(pop_lsb(theirOrtho), occ);

    usAttacks |= attacks_bb<KING>(pos.square<KING>(us));
    themAttacks |= attacks_bb<KING>(pos.square<KING>(them));
}

bool Evaluator::has_tension(const Position& pos) {
    Bitboard ourAttacks, theirAttacks;
    compute_both_attack_maps(pos, ourAttacks, theirAttacks);
    return (ourAttacks & theirAttacks & pos.pieces()) != 0;
}

Value Evaluator::adjusted_see(const Position& pos, Move m) {
    if (!pos.see_ge(m, VALUE_ZERO))
        return Value(-100);

    Value bonus = VALUE_ZERO;
    if (type_of(pos.piece_on(m.from_sq())) == PAWN
        && relative_rank(pos.side_to_move(), m.to_sq()) >= RANK_6)
        bonus += Value(5 * relative_rank(pos.side_to_move(), m.to_sq()));
    return bonus;
}

Value Evaluator::evaluate_king_safety(const Position& pos, Color c, Bitboard enemyAttacks) {
    Square   ksq  = pos.square<KING>(c);
    Color    them = ~c;
    Bitboard occ  = pos.pieces();

    Value penalty = VALUE_ZERO;

    if (enemyAttacks & square_bb(ksq))
    {
        Bitboard kingAttackers = pos.attackers_to(ksq, occ) & pos.pieces(them);
        int      attackerCount = popcount(kingAttackers);
        penalty -= attackerCount * 20 + attackerCount * attackerCount * 2;

        if ((kingAttackers & pos.pieces(them, QUEEN)) && attackerCount >= 2)
            penalty -= 35;
    }

    Bitboard pinned = pos.blockers_for_king(c) & pos.pieces(c);
    if (pinned)
        penalty -= popcount(pinned) * 10;

    File     kf        = file_of(ksq);
    Bitboard kingFiles = 0;
    if (kf > FILE_A)
        kingFiles |= file_bb(File(kf - 1));
    kingFiles |= file_bb(kf);
    if (kf < FILE_H)
        kingFiles |= file_bb(File(kf + 1));

    Bitboard ourPawns  = pos.pieces(c, PAWN);
    int      openFiles = popcount(kingFiles & ~ourPawns);
    penalty -= openFiles * 7;

    Bitboard pawnShield = pos.pieces(c, PAWN) & enemyAttacks & (shift<NORTH>(pos.pieces(c, KING)));
    if (popcount(pawnShield) < 2)
        penalty -= 12;

    return penalty;
}

DEE_Result Evaluator::evaluate(const Position& pos) {
    DEE_Result result = {VALUE_ZERO, 0, false, VALUE_ZERO};

    Color    us   = pos.side_to_move();
    Color    them = ~us;
    Bitboard ourAttacks, theirAttacks;
    compute_both_attack_maps(pos, ourAttacks, theirAttacks);

    // Only count major pieces as hanging (not minors)
    Bitboard ourMajor    = pos.pieces(us) & (pos.pieces(ROOK) | pos.pieces(QUEEN));
    Bitboard hanging     = ourMajor & theirAttacks & ~ourAttacks;
    result.hanging_count = popcount(hanging);

    Value threat = VALUE_ZERO;
    while (hanging)
    {
        Square s = pop_lsb(hanging);
        threat -= PieceValue[pos.piece_on(s)];
    }

    // Check for our hanging majors too
    Bitboard theirMajor   = pos.pieces(them) & (pos.pieces(ROOK) | pos.pieces(QUEEN));
    Bitboard theirHanging = theirMajor & ourAttacks & ~theirAttacks;
    while (theirHanging)
    {
        Square s = pop_lsb(theirHanging);
        threat += PieceValue[pos.piece_on(s)] / 2;
    }

    // Only mark as tactical if there are real tactical threats (3+ contested pieces)
    Bitboard contested = ourAttacks & theirAttacks & pos.pieces();
    result.is_tactical = popcount(contested) >= 4;

    result.king_safety_delta =
      evaluate_king_safety(pos, us, theirAttacks) - evaluate_king_safety(pos, them, ourAttacks);

    // Conservative threat_value: only significant if real piece is hanging
    result.threat_value = threat;
    return result;
}

}  // namespace DEE
}  // namespace Stockfish
