#pragma once

#include "types.h"
#include "position.h"

namespace NextFish {

struct DEEResult {
    Value total_value = 0;       // Combined exchange value
    Value see_value = 0;         // Static material exchange value
    Value structural_delta = 0;  // Pawn structure / open files aftermath
    Value dynamic_delta = 0;     // King safety & piece activity
    double tension_energy = 0.0; // Threat-as-asset value
};

class DEEEngine {
public:
    DEEEngine() = default;

    // Full Dynamic Exchange Evaluation of a candidate move
    DEEResult evaluate_exchange(const Position& pos, Move m) const;

    // Quick score for move ordering
    Value score_move(const Position& pos, Move m) const;
};

extern DEEEngine GlobalDEE;

} // namespace NextFish
