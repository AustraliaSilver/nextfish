#pragma once

#include "types.h"
#include "position.h"
#include "harenn.h"

namespace NextFish {

class HAREEngine {
public:
    HAREEngine();

    // Compute dynamic reduction for candidate move
    int compute_reduction(const Position& pos, Move m, int move_idx, int depth,
                          const HARENNOutput& harenn_out, bool is_pv, bool is_killer,
                          bool improving = true, int history_score = 0) const;

private:
    int lmr_table[64][64];
};

extern HAREEngine GlobalHARE;

} // namespace NextFish
