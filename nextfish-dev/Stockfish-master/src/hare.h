#ifndef HARE_H_INCLUDED
#define HARE_H_INCLUDED

#include <cstdint>

#include "position.h"
#include "types.h"

namespace Stockfish::HARE {

struct Config {
    bool enabled = false;
    int  minDepth = 8;
    int  windowMarginCp = 24;
    int  tacticalScale = 10;
    int  quietBonus = 4;
    int  kingDangerScale = 10;
    int  criticalityScale = 8;
    int  horizonRiskScale = 8;
    int  checkBonus = 8;
    int  cascadeBudgetPct = 60;
    int  maxDeltaPly = 2;
    bool failLowVerifyEnabled = true;
    int  failLowWindowCp = 18;
    int  failLowMinReductionPly = 2;
    int  failLowVerifyDepthGain = 1;
};

struct NodeSnapshot {
    Depth depth = 0;
    Depth rootDepth = 0;
    int   ply = 0;
    Value alpha = VALUE_ZERO;
    Value beta = VALUE_ZERO;
    Value staticEval = VALUE_ZERO;
    Color us = WHITE;
    bool  inCheck = false;
    bool  cutNode = false;
    bool  pvNode = false;
    bool  improving = false;
};

struct MoveSnapshot {
    int  moveCount = 0;
    int  statScore = 0;
    bool capture = false;
    bool givesCheck = false;
    bool promotion = false;
};

struct Guidance {
    // 0..1000, negative means "not provided"
    int tacticalComplexity = -1;
    int moveCriticality = -1;
    int horizonRisk = -1;

    [[nodiscard]] bool valid() const {
        return tacticalComplexity >= 0 || moveCriticality >= 0 || horizonRisk >= 0;
    }
};

class GuidanceProvider {
   public:
    virtual ~GuidanceProvider() = default;
    virtual Guidance query(const Position&,
                           Move,
                           const NodeSnapshot&,
                           const MoveSnapshot&) const = 0;
};

class NullGuidanceProvider final : public GuidanceProvider {
   public:
    Guidance query(const Position&, Move, const NodeSnapshot&, const MoveSnapshot&) const override {
        return {};
    }
};

struct ReductionDecision {
    int  fixedDelta = 0;  // fixed-point unit (1024 = 1 ply)
    int  capFixed = -1;   // if >= 0, cap the final reduction to this value
    bool windowSensitiveActive = false;
};

ReductionDecision compute_reduction_adjustment(const Config&,
                                               const Position&,
                                               Move,
                                               const NodeSnapshot&,
                                               const MoveSnapshot&,
                                               int baseReductionFixed,
                                               int cumulativeReductionPly,
                                               const Guidance&);

}  // namespace Stockfish::HARE

#endif  // HARE_H_INCLUDED
