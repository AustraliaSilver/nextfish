#ifndef DQRS_H_INCLUDED
#define DQRS_H_INCLUDED

#include "types.h"
#include "position.h"

namespace Stockfish {

namespace DQRS {

struct ESA_Result {
    Value optimal_result;
    bool  is_stable;
    Move  best_move;
};

// Exchange Sequence Algebra (ESA) - analyzes exchanges on a square
ESA_Result analyze_exchange(const Position& pos, Square target);

// Eval Trajectory Prediction - tracks eval history in qsearch
class TrajectoryPredictor {
public:
    void record(int ply, Value v);
    bool should_stop(int ply, Value alpha, Value beta);
    Value predicted_convergence() const;
    void reset();

private:
    Value history[MAX_PLY];
    int count = 0;
};

} // namespace DQRS

} // namespace Stockfish

#endif // DQRS_H_INCLUDED
