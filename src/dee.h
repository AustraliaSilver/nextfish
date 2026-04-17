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
    Value  static_score;
};

struct DEE_Result {
    Value total_score;
    int   threat_value;
};

class Evaluator {
public:
    // Tie-breaker used in MovePicker for capture ordering
    static Value adjusted_see(const Position& pos, Move m);

    // QS Pruning Logic (V15)
    static bool should_prune_in_qs(const Position& pos, Move m, Value adjustedSee);

    // Board-wide tactical metrics
    static int tension_score(const Position& pos);
    
    static void
    compute_both_attack_maps(const Position& pos, Bitboard& usAttacks, Bitboard& themAttacks);

private:
    static Value evaluate_king_safety(const Position& pos, Color c, Bitboard enemyAttacks);
};

}  // namespace DEE

}  // namespace Stockfish

#endif  // DEE_H_INCLUDED
