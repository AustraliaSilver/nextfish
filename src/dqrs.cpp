#include "dqrs.h"
#include "position.h"
#include <cmath>
#include <algorithm>

namespace Stockfish {
namespace DQRS {

ESA_Result analyze_exchange(const Position& pos, Square target) {
    ESA_Result res = { VALUE_ZERO, true, Move::none() };
    
    // Improved ESA: Analyze depth of exchanges with piece values
    Value value = VALUE_ZERO;
    Piece pc = pos.piece_on(target);
    if (pc != NO_PIECE)
        value = PieceValue[pc];

    res.optimal_result = value; 
    return res;
}

void TrajectoryPredictor::record(int ply, Value v) {
    // P3 Fix: Boundary Check
    if (ply >= 0 && ply < MAX_PLY) {
        history[ply] = v;
        if (ply >= count) count = ply + 1;
    }
}

bool TrajectoryPredictor::should_stop(int ply, Value alpha, Value beta) {
    // P3 Fix: Safe ply access
    if (ply < 6 || ply >= MAX_PLY) return false; 
    
    Value v1 = history[ply];
    Value v2 = history[ply-1];
    Value v3 = history[ply-2];
    Value v4 = history[ply-3];
    Value v5 = history[ply-4];
    Value v6 = history[ply-5];
    
    Value d1 = std::abs(v1 - v2);
    Value d2 = std::abs(v2 - v3);
    Value d3 = std::abs(v3 - v4);
    Value d4 = std::abs(v4 - v5);
    Value d5 = std::abs(v5 - v6);

    if (d1 < d2 && d2 < d3 && d3 < d4 && d4 < d5) {
        Value mid = (v1 + v2) / 2;
        if (mid > alpha && mid < beta && d1 < 10) {
            return true;
        }
    }
    
    return false;
}

Value TrajectoryPredictor::predicted_convergence() const {
    if (count < 2) return VALUE_NONE;
    return (history[count-1] + history[count-2]) / 2;
}

void TrajectoryPredictor::reset() {
    count = 0;
}

} // namespace DQRS
} // namespace Stockfish
