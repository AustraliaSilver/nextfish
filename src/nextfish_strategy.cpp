#include "nextfish_strategy.h"
#include "tune.h"
#include <algorithm>
#include <cmath>

namespace Nextfish {

    // Tunable parameters for v63 Singularity Reborn (Dynamic & Non-linear)
    double WhiteAggression = 25.0;
    double WhiteCaution = 5.0;
    double BlackPessimism = -15.0;
    
    // Time Management Dynamic Factors
    double PanicTimeFactor = 2.0;
    double NormalTimeFactor = 0.9;

    // Pruning Factors
    double DrawishLMR = 110.0; // 1.10x
    double ComplexLMR = 60.0;  // 0.60x

    Advice Strategy::consult(Color us, const Position& pos, const Search::Stack* ss, Depth depth, int moveCount) {
        Advice advice;
        
        Value score = ss->staticEval;
        Value prevScore = (ss - 1)->staticEval;

        // 1. Conditional Optimism (King Safety Awareness)
        // Only be aggressive if our King is safe.
        Square ksq = pos.square<KING>(us);
        Bitboard enemyPieces = pos.pieces(~us);
        bool kingSafe = (pos.attackers_to(ksq) & enemyPieces) == 0;

        double baseOptimism;
        if (us == WHITE) {
            baseOptimism = kingSafe ? WhiteAggression : WhiteCaution;
        } else {
            baseOptimism = BlackPessimism;
        }
        
        advice.optimismAdjustment = int(baseOptimism);

        // 2. Selective Pruning (Anti-Engine Horizon)
        bool evalDropped = (prevScore != VALUE_NONE) && (double(score) < double(prevScore) - 20.0);
        bool isDrawish = std::abs(score) < 50 && pos.non_pawn_material(WHITE) == pos.non_pawn_material(BLACK);

        if (ss->inCheck || evalDropped) {
            // Panic / Complexity: Go extremely deep
            advice.reductionMultiplier = ComplexLMR / 100.0;
            advice.reductionAdjustment = -2;
        } 
        else if (isDrawish && !kingSafe) {
             // Drawish but unsafe: Normal search
            advice.reductionMultiplier = 1.0;
            advice.reductionAdjustment = 0;
        }
        else if (isDrawish) {
            // Drawish and safe: Prune more to save time for critical moments
            advice.reductionMultiplier = DrawishLMR / 100.0;
            advice.reductionAdjustment = 1;
        }
        else {
            advice.reductionMultiplier = 1.0;
            advice.reductionAdjustment = 0;
        }

        return advice;
    }

    double Strategy::getTimeFactor(Color us) {
        // v63: Dynamic Time Factor is handled inside search.cpp logic usually, 
        // but here we set a base baseline. 
        // We will return a higher base for Black to allow "PanicTimeFactor" logic to scale up from a higher ground.
        return (us == BLACK) ? 1.15 : 0.95;
    }

    double Strategy::getTimeFactor(Color us) {
        // v58: Final push for Black's survival resource.
        return (us == BLACK) ? 1.35 : 0.80;
    }

}
