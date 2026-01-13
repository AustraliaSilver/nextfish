#include "nextfish_strategy.h"
#include "tune.h"
#include <algorithm>
#include <cmath>

namespace Nextfish {

    // Tunable parameters for v67 Pulsar Evolution (SPSA Optimized)
    double WhiteOptimism = 21.56;
    double BlackLossPessimism = -17.14;
    double BlackEqualPessimism = -5.20;
    double VolatilityThreshold = 13.97;
    double CodeRedLMR = 63.48; 
    double BlackLMR = 87.85;   
    
    // New parameters for SPSA Discovery
    double WhiteAggression = 25.11;
    double PanicTimeFactor = 1.90;

    // v66 Evolution Parameters
    double ComplexityScale = 0.98;       
    double SoftSingularityMargin = -1.60; 
    double TempoBonus = -0.35;            

    Advice Strategy::consult(Stockfish::Color us, const Stockfish::Position& pos, const Stockfish::Search::Stack* ss, Stockfish::Depth [[maybe_unused]] depth, int [[maybe_unused]] moveCount) {
        Advice advice;
        
        // Game Phase & Complexity Calculation
        int totalMaterial = pos.non_pawn_material();
        double gamePhase = std::clamp(1.0 - (double(totalMaterial) / 7800.0), 0.0, 1.0);
        bool isComplex = totalMaterial > 5000 && pos.count<Stockfish::PAWN>(Stockfish::WHITE) + pos.count<Stockfish::PAWN>(Stockfish::BLACK) > 10;

        Stockfish::Value score = ss->staticEval;
        Stockfish::Value prevScore = (ss - 1)->staticEval;

        // 0. Complexity Scaling
        if (isComplex) {
            double scale = ComplexityScale; 
            if (us == Stockfish::BLACK && score < 0) scale *= 1.1;
        }

        // 1. Adaptive Optimism with Tempo Bonus
        double baseOptimism = (us == Stockfish::WHITE) ? WhiteOptimism : (score < 0 ? BlackLossPessimism : BlackEqualPessimism);
        
        if (us == Stockfish::WHITE && !pos.checkers()) {
             baseOptimism += (WhiteAggression - WhiteOptimism) * 0.2;
        }
        
        baseOptimism += TempoBonus;

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

        // 3. Code Red Search Logic with Singularity Margin
        bool evalDropped = (prevScore != Stockfish::VALUE_NONE) && (double(score) < double(prevScore) - VolatilityThreshold);

        if (ss->inCheck || evalDropped || heavyPressure || (us == Stockfish::BLACK && shieldBroken)) {
            advice.reductionMultiplier = CodeRedLMR / 100.0; 
            advice.reductionAdjustment = -1;
        } 
        else if (us == Stockfish::BLACK) {
            advice.reductionMultiplier = (BlackLMR + SoftSingularityMargin) / 100.0;
            advice.reductionAdjustment = 0;
        }
        else {
            advice.reductionMultiplier = (100.0 + SoftSingularityMargin) / 100.0; 
            advice.reductionAdjustment = 0;
        }

        return advice;
    }

    double Strategy::getTimeFactor(Stockfish::Color us) {
        return (us == Stockfish::BLACK) ? 1.35 : 0.80;
    }

}