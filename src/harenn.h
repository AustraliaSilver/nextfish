#ifndef HARENN_H_INCLUDED
#define HARENN_H_INCLUDED

#include <vector>
#include <string>
#include <cstdint>
#include "types.h"

namespace Stockfish {

class Position;

namespace HARENN {

struct EvalResult {
    float eval;              
    float tau;               
    float horizonRisk;       
    float resolutionScore;   
};

struct Layer {
    std::vector<int16_t> weights;
    std::vector<int32_t> bias;
    int rows, cols;
};

class Evaluator {
public:
    Evaluator();
    ~Evaluator();

    bool load_model(const std::string& filename);
    EvalResult evaluate(const Position& pos) const;

private:
    bool modelLoaded = false;
    float eval_mean = 0.0f;
    float eval_std = 1.0f;
    Layer fc1, eval_head, tau_head;

    void compute_layer_simd(const int32_t* input, const Layer& layer, int32_t* output) const;
    void input_layer_sparse_simd(const int* active_features, int count, const Layer& layer, int32_t* output) const;
};

class GuidanceProvider {
public:
    static void init();
    static EvalResult query(const Position& pos);
    
    static int compute_reduction_adjustment(const Position& pos, Depth depth, Move m, int r);
    static int compute_aspiration_delta(const Position& pos, int iteration, int currentDelta);

private:
    static Evaluator evaluator;
};

} // namespace HARENN
} // namespace Stockfish

#endif // HARENN_H_INCLUDED
