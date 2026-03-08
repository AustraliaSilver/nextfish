#include "harenn.h"
#include "position.h"
#include <algorithm>
#include <cmath>

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
    
    // HARENN v9: Fast tactical scan
    Bitboard pieces = pos.pieces();
    Bitboard contested = 0;
    Color us = pos.side_to_move();
    Color them = ~us;
    
    // Check for contested pieces using attackers_to
    Bitboard ourPieces = pos.pieces(us);
    while (ourPieces) {
        Square s = pop_lsb(ourPieces);
        if (pos.attackers_to(s, them)) contested |= s;
    }
    
    int contestedCount = popcount(contested);
    res.tau = std::min(0.95f, contestedCount * 0.12f);
    
    res.horizonRisk = (res.tau * 0.65f) + (popcount(pos.checkers()) * 0.35f);
    res.horizonRisk = std::clamp(res.horizonRisk, 0.01f, 0.99f);
    
    res.resolutionScore = 1.0f - (res.tau * 0.75f);

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
    (void)pos; (void)depth; (void)m;
    return 0; 
}

int GuidanceProvider::compute_aspiration_delta(const Position& pos, int iteration, int currentDelta) {
    (void)pos; (void)iteration;
    return currentDelta;
}

} // namespace HARENN
} // namespace Stockfish
