#include "hare.h"

#include <algorithm>
#include <cstdlib>

#include "bitboard.h"

namespace Stockfish::HARE {

namespace {

constexpr int FIXED_ONE_PLY = 1024;

int clamp_unit(int v) { return std::clamp(v, 0, 1000); }

int quick_tactical_complexity(const Position& pos, const NodeSnapshot& node, const MoveSnapshot& mv) {
    int complexity = 0;

    if (node.inCheck)
        complexity += 260;
    if (mv.givesCheck)
        complexity += 180;
    if (mv.capture)
        complexity += 120;
    if (mv.promotion)
        complexity += 260;

    const Square ourKingSq   = pos.square<KING>(node.us);
    const Square theirKingSq = pos.square<KING>(~node.us);
    const int    ourKingAttackers =
      popcount(pos.attackers_to(ourKingSq) & pos.pieces(~node.us));
    const int theirKingAttackers =
      popcount(pos.attackers_to(theirKingSq) & pos.pieces(node.us));

    complexity += (ourKingAttackers + theirKingAttackers) * 70;

    if (mv.moveCount > 10)
        complexity += std::min(120, (mv.moveCount - 10) * 10);

    return clamp_unit(complexity);
}

}  // namespace

ReductionDecision compute_reduction_adjustment(const Config&       cfg,
                                               const Position&     pos,
                                               Move               move,
                                               const NodeSnapshot& node,
                                               const MoveSnapshot& mv,
                                               int                 baseReductionFixed,
                                               int                 cumulativeReductionPly,
                                               const Guidance&     guidance) {
    ReductionDecision out{};

    if (!cfg.enabled || node.depth < cfg.minDepth)
        return out;

    const int alphaGap = std::abs(int(node.staticEval - node.alpha));
    const int betaGap  = std::abs(int(node.staticEval - node.beta));
    const bool nearWindow =
      alphaGap <= cfg.windowMarginCp || betaGap <= cfg.windowMarginCp;
    out.windowSensitiveActive = nearWindow;

    // Phase-1 conservative rule: only touch reductions near alpha/beta window.
    if (!nearWindow)
        return out;

    int tactical = quick_tactical_complexity(pos, node, mv);
    int criticality = tactical / 2;
    int horizon = tactical / 3;

    if (guidance.valid())
    {
        if (guidance.tacticalComplexity >= 0)
            tactical = clamp_unit(guidance.tacticalComplexity);
        if (guidance.moveCriticality >= 0)
            criticality = clamp_unit(guidance.moveCriticality);
        if (guidance.horizonRisk >= 0)
            horizon = clamp_unit(guidance.horizonRisk);
    }

    int tension = (cfg.tacticalScale * tactical + cfg.criticalityScale * criticality
                   + cfg.horizonRiskScale * horizon)
                / 1000;

    tension += cfg.checkBonus * int(mv.givesCheck || mv.promotion);
    tension += cfg.kingDangerScale * int(node.inCheck);

    // Strongly negative history often flags tactical/defensive moves that are underexplored.
    if (mv.statScore < -3000)
        tension += cfg.quietBonus * 2;

    // Convert to reduction delta in plies (negative means reduce less).
    int deltaPly = -(tension / 10);
    deltaPly     = std::clamp(deltaPly, -cfg.maxDeltaPly, cfg.maxDeltaPly);
    out.fixedDelta = deltaPly * FIXED_ONE_PLY;

    // Cascade limiter to avoid over-reduction collapse on a line.
    const int budgetPly = std::max(2, int(node.rootDepth) * cfg.cascadeBudgetPct / 100);
    const int remaining = budgetPly - cumulativeReductionPly;
    if (remaining <= 0)
        out.capFixed = FIXED_ONE_PLY;
    else
    {
        const int capPly = std::max(1, remaining);
        out.capFixed     = std::max(FIXED_ONE_PLY, capPly * FIXED_ONE_PLY);
    }

    // Avoid producing a negative final reduction through the cap path.
    if (baseReductionFixed + out.fixedDelta < -FIXED_ONE_PLY)
        out.fixedDelta = -FIXED_ONE_PLY - baseReductionFixed;

    (void) move;
    return out;
}

}  // namespace Stockfish::HARE
