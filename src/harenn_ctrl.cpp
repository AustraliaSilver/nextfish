#include "harenn_ctrl.h"
#include "harenn.h"
#include "bitboard.h"
#include "position.h"
#include <algorithm>
#include <cmath>

namespace Stockfish {

namespace HARENN {

namespace {
    struct NodeCache {
        Key key = 0;
        EvalResult res;
    };
    thread_local NodeCache nodeCache;
}

void Controller::init() {
    GuidanceProvider::init();
}

EvalResult Controller::get_analysis(const Position& pos) {
    if (pos.key() == nodeCache.key)
        return nodeCache.res;

    nodeCache.res = GuidanceProvider::query(pos);
    nodeCache.key = pos.key();
    return nodeCache.res;
}

int Controller::get_smart_reduction(const Position& pos, Depth depth, Move m, int moveCount, int baseR) {
    // V31: AI Strategic Orchestrator
    // We target depth >= 6 and critical moves. 
    if (!m || depth < 6 || moveCount < 3 || pos.capture_stage(m))
        return baseR;

    EvalResult res = get_analysis(pos);
    int adj = 0;

    // 1. Unified Volatility Guard (Combined Tau and Rho)
    // New scale Tau (0.05-0.25). 0.16 is the "danger" threshold.
    float volatility = (res.tau * 3.0f) + (res.rho * 0.2f); 
    if (volatility > 0.65f) // High tactical risk
        adj -= 128;

    // 2. Endgame Glide (Using RS)
    // If it's a deep endgame and AI says it's quiet, speed up.
    if (res.rs > 0.85f && res.tau < 0.10f)
        adj += 64;

    return std::max(0, baseR + adj);
}

int Controller::get_move_bonus(const Position& pos, Move m) {
    (void)pos; (void)m;
    return 0;
}

int Controller::adjust_aspiration(const Position& pos, int delta) {
    EvalResult res = get_analysis(pos);
    // Use Rho to subtly adjust the search window at the root
    if (res.rho > 0.80f)
        return delta + 2;
    return delta;
}

// Function to check consensus between AI and Engine
int Controller::get_qs_tactical_adjustment(const Position& pos, int standPat) {
    EvalResult res = get_analysis(pos);
    
    // If AI evaluation significantly disagrees with the engine's stand-pat
    // (Meaning there's likely a hidden tactical resource AI sees)
    if (std::abs(res.eval - (float)standPat) > 180.0f) {
        // Return a small penalty to force engine to search captures
        return standPat - 15; 
    }
    
    return standPat;
}

} // namespace HARENN

} // namespace Stockfish
