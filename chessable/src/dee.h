#ifndef DEE_H_INCLUDED
#define DEE_H_INCLUDED

#include "types.h"
#include "position.h"

namespace Stockfish {

namespace DEE {

struct DEE_Result {
    Value threat_value;
    int   hanging_count;
    bool  is_tactical;
    Value king_safety_delta;
};

class Evaluator {
   public:
    static DEE_Result evaluate(const Position& pos);
    static bool       has_tension(const Position& pos);
    static Value      adjusted_see(const Position& pos, Move m);

   private:
    static void
    compute_both_attack_maps(const Position& pos, Bitboard& usAttacks, Bitboard& themAttacks);
    static Value evaluate_king_safety(const Position& pos, Color c, Bitboard enemyAttacks);
};

}  // namespace DEE

}  // namespace Stockfish

#endif  // DEE_H_INCLUDED
