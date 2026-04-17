#include "harenn.h"
#include "position.h"
#include "bitboard.h"
#include "dee.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cmath>
#include <vector>

namespace Stockfish {

namespace HARENN {

namespace {
    float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }
}

bool Network::load(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return false;

    char magic[4];
    f.read(magic, 4);
    if (std::string(magic, 4) != "HNN4") return false;

    f.read((char*)&eval_mean, 4);
    f.read((char*)&eval_std, 4);

    auto load_layer = [&](Layer& layer) {
        f.read((char*)&layer.rows, 4);
        f.read((char*)&layer.cols, 4);
        int size = layer.rows * layer.cols;
        layer.weights.resize(size);
        f.read((char*)layer.weights.data(), size * 2);
        layer.bias.resize(layer.cols);
        f.read((char*)layer.bias.data(), layer.cols * 4);
    };

    load_layer(fc1);
    load_layer(eval_head);
    load_layer(tau_head);
    load_layer(rho_head);
    load_layer(rs_head);

    return true;
}

EvalResult Network::forward(const int* active_features, int count) const {
    // 1. Input Layer (Sparse Sequential Access)
    const int out_features = fc1.cols; // 256
    static thread_local std::vector<int32_t> h1(256);
    std::fill(h1.begin(), h1.end(), 0);

    for (int i = 0; i < count; ++i) {
        const int feat_idx = active_features[i];
        const int16_t* feat_weights = &fc1.weights[feat_idx * out_features];
        for (int j = 0; j < out_features; ++j) {
            h1[j] += feat_weights[j];
        }
    }

    // 2. Bias + ReLU
    for (int j = 0; j < out_features; ++j) {
        h1[j] = std::max(0, h1[j] + fc1.bias[j]);
    }

    // 3. Multi-Head Output
    auto run_head = [&](const Layer& head) {
        int64_t sum = head.bias[0];
        for (int j = 0; j < out_features; ++j) {
            sum += (int64_t)h1[j] * head.weights[j];
        }
        return (float)sum / (128.0f * 128.0f);
    };

    EvalResult res;
    res.eval = run_head(eval_head) * eval_std + eval_mean;
    res.tau  = sigmoid(run_head(tau_head));
    res.rho  = sigmoid(run_head(rho_head));
    res.rs   = sigmoid(run_head(rs_head));

    return res;
}

static Network global_net;

void GuidanceProvider::init() {
    if (global_net.load("nextfish.harenn")) {
        sync_cout << "info string HARENN: Full 4-Head Model loaded successfully" << sync_endl;
    } else {
        sync_cout << "info string HARENN: Failed to load model. Check nextfish.harenn path" << sync_endl;
    }
}

EvalResult GuidanceProvider::query(const Position& pos) {
    int active_features[64];
    int count = 0;
    
    // Convert board to indices (Sparse 768 representation)
    for (Square sq = SQ_A1; sq <= SQ_H8; ++sq) {
        Piece pc = pos.piece_on(sq);
        if (pc != NO_PIECE) {
            active_features[count++] = (int)sq * 12 + (int)pc - 1;
        }
    }
    
    return global_net.forward(active_features, count);
}

} // namespace HARENN

} // namespace Stockfish
