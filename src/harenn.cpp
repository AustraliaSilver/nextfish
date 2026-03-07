#include "harenn.h"
#include "position.h"
#include <algorithm>

namespace Stockfish {
namespace HARENN {

Evaluator::Evaluator() : modelLoaded(false) {}
Evaluator::~Evaluator() {}

bool Evaluator::load_model(const std::string& filename) {
    (void)filename;
    modelLoaded = true;
    return true;
}

EvalResult Evaluator::evaluate(const Position& pos) const {
    EvalResult res = { 0.0f, 0.0f, 0.0f, 0.0f };
    
    // Optimization: only pieces count
    Bitboard pieces = pos.pieces();
    while (pieces) {
        Square s = pop_lsb(pieces);
        res.tau += popcount(pos.attackers_to(s, ~pos.side_to_move())) * 0.05f;
    }
    res.tau = std::min(0.8f, res.tau / 8.0f);
    res.horizonRisk = res.tau * 0.6f;
    res.resolutionScore = 1.0f - res.tau;

    return res;
}

static Evaluator globalEvaluator;

void GuidanceProvider::init() {
    globalEvaluator.load_model("harenn.model");
}

EvalResult GuidanceProvider::query(const Position& pos) {
    return globalEvaluator.evaluate(pos);
}

int GuidanceProvider::compute_reduction_adjustment(const Position& pos, Depth depth, Move m, int r) {
    (void)depth;
    (void)r;
    EvalResult res = query(pos);
    
    int adjustment = 0;
    if (res.horizonRisk > 0.8f) adjustment -= 1;
    if (res.tau > 0.7f) adjustment -= 1;
    
    return std::max(-2, std::min(2, adjustment));
}

int GuidanceProvider::compute_aspiration_delta(const Position& pos, int iteration, int currentDelta) {
    (void)iteration;
    EvalResult res = query(pos);
    
    float multiplier = 1.0f + res.tau * 0.35f; // More conservative
    return static_cast<int>(currentDelta * multiplier);
}

} // namespace HARENN
} // namespace Stockfish
