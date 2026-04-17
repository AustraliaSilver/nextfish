#ifndef HARENN_H_INCLUDED
#define HARENN_H_INCLUDED

#include "types.h"
#include <string>
#include <vector>

namespace Stockfish {

class Position; // Forward declaration

namespace HARENN {

struct EvalResult {
    float eval;
    float tau;
    float rho;
    float rs;
};

struct Layer {
    int rows, cols;
    std::vector<int16_t> weights;
    std::vector<int32_t> bias;
};

class Network {
public:
    bool load(const std::string& filename);
    EvalResult forward(const int* active_features, int count) const;

private:
    float eval_mean, eval_std;
    Layer fc1;
    Layer eval_head;
    Layer tau_head;
    Layer rho_head;
    Layer rs_head;
};

class GuidanceProvider {
public:
    static void init();
    static EvalResult query(const Position& pos);
};

} // namespace HARENN

} // namespace Stockfish

#endif // HARENN_H_INCLUDED
