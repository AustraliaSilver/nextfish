/*
  Lightweight AAW-X Phase 1 helper.
  Keeps aspiration tuning logic isolated from search.cpp.
*/

#ifndef AAWX_PHASE1_H_INCLUDED
#define AAWX_PHASE1_H_INCLUDED

#include <algorithm>
#include <cmath>

namespace Stockfish::AAWX {

struct Phase1Input {
    int baseDelta;
    int minDelta;
    int maxDelta;
    int avgScore;
    int prevScore;
    int meanSquaredScore;
    int trendAsymmetry;
    int baseMaxAttempts;
    bool conservativeSide;
    double timePressure;
    int targetConfidence;
    int sigmaBlend;
    int trendCap;
};

struct Phase1Output {
    int delta;
    int lo;
    int hi;
    int maxAttempts;
};

inline Phase1Output compute_phase1(const Phase1Input& in) {

    const int volatility = std::abs(in.avgScore - in.prevScore);
    const double rms = std::sqrt(double(std::max(0, std::abs(in.meanSquaredScore))));

    const double sigma =
      0.55 * double(volatility) + 0.45 * rms;
    const double conf = std::clamp(double(in.targetConfidence), 60.0, 95.0);
    const double z = 0.8 + (conf - 60.0) * 0.025;
    const double pressureScale = std::clamp(in.timePressure, 0.7, 1.8);

    const int adaptiveDelta = int(std::round(z * sigma * std::clamp(in.sigmaBlend, 0, 40) / 100.0));
    int delta = (in.baseDelta * 90 + adaptiveDelta * 10) / 100;
    delta = int(std::round(delta * pressureScale));
    delta = std::clamp(delta, in.minDelta, in.maxDelta);

    int lo = delta;
    int hi = delta;

    const int trend = in.avgScore - in.prevScore;
    if (std::abs(trend) >= 35)
    {
        int asym = (std::abs(trend) * in.trendAsymmetry) / 256;
        asym = std::min(asym, std::max(0, in.trendCap));

        if (trend > 0)
            hi += asym;
        else
            lo += asym;
    }

    int maxAttempts = in.baseMaxAttempts;
    if (volatility > 120 || sigma > 180.0)
        maxAttempts = std::max(3, in.baseMaxAttempts - 1);
    if (in.conservativeSide)
        maxAttempts = std::max(3, maxAttempts - 1);

    return Phase1Output{delta, lo, hi, maxAttempts};
}

}  // namespace Stockfish::AAWX

#endif
