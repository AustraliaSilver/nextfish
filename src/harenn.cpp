#include "harenn.h"
#include "position.h"
#include "bitboard.h"
#include "dee.h"
#include <algorithm>
#include <fstream>
#include <cmath>
#include <cstring>
#include <iostream>

namespace Stockfish {
namespace HARENN {

namespace {
    struct CacheEntry {
        Key key = 0;
        EvalResult res;
    };
    thread_local CacheEntry threadCache;

    // Use 512 to be safe, though model is 256
    thread_local int32_t h1_buffer[512];

    constexpr int32_t SCALE_W = 128;
    constexpr int32_t SCALE_OUT = 128 * 128;
}

Evaluator GuidanceProvider::evaluator;

Evaluator::Evaluator() : modelLoaded(false) {}
Evaluator::~Evaluator() {}

bool Evaluator::load_model(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
        std::cerr << "info string HARENN: Failed to open model file " << filename << std::endl;
        return false;
    }

    char magic[4];
    f.read(magic, 4);
    if (std::memcmp(magic, "HNN4", 4) != 0) {
        std::cerr << "info string HARENN: Invalid magic in " << filename << std::endl;
        return false;
    }

    f.read((char*)&eval_mean, 4);
    f.read((char*)&eval_std, 4);

    auto load_layer = [&](Layer& l) {
        f.read((char*)&l.rows, 4);
        f.read((char*)&l.cols, 4);
        l.weights.resize(l.rows * l.cols);
        l.bias.resize(l.rows);
        f.read((char*)l.weights.data(), l.weights.size() * 2);
        f.read((char*)l.bias.data(), l.bias.size() * 4);
    };

    load_layer(fc1);
    load_layer(eval_head);
    load_layer(tau_head);

    modelLoaded = true;
    std::cerr << "info string HARENN: Model loaded successfully (" 
              << fc1.rows << "x" << fc1.cols << ")" << std::endl;
    return true;
}

void Evaluator::input_layer_sparse_simd(const int* active_features, int count, const Layer& layer, int32_t* output) const {
    const int in_features = layer.rows; // 768 (transposed)
    const int out_features = layer.cols; // 256
    const int16_t* weights = layer.weights.data();
    const int32_t* bias = layer.bias.data();

    // 1. Init with bias
    std::memcpy(output, bias, out_features * sizeof(int32_t));

    // 2. Add active features
    for (int k = 0; k < count; ++k) {
        int feat_idx = active_features[k];
        if (feat_idx >= in_features) continue;

        const int16_t* feat_weights = &weights[feat_idx * out_features];
        for (int i = 0; i < out_features; ++i) {
            output[i] += feat_weights[i];
        }
    }
    
    // 3. ReLU
    for (int i = 0; i < out_features; ++i) {
        output[i] = std::max(0, output[i]);
    }
}

void Evaluator::compute_layer_simd(const int32_t* input, const Layer& layer, int32_t* output) const {
    const int in_features = layer.cols;
    const int16_t* weights = layer.weights.data();

    int64_t sum = layer.bias[0];
    for (int j = 0; j < in_features; ++j) {
        sum += (int64_t)input[j] * weights[j];
    }
    output[0] = (int32_t)(sum / SCALE_W);
}

EvalResult Evaluator::evaluate(const Position& pos) const {
    if (!modelLoaded) return {0.0f, 0.5f, 0.0f, 0.0f};

    int active_features[64]; // Increased size to prevent overflow
    int count = 0;
    for (Color c : {WHITE, BLACK}) {
        for (PieceType pt = PAWN; pt <= KING; ++pt) {
            Bitboard b = pos.pieces(c, pt);
            int piece_offset = (c == WHITE ? 0 : 6) + (pt - 1);
            while (b) {
                active_features[count++] = pop_lsb(b) * 12 + piece_offset;
            }
        }
    }

    int32_t out_e, out_t;
    input_layer_sparse_simd(active_features, count, fc1, h1_buffer);
    compute_layer_simd(h1_buffer, eval_head, &out_e);
    compute_layer_simd(h1_buffer, tau_head, &out_t);

    float final_eval = (float)out_e / (float)SCALE_OUT;
    float final_tau = 1.0f / (1.0f + std::exp(-(float)out_t / (float)SCALE_OUT));

    return { final_eval * eval_std + eval_mean, final_tau, 0.0f, 0.0f };
}

void GuidanceProvider::init() {
    evaluator.load_model("nextfish.harenn");
}

EvalResult GuidanceProvider::query(const Position& pos) {
    if (pos.key() == threadCache.key)
        return threadCache.res;

    threadCache.res = evaluator.evaluate(pos);
    threadCache.key = pos.key();
    return threadCache.res;
}

int GuidanceProvider::compute_reduction_adjustment(const Position& pos, Depth d, Move m, int r) {
    (void)pos; (void)d; (void)m;
    return r; 
}

int GuidanceProvider::compute_aspiration_delta(const Position& pos, int iter, int delta) {
    (void)pos; (void)iter;
    return delta;
}

} // namespace HARENN
} // namespace Stockfish
