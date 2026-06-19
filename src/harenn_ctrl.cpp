#include "harenn_ctrl.h"
#include "harenn.h"
#include "bitboard.h"
#include "position.h"
#include "ucioption.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace Stockfish {

namespace HARENN {

namespace {
    // Tunable parameters, refreshed from UCI options at start of search
    float tm_center      = 0.35f;
    float tm_slope       = 14.3f;
    float tm_range_min   = 95.0f;
    float tm_range_max   = 105.0f;
    float ext_threshold_white = 0.8228f;
    float ext_threshold_black = 0.7060f;
}

void Controller::init() {
    GuidanceProvider::init();
}

void Controller::refresh_params(const OptionsMap& options) {
    tm_center      = options["HARE TM Center"] * 0.01f;
    tm_slope       = options["HARE TM Slope"] * 0.1f;
    tm_range_min   = (float)(int)options["HARE TM Range Min"];
    tm_range_max   = (float)(int)options["HARE TM Range Max"];
    ext_threshold_white = options["HARE Ext Threshold White"] * 0.001f;
    ext_threshold_black = options["HARE Ext Threshold Black"] * 0.001f;
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
    const float threshold = isBlack ? ext_threshold_black : ext_threshold_white;
    if (res.rho > threshold || res.rs < (1.0f - threshold)) {
        return 1;
    }

    return 0;
}

int Controller::get_time_multiplier(const Position& pos) {
    if (!GuidanceProvider::is_model_loaded()) return 100;

    EvalResult res = get_analysis(pos, NumaReplicatedAccessToken(0));

    float mult = 100.0f + (res.tau - tm_center) * tm_slope;
    return int(std::clamp(mult, tm_range_min, tm_range_max));
}

} // namespace HARENN

} // namespace Stockfish
