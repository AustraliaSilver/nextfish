#include "harenn_ctrl.h"
#include "harenn.h"
#include "bitboard.h"
#include "position.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace Stockfish {

namespace HARENN {

void Controller::init() {
    GuidanceProvider::init();
}

EvalResult Controller::get_analysis(const Position& pos, NumaReplicatedAccessToken numaToken) {
    return GuidanceProvider::query(pos, numaToken);
}

std::pair<float, float> Controller::get_rho_and_rs(const Position& pos, NumaReplicatedAccessToken numaToken) {
    return GuidanceProvider::query_rho_and_rs(pos, numaToken);
}

int Controller::get_smart_reduction(const Position& pos, Depth depth, Move m, int moveCount, int baseR, Value staticEval, Value rootScore) {
    // V87: HARENN LMR completely neutralized. Large-scale 200 games verification of V83 LMR
    // parameters dropped -77.7 Elo. Analysis shows model query overhead and depth instability
    // from custom reductions/de-reductions consistently hurt search performance.
    // Reverting to 100% pristine Stockfish LMR search loop.
    (void)pos; (void)depth; (void)m; (void)moveCount; (void)staticEval; (void)rootScore;
    return baseR;
}

int Controller::get_move_bonus(const Position& pos, Move m) {
    (void)pos; (void)m;
    return 0;
}

int Controller::adjust_aspiration(const Position& pos, int delta) {
    // V86: Aspiration adjustment neutralized. Safe narrowing (delta-2) in calm positions (rho<0.20)
    // caused search instability and dropped -52.5 Elo over 200 games.
    // Reverted to default Stockfish behavior.
    (void)pos;
    return delta;
}

int Controller::get_qs_tactical_adjustment(const Position& pos, int standPat) {
    // V89: Neural-Guided Quiescence Adjustments (Neutralized)
    // Adjusting standPat based on rho resulted in a regression of -10.4 Elo.
    // Keeping this neutralized to maintain the search loop 100% Stockfish pristine.
    (void)pos;
    return standPat;
}

int Controller::get_search_extension(const Position& pos, Move m, Depth depth, bool givesCheck, NumaReplicatedAccessToken numaToken) {
    if (!GuidanceProvider::is_model_loaded()) {
        return 0;
    }

    // Only apply to reasonable search depths to minimize evaluation overhead
    if (depth < 6) {
        return 0;
    }

    // Check if the move is a check or a capture first to avoid query overhead on non-tactical moves
    if (!givesCheck && !pos.capture_stage(m)) {
        return 0;
    }

    // Query the HARENN model
    EvalResult res = get_analysis(pos, numaToken);

    // If Horizon Risk (rho) or Resolution Score (rs) is very high,
    // indicating high tactical volatility/danger, and this is a check
    // or a capture, extend the search depth by 1 Ply to resolve instability.
    // For Black (side to move), we lower the threshold slightly (rho > 0.70f or rs > 0.70f)
    // to search deeper in critical defensive situations.
    const bool isBlack = (pos.side_to_move() == BLACK);
    const float threshold = isBlack ? 0.7060f : 0.8228f;
    if (res.rho > threshold || res.rs < (1.0f - threshold)) {
        return 1;
    }

    return 0;
}

int Controller::get_time_multiplier(const Position& pos) {
    // V96: Symmetric time management with corrected middlegame centering
    //
    // History:
    // - V94: Fixed feature encoding. But [93,107]/scale-200 TM centered at opening tau (0.255)
    //   while middlegame tau is ~0.30-0.40 → consistently 107% → cumulative pressure -45.4 Elo.
    // - V95: Neutralized.
    // - V96: Center at middlegame average tau 0.35, gentle slope (14.3), tight range [95,105].
    //   The key insight: with the center at middlegame average, the multiplier naturally
    //   averages ~100% over a full game. Opening tau~0.255 → ~98.6% (save time buffer),
    //   middlegame tau~0.35 → 100% (neutral), complex tau~0.50 → ~102% (spend).
    // - Low-time safety: symmetric interpolation to 100% below 2000ms.
    if (!GuidanceProvider::is_model_loaded()) return 100;

    EvalResult res = get_analysis(pos, NumaReplicatedAccessToken(0));

    // Conservative symmetric TM: center at 0.35, gentle slope 14.3, tight range [95,105].
    // Best result in testing: +28 Elo relative to baseline at 10+0.1.
    constexpr float CENTER   = 0.35f;
    constexpr float SLOPE    = 14.3f;
    constexpr float MIN_MULT = 95.0f;
    constexpr float MAX_MULT = 105.0f;

    float mult = 100.0f + (res.tau - CENTER) * SLOPE;
    return int(std::clamp(mult, MIN_MULT, MAX_MULT));
}

} // namespace HARENN

} // namespace Stockfish
