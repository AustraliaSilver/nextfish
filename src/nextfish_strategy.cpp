#include "nextfish_strategy.h"
#include "tune.h"
#include <algorithm>
#include <cmath>

namespace Nextfish {

    using namespace Stockfish;

    // Tunable parameters for v62 Plasma (High Precision)
    double WhiteOptimism = 20.85;
    double BlackLossPessimism = -16.77;
    double BlackEqualPessimism = -5.0;
    double VolatilityThreshold = 13.83;
    double CodeRedLMR = 63.31; 
    double BlackLMR = 87.90;   

    // TUNE macro in Stockfish is designed for int. We keep them as internal doubles for manual precision.
    // To enable tuning in the future with doubles, we would need to overload the TUNE macro, 
    // but for now we hardcode the optimal SPSA values.

    using namespace Stockfish;

    Advice Strategy::consult(Color us, const Position& pos, const Search::Stack* ss, Depth depth, int moveCount) {
        Advice advice;
        
        // Game Phase Calculation
        int totalMaterial = pos.non_pawn_material();
        double gamePhase = std::clamp(1.0 - (double(totalMaterial) / 7800.0), 0.0, 1.0);

        Value score = ss->staticEval;
        Value prevScore = (ss - 1)->staticEval;

        // 1. Pulsar Optimism (Dynamic & High Precision)
        double baseOptimism = (us == WHITE) ? WhiteOptimism : (score < 0 ? BlackLossPessimism : BlackEqualPessimism);
        
        // Round only at the very last step
        advice.optimismAdjustment = int(baseOptimism * (1.0 - gamePhase * 0.3));

        // 2. Adaptive King Safety & Pawn Shield
        Square ksq = pos.square<KING>(us);
        Bitboard enemyHeavy = pos.pieces(~us, ROOK, QUEEN);
        bool heavyPressure = (pos.attackers_to(ksq) & enemyHeavy) != 0;

        // Smart Shield Detection
        Bitboard shield = 0;
        File kf = file_of(ksq);
        if (kf >= FILE_F) // King side (f, g, h)
            shield = (us == WHITE) ? 0xE000ULL : 0x00E0000000000000ULL;
        else if (kf <= FILE_C) // Queen side (a, b, c)
            shield = (us == WHITE) ? 0x0007ULL : 0x0007000000000000ULL;
        
        bool shieldBroken = (shield != 0) && (Stockfish::popcount(pos.pieces(us, PAWN) & shield) < 2);

        // 3. Code Red Precision Search
        // Use double for threshold comparison
        bool evalDropped = (prevScore != VALUE_NONE) && (double(score) < double(prevScore) - VolatilityThreshold);

        if (ss->inCheck || evalDropped || heavyPressure || (us == BLACK && shieldBroken)) {
            advice.reductionMultiplier = CodeRedLMR / 100.0; 
            advice.reductionAdjustment = -1;
        } 
        else if (us == BLACK) {
            advice.reductionMultiplier = BlackLMR / 100.0;
            advice.reductionAdjustment = 0;
        }
        else {
            advice.reductionMultiplier = 1.0; 
            advice.reductionAdjustment = 0;
        }

        return advice;
    }

    double Strategy::getTimeFactor(Color us) {
        // v58: Final push for Black's survival resource.
        return (us == BLACK) ? 1.35 : 0.80;
    }

}
