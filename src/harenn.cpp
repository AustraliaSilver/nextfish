#include "harenn.h"
#include "position.h"
#include "bitboard.h"
#include "dee.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cmath>
#include <vector>
#include <immintrin.h>
#include <functional>

namespace Stockfish {

namespace HARENN {

namespace {
    constexpr int SIGMOID_TABLE_SIZE = 1024;
    constexpr float SIGMOID_SCALE = 100.0f;
    constexpr float SIGMOID_OFFSET = SIGMOID_TABLE_SIZE / 2.0f;
    
    static float sigmoid_table[SIGMOID_TABLE_SIZE];
    static bool sigmoid_table_initialized = false;
    
    void init_sigmoid_table() {
        if (sigmoid_table_initialized) return;
        for (int i = 0; i < SIGMOID_TABLE_SIZE; ++i) {
            float x = (i - SIGMOID_OFFSET) / SIGMOID_SCALE;
            sigmoid_table[i] = 1.0f / (1.0f + std::exp(-x));
        }
        sigmoid_table_initialized = true;
    }
    
    inline float fast_sigmoid(float x) {
        int idx = static_cast<int>(x * SIGMOID_SCALE + SIGMOID_OFFSET);
        idx = std::clamp(idx, 0, SIGMOID_TABLE_SIZE - 1);
        return sigmoid_table[idx];
    }
}

bool Network::load(const std::string& filename) {
    // Read entire file into buffer for faster I/O
    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f) return false;

    std::streamsize file_size = f.tellg();
    f.seekg(0, std::ios::beg);
    
    std::vector<char> buffer(file_size);
    if (!f.read(buffer.data(), file_size)) return false;
    f.close();
    
    const char* ptr = buffer.data();
    
    // Parse magic
    if (std::string(ptr, 4) != "HNN4") return false;
    ptr += 4;
    
    // Parse mean and std
    std::memcpy(&eval_mean, ptr, 4); ptr += 4;
    std::memcpy(&eval_std, ptr, 4); ptr += 4;
    
    auto load_layer = [&](Layer& layer) {
        std::memcpy(&layer.rows, ptr, 4); ptr += 4;
        std::memcpy(&layer.cols, ptr, 4); ptr += 4;
        int size = layer.rows * layer.cols;
        layer.weights.reserve(size);
        layer.weights.resize(size);
        std::memcpy(layer.weights.data(), ptr, size * 2); ptr += size * 2;
        layer.bias.reserve(layer.cols);
        layer.bias.resize(layer.cols);
        std::memcpy(layer.bias.data(), ptr, layer.cols * 4); ptr += layer.cols * 4;
    };
    
    load_layer(fc1);
    load_layer(fc2);
    load_layer(eval_head);
    load_layer(tau_head);
    load_layer(rho_head);
    load_layer(rs_head);
    
    return true;
}

namespace {
    // Shared hidden layer computation for lazy evaluation (2-layer architecture)
    void compute_hidden_layer(const int* active_features, int count, 
                              const Layer& fc1, const Layer& fc2,
                              std::vector<int32_t>& h1, std::vector<int32_t>& h2) {
        const int out_features1 = fc1.cols;
        const int out_features2 = fc2.cols;
        std::fill(h1.begin(), h1.end(), 0);

        // First layer (sparse → dense) with AVX2 SIMD
        for (int i = 0; i < count; ++i) {
            const int feat_idx = active_features[i];
            const int16_t* feat_weights = &fc1.weights[feat_idx * out_features1];
            int32_t* h1_ptr = h1.data();
            
            // AVX2 SIMD: Process 8 int16 weights at a time, convert to int32, add to h1
            int j = 0;
            for (; j <= out_features1 - 8; j += 8) {
                __m128i w16 = _mm_loadu_si128((__m128i*)&feat_weights[j]);
                __m256i w32 = _mm256_cvtepi16_epi32(w16);
                __m256i h1_vec = _mm256_loadu_si256((__m256i*)&h1_ptr[j]);
                h1_vec = _mm256_add_epi32(h1_vec, w32);
                _mm256_storeu_si256((__m256i*)&h1_ptr[j], h1_vec);
            }
            // Handle remaining elements
            for (; j < out_features1; ++j) {
                h1_ptr[j] += feat_weights[j];
            }
        }

        // Bias + ReLU for first layer with AVX2 SIMD
        int32_t* h1_ptr = h1.data();
        const int32_t* bias1_ptr = fc1.bias.data();
        int j = 0;
        for (; j <= out_features1 - 8; j += 8) {
            __m256i h1_vec = _mm256_loadu_si256((__m256i*)&h1_ptr[j]);
            __m256i bias_vec = _mm256_loadu_si256((__m256i*)&bias1_ptr[j]);
            h1_vec = _mm256_add_epi32(h1_vec, bias_vec);
            __m256i zero = _mm256_setzero_si256();
            h1_vec = _mm256_max_epi32(h1_vec, zero);
            _mm256_storeu_si256((__m256i*)&h1_ptr[j], h1_vec);
        }
        for (; j < out_features1; ++j) {
            h1_ptr[j] = std::max(0, h1_ptr[j] + bias1_ptr[j]);
        }

        // Second layer (dense → dense) with AVX2 SIMD
        std::fill(h2.begin(), h2.end(), 0);
        const int16_t* w2 = fc2.weights.data();
        const int32_t* bias2_ptr = fc2.bias.data();
        
        for (int i = 0; i < out_features2; ++i) {
            int64_t sum = bias2_ptr[i];
            const int16_t* w2_col = &w2[i];
            
            // AVX2 SIMD: Process 8 elements at a time using madd_epi16
            int j = 0;
            for (; j <= out_features1 - 8; j += 8) {
                __m256i h1_vec = _mm256_loadu_si256((__m256i*)&h1_ptr[j]);
                __m256i w2_vec = _mm256_loadu_si256((__m256i*)&w2_col[j * out_features2]);
                __m256i prod = _mm256_madd_epi16(h1_vec, w2_vec);
                // madd_epi16 produces 4 int32 results in lanes
                // Extract and sum them properly
                __m128i prod_lo = _mm256_castsi256_si128(prod);
                __m128i prod_hi = _mm256_extracti128_si256(prod, 1);
                __m128i sum128 = _mm_add_epi32(prod_lo, prod_hi);
                // Horizontal sum of 4 int32 values
                __m128i sum_hi = _mm_unpackhi_epi64(sum128, sum128);
                sum128 = _mm_add_epi32(sum128, sum_hi);
                __m128i sum_64 = _mm_shuffle_epi32(sum128, _MM_SHUFFLE(1, 0, 1, 0));
                sum128 = _mm_add_epi32(sum128, sum_64);
                sum += _mm_cvtsi128_si32(sum128);
            }
            
            // Handle remaining elements
            for (; j < out_features1; ++j) {
                sum += (int64_t)h1_ptr[j] * w2_col[j * out_features2];
            }
            h2[i] = (int32_t)sum;
        }

        // Bias + ReLU for second layer with AVX2 SIMD
        int32_t* h2_ptr = h2.data();
        int k = 0;
        for (; k <= out_features2 - 8; k += 8) {
            __m256i h2_vec = _mm256_loadu_si256((__m256i*)&h2_ptr[k]);
            __m256i zero = _mm256_setzero_si256();
            h2_vec = _mm256_max_epi32(h2_vec, zero);
            _mm256_storeu_si256((__m256i*)&h2_ptr[k], h2_vec);
        }
        for (; k < out_features2; ++k) {
            h2_ptr[k] = std::max(0, h2_ptr[k]);
        }
    }
    
    float run_head_single(const std::vector<int32_t>& h2, const Layer& head) {
        const int out_features = h2.size();
        int64_t sum = head.bias[0];
        const int16_t* w = head.weights.data();
        const int32_t* h2_ptr = h2.data();
        
        // AVX2 SIMD: Process 8 elements at a time using madd_epi16
        int j = 0;
        for (; j <= out_features - 8; j += 8) {
            __m256i h2_vec = _mm256_loadu_si256((__m256i*)&h2_ptr[j]);
            __m256i w_vec = _mm256_loadu_si256((__m256i*)&w[j]);
            __m256i prod = _mm256_madd_epi16(h2_vec, w_vec);
            __m128i prod_lo = _mm256_castsi256_si128(prod);
            __m128i prod_hi = _mm256_extracti128_si256(prod, 1);
            __m128i sum128 = _mm_add_epi32(prod_lo, prod_hi);
            __m128i sum_hi = _mm_unpackhi_epi64(sum128, sum128);
            sum128 = _mm_add_epi32(sum128, sum_hi);
            __m128i sum_64 = _mm_shuffle_epi32(sum128, _MM_SHUFFLE(1, 0, 1, 0));
            sum128 = _mm_add_epi32(sum128, sum_64);
            sum += _mm_cvtsi128_si32(sum128);
        }
        
        // Handle remaining elements
        for (; j < out_features; ++j) {
            sum += (int64_t)h2_ptr[j] * w[j];
        }
        
        return (float)sum / (128.0f * 128.0f);
    }
}

EvalResult Network::forward(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.cols;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);

    EvalResult res;
    res.eval = run_head_single(h2, eval_head) * eval_std + eval_mean;
    res.tau  = fast_sigmoid(run_head_single(h2, tau_head));
    res.rho  = fast_sigmoid(run_head_single(h2, rho_head));
    res.rs   = fast_sigmoid(run_head_single(h2, rs_head));

    return res;
}

float Network::compute_eval(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.cols;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return run_head_single(h2, eval_head) * eval_std + eval_mean;
}

float Network::compute_tau(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.cols;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, tau_head));
}

float Network::compute_rho(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.cols;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, rho_head));
}

float Network::compute_rs(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.cols;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, rs_head));
}

static Network global_net;

void GuidanceProvider::init() {
    init_sigmoid_table();
    if (global_net.load("nextfish.harenn")) {
        sync_cout << "info string HARENN: Full 4-Head Model loaded successfully" << sync_endl;
    } else {
        sync_cout << "info string HARENN: Failed to load model. Check nextfish.harenn path" << sync_endl;
    }
}

EvalResult GuidanceProvider::query(const Position& pos) {
    int active_features[64];
    int count = 0;
    
    // Convert board to indices (Sparse 768 representation) - optimized with bitboard
    // Flip board for Black to move (Stockfish NNUE style)
    // This ensures the model always evaluates from the perspective of the side to move
    Bitboard pieces = pos.pieces();
    while (pieces) {
        Square sq = pop_lsb(pieces);
        Piece pc = pos.piece_on(sq);
        if (pc != NO_PIECE) {
            Square actual_sq = sq;
            if (pos.side_to_move() == BLACK) {
                // Mirror horizontally: file becomes 7 - file
                actual_sq = make_square(file_of(sq), rank_of(sq));
                actual_sq = Square((int(actual_sq) / 8) * 8 + (7 - (int(actual_sq) % 8)));
            }
            active_features[count++] = (int)actual_sq * 12 + (int)pc - 1;
        }
    }
    
    return global_net.forward(active_features, count);
}

} // namespace HARENN

} // namespace Stockfish
