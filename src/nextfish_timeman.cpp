#include "nextfish_timeman.h"
#include "search.h"
#include "misc.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Nextfish {

using namespace Stockfish;

void TimeManager::init(const Search::LimitsType& limits, Color us, int ply [[maybe_unused]]) {
    startTime = limits.startTime;
    pulseFactor = 1.0;

    TimePoint timeLeft = limits.time[us];
    TimePoint inc = limits.inc[us];
    
    // Heritage v34 Logic: Công thức thời gian ổn định hơn
    int movesToGo = limits.movestogo ? limits.movestogo : 40;
    
    // Chimera update: slightly more aggressive base scale for faster play in openings
    double baseScale = 1.0 / std::min(movesToGo, 45); 
    
    baseOptimum = TimePoint(timeLeft * baseScale + inc * 0.6); // Increased inc usage
    baseMaximum = TimePoint(timeLeft * 0.75); // 75% max

    currentOptimum = baseOptimum;
    currentMaximum = baseMaximum;
}

TimePoint TimeManager::optimum() const {
    return TimePoint(currentOptimum * pulseFactor);
}

TimePoint TimeManager::maximum() const {
    // Allow up to 5x optimum in extreme cases, capped by baseMaximum
    return std::min(baseMaximum, TimePoint(currentOptimum * pulseFactor * 5.0));
}

void TimeManager::update_pulse(int bestMoveChanges, int scoreDiff) {
    // Chimera Pulse: More responsive to instability
    // bestMoveChanges: how many times the root best move changed
    // scoreDiff: how much the score swung (cp)

    double stabilityFactor = 1.0 + (std::min(bestMoveChanges, 15) * 0.08);
    double volatilityFactor = 1.0 + (std::min(std::abs(scoreDiff), 150) / 300.0);

    // If score is dropping rapidly (blunder detection?), use more time!
    if (scoreDiff < -20) {
        volatilityFactor *= 1.2;
    }

    pulseFactor = stabilityFactor * volatilityFactor;

    // Safety clamps
    pulseFactor = std::clamp(pulseFactor, 0.5, 3.5); 
}

TimePoint TimeManager::elapsed_time() const {
    return now() - startTime;
}

void TimeManager::clear() {
    pulseFactor = 1.0;
}

void TimeManager::advance_nodes_time(std::int64_t nodes [[maybe_unused]]) {}

}