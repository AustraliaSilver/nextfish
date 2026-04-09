#include "harenn.h"
#include "position.h"
#include "bitboard.h"
#include <algorithm>

namespace Stockfish {
namespace HARENN {

Evaluator::Evaluator() :
    modelLoaded(false) {}
Evaluator::~Evaluator() {}

bool Evaluator::load_model(const std::string& filename) {
    (void) filename;
    modelLoaded = true;
    return true;
}

EvalResult Evaluator::evaluate(const Position& pos) const {
    EvalResult res = {0.0f, 0.0f, 0.0f, 0.0f};

    Color    us  = pos.side_to_move();
    Bitboard occ = pos.pieces();

    int      attacks = 0;
    Bitboard tmp     = pos.pieces(us);
    while (tmp)
    {
        Square s = pop_lsb(tmp);
        if (pos.attackers_to(s, occ) & pos.pieces(~us))
            attacks++;
    }
    tmp = pos.pieces(~us);
    while (tmp)
    {
        Square s = pop_lsb(tmp);
        if (pos.attackers_to(s, occ) & pos.pieces(us))
            attacks++;
    }

    // Conservative tau: only meaningful at higher attack counts
    res.tau             = std::min(0.95f, attacks * 0.10f + (attacks >= 5 ? 0.10f : 0.0f));
    res.horizonRisk     = popcount(pos.checkers()) * 0.35f + res.tau * 0.45f;
    res.resolutionScore = 1.0f - res.tau * 0.65f - (attacks > 7 ? 0.10f : 0.0f);

    int      materialDiff = 0;
    Bitboard ourPieces    = pos.pieces(us);
    Bitboard theirPieces  = pos.pieces(~us);
    Bitboard ourTmp       = ourPieces;
    while (ourTmp)
    {
        Square s     = pop_lsb(ourTmp);
        Piece  piece = pos.piece_on(s);
        materialDiff += PieceValue[piece];
    }
    Bitboard theirTmp = theirPieces;
    while (theirTmp)
    {
        Square s     = pop_lsb(theirTmp);
        Piece  piece = pos.piece_on(s);
        materialDiff -= PieceValue[piece];
    }
    res.eval = static_cast<float>(materialDiff);

    return res;
}

static Evaluator globalEvaluator;

void GuidanceProvider::init() { globalEvaluator.load_model("harenn.model"); }

EvalResult GuidanceProvider::query(const Position& pos) { return globalEvaluator.evaluate(pos); }

int GuidanceProvider::compute_reduction_adjustment(const Position& pos,
                                                   Depth           depth,
                                                   Move            m,
                                                   int             r) {
    (void) pos;
    (void) depth;
    (void) m;
    (void) r;
    return 0;
}

int GuidanceProvider::compute_aspiration_delta(const Position& pos,
                                               int             iteration,
                                               int             currentDelta) {
    (void) pos;
    (void) iteration;
    return currentDelta;
}

}  // namespace HARENN
}  // namespace Stockfish
