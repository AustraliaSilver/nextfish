#include "hare.h"
#include <cmath>
#include <algorithm>

namespace NextFish {

HAREEngine GlobalHARE;

HAREEngine::HAREEngine() {
    for (int d = 1; d < 64; ++d) {
        for (int m = 1; m < 64; ++m) {
            lmr_table[d][m] = static_cast<int>(0.80 + std::log(d) * std::log(m) / 2.05);
        }
    }
}

int HAREEngine::compute_reduction(const Position& pos, Move m, int move_idx, int depth,
                                  const HARENNOutput& harenn_out, bool is_pv, bool is_killer,
                                  bool improving, int history_score) const {
    if (depth < 3 || move_idx < 2 || pos.is_in_check() || pos.gives_check(m)) {
        return 0;
    }

    int d_idx = std::min(63, depth);
    int m_idx = std::min(63, move_idx);
    int r = lmr_table[d_idx][m_idx];

    // Non-PV nodes get deeper reduction (Cut-node refutation)
    if (!is_pv) {
        r++;
    } else {
        r--;
    }

    // Improving heuristic
    if (!improving) {
        r++;
    }

    // History heuristic (bounded: bad history reduces, good history extends)
    int hist_term = history_score / 8000;
    r -= std::clamp(hist_term, -1, 1);

    if (is_killer) r -= 2;

    // Modulate based on HARENN outputs: reduce less if tactical complexity or horizon risk is high
    if (harenn_out.tactical_complexity > 0.65) {
        r--;
    }
    if (harenn_out.horizon_risk > 0.55) {
        r--;
    }

    return std::clamp(r, 0, depth - 2);
}

} // namespace NextFish
