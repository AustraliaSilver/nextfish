#ifndef HARENN_H_INCLUDED
#define HARENN_H_INCLUDED

#include <vector>
#include <string>
#include <fstream>
#include "types.h"

namespace Stockfish {

class Position;

namespace HARENN {

struct EvalResult {
    float eval;              // Standard evaluation
    float tau;               // Tactical complexity
    float horizonRisk;       // Probability of horizon effect
    float resolutionScore;   // Quietness/Resolution of position
    // moveCriticality is moved to a separate buffer to prevent stack overflow
};

struct DetailedEvalResult : public EvalResult {
    float moveCriticality[64][64]; 
};

class Evaluator {
public:
    Evaluator();
    ~Evaluator();

    bool load_model(const std::string& filename);
    EvalResult evaluate(const Position& pos) const;

private:
    // Model weights and architecture details would go here
    // For now, we'll implement the interface and a placeholder/mock
    // that uses the standard NNUE but adds head predictions
    bool modelLoaded = false;
};

// Global HARENN provider for search guidance
class GuidanceProvider {
public:
    static void init();
    static EvalResult query(const Position& pos);
    
    // Adjust search parameters based on HARENN
    static int compute_reduction_adjustment(const Position& pos, Depth depth, Move m, int r);
    static int compute_aspiration_delta(const Position& pos, int iteration, int currentDelta);
};

} // namespace HARENN

} // namespace Stockfish

#endif // HARENN_H_INCLUDED
