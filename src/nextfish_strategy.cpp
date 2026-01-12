#include "nextfish_strategy.h"
#include "tune.h"
#include <algorithm>
#include <cmath>

namespace Nextfish {

    // Tunable parameters for v65 Auto-Learn (SPSA Driven)
    double WhiteOptimism = 20.85;
    double BlackLossPessimism = -16.77;
    double BlackEqualPessimism = -5.0;
    double VolatilityThreshold = 13.83;
    double CodeRedLMR = 63.31; 
    double BlackLMR = 87.90;   
    
    // New parameters for SPSA Discovery
    double WhiteAggression = 25.00;
    double PanicTimeFactor = 2.00;

    Advice Strategy::consult(Stockfish::Color us, const Stockfish::Position& pos, const Stockfish::Search::Stack* ss, Stockfish::Depth depth, int moveCount) {
        Advice advice;
        
        // Game Phase Calculation
        int totalMaterial = pos.non_pawn_material();
        double gamePhase = std::clamp(1.0 - (double(totalMaterial) / 7800.0), 0.0, 1.0);

        Stockfish::Value score = ss->staticEval;
        Stockfish::Value prevScore = (ss - 1)->staticEval;

        // 1. Adaptive Optimism
        // Logic will now use WhiteAggression as a potential boost
        double baseOptimism = (us == Stockfish::WHITE) ? WhiteOptimism : (score < 0 ? BlackLossPessimism : BlackEqualPessimism);
        
        // Boost White optimism slightly if King is safe (using WhiteAggression as a reference)
        if (us == Stockfish::WHITE && !pos.checkers()) {
             baseOptimism += (WhiteAggression - WhiteOptimism) * 0.2;
        }

        advice.optimismAdjustment = int(baseOptimism * (1.0 - gamePhase * 0.3));

        // 2. Adaptive King Safety & Pawn Shield
        Stockfish::Square ksq = pos.square<Stockfish::KING>(us);
        Stockfish::Bitboard enemyHeavy = pos.pieces(~us, Stockfish::ROOK, Stockfish::QUEEN);
        bool heavyPressure = (pos.attackers_to(ksq) & enemyHeavy) != 0;

        // Smart Shield Detection
        Stockfish::Bitboard shield = 0;
        Stockfish::File kf = Stockfish::file_of(ksq);
        if (kf >= Stockfish::FILE_F) 
            shield = (us == Stockfish::WHITE) ? 0xE000ULL : 0x00E0000000000000ULL;
        else if (kf <= Stockfish::FILE_C) 
            shield = (us == Stockfish::WHITE) ? 0x0007ULL : 0x0007000000000000ULL;
        
        bool shieldBroken = (shield != 0) && (Stockfish::popcount(pos.pieces(us, Stockfish::PAWN) & shield) < 2);

        // 3. Code Red Search Logic
        bool evalDropped = (prevScore != Stockfish::VALUE_NONE) && (double(score) < double(prevScore) - VolatilityThreshold);

        if (ss->inCheck || evalDropped || heavyPressure || (us == Stockfish::BLACK && shieldBroken)) {
            advice.reductionMultiplier = CodeRedLMR / 100.0; 
            advice.reductionAdjustment = -1;
        } 
        else if (us == Stockfish::BLACK) {
            advice.reductionMultiplier = BlackLMR / 100.0;
            advice.reductionAdjustment = 0;
        }
        else {
            advice.reductionMultiplier = 1.0; 
            advice.reductionAdjustment = 0;
        }

        return advice;
    }

    double Strategy::getTimeFactor(Stockfish::Color us) {
        // Use PanicTimeFactor if position is unstable (score dropping)
        // For simplicity in getTimeFactor, we keep a base, 
        // the dynamic part is usually handled in Search::time_management.
        return (us == Stockfish::BLACK) ? 1.35 : 0.80;
    }

}
