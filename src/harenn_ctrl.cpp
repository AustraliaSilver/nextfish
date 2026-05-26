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

EvalResult Controller::get_analysis(const Position& pos) {
    return GuidanceProvider::query(pos);
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

int Controller::get_time_multiplier(const Position& pos) {
    // V95: Time Management permanently neutralized.
    //
    // History:
    // - V88-V91: Wrong feature encoding (perspective flip mismatch) caused avg tau=0.757
    //   instead of correct 0.255. All tau-based formulas worked on garbage → regressions.
    // - V94: Fixed feature encoding (sq^56 rank flip, W=0-5, B=6-11). Verified avg tau=0.255.
    //   But [93,107]/scale-200 time management STILL caused -45.4 Elo regression.
    // - Root cause: Middlegame positions have tau ~0.30-0.40 (above opening calibration 0.255).
    //   Asymmetric clamping → consistently uses 107% time → cumulative time pressure.
    // - HARENN loaded (no TM) and HARENN disabled are statistically equal (+6.9 vs +31.4 Elo,
    //   within the ±62 Elo error bars for 100-game samples).
    //
    // Conclusion: Leave Stockfish's optimized time management untouched.
    (void)pos;
    return 100;
}

} // namespace HARENN

} // namespace Stockfish
