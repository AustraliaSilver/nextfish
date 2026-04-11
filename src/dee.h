#ifndef DEE_H_INCLUDED
#define DEE_H_INCLUDED

#include "types.h"
#include "position.h"

namespace Stockfish {

namespace DEE {

struct ExchangeCluster {
    Square target;
    int    pressure;
    int    attacker_imbalance;
    Value  net_swing;
    Value  aftermath_score;
};

struct DEE_Result {
    Value threat_value;
    int   hanging_count;
    bool  is_tactical;
    Value king_safety_delta;
    int   tension_score;
    Value aftermath_score;
    int   cluster_count;
    ExchangeCluster clusters[4];
};

class Evaluator {
   public:
    static DEE_Result evaluate(const Position& pos);
    static bool       has_tension(const Position& pos);
    static int        tension_score(const Position& pos);
    static Value      adjusted_see(const Position& pos, Move m);
    static int        move_order_bonus(const Position& pos, Move m, Depth depth);

   private:
    static void
    compute_both_attack_maps(const Position& pos, Bitboard& usAttacks, Bitboard& themAttacks);
    static Value evaluate_king_safety(const Position& pos, Color c, Bitboard enemyAttacks);
};

}  // namespace DEE

}  // namespace Stockfish

#endif  // DEE_H_INCLUDED
