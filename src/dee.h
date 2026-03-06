#ifndef DEE_H_INCLUDED
#define DEE_H_INCLUDED

#include "types.h"
#include "position.h"
#include <vector>

namespace Stockfish {

namespace DEE {

struct ExchangeSequence {
    std::vector<Move> moves;
    Value material_delta;
    Value structural_impact;
    Value dynamic_impact;
};

struct DEE_Result {
    Value total_score;
    Value threat_value;
    bool  should_execute_now;
    Move  best_exchange_move;
};

class Evaluator {
public:
    // Core DEE evaluation
    static DEE_Result evaluate(const Position& pos);
    
    // Quick check for tactical tension
    static bool has_tension(const Position& pos);

    // Get adjusted SEE value with DEE principles
    static Value adjusted_see(const Position& pos, Move m);

private:
    static Value compute_structural_impact(const Position& pos, const ExchangeSequence& seq);
    static Value compute_dynamic_impact(const Position& pos, const ExchangeSequence& seq);
};

} // namespace DEE

} // namespace Stockfish

#endif // DEE_H_INCLUDED
