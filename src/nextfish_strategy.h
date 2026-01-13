#ifndef NEXTFISH_STRATEGY_H_INCLUDED
#define NEXTFISH_STRATEGY_H_INCLUDED

#include "types.h"
#include "position.h"
#include "search.h"

namespace Nextfish {

    // Tunable parameters
    extern double WhiteOptimism;
    extern double BlackLossPessimism;
    extern double BlackEqualPessimism;
    extern double VolatilityThreshold;
    extern double CodeRedLMR;
    extern double BlackLMR;
    extern double WhiteAggression;
    extern double PanicTimeFactor;
    extern double ComplexityScale;
    extern double SoftSingularityMargin;
    extern double TempoBonus;

    struct Advice {
        int reductionAdjustment;
        double reductionMultiplier; // Mới: Hệ số nhân để điều chỉnh LMR tinh tế hơn
        int optimismAdjustment;
        double timeScale;
    };

    class Strategy {
    public:
        static Advice consult(Stockfish::Color us, 
                              const Stockfish::Position& pos, 
                              const Stockfish::Search::Stack* ss, 
                              Stockfish::Depth depth, 
                              int moveCount);
        
        static double getTimeFactor(Stockfish::Color us);
    };

}

#endif // NEXTFISH_STRATEGY_H_INCLUDED