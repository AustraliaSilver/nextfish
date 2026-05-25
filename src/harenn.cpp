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
    
    auto load_layer = [&](Layer& layer, bool bias_size_is_cols) {
        std::memcpy(&layer.rows, ptr, 4); ptr += 4;
        std::memcpy(&layer.cols, ptr, 4); ptr += 4;
        int size = layer.rows * layer.cols;
        layer.weights.reserve(size);
        layer.weights.resize(size);
        std::memcpy(layer.weights.data(), ptr, size * 2); ptr += size * 2;
        
        int bias_size = bias_size_is_cols ? layer.cols : layer.rows;
        layer.bias.reserve(bias_size);
        layer.bias.resize(bias_size);
        std::memcpy(layer.bias.data(), ptr, bias_size * 4); ptr += bias_size * 4;
    };
    
    load_layer(fc1, true);
    load_layer(fc2, false);
    load_layer(eval_head, false);
    load_layer(tau_head, false);
    load_layer(rho_head, false);
    load_layer(rs_head, false);
    
    return true;
}

namespace {
    // Shared hidden layer computation for lazy evaluation (2-layer architecture)
    void compute_hidden_layer(const int* active_features, int count, 
                              const Layer& fc1, const Layer& fc2,
                              std::vector<int32_t>& h1, std::vector<int32_t>& h2) {
        const int out_features1 = fc1.cols;
        const int out_features2 = fc2.rows; // fc2 is (out_features2, out_features1)
        
        std::fill(h1.begin(), h1.end(), 0);

        // First layer (sparse → dense) - AVX2 optimized
        for (int i = 0; i < count; ++i) {
            const int feat_idx = active_features[i];
            const int16_t* feat_weights = &fc1.weights[feat_idx * out_features1];
            
            int j = 0;
            for (; j <= out_features1 - 8; j += 8) {
                __m128i w_vec_16 = _mm_loadu_si128((const __m128i*)(feat_weights + j));
                __m256i w_vec = _mm256_cvtepi16_epi32(w_vec_16);
                __m256i h1_vec = _mm256_loadu_si256((const __m256i*)(h1.data() + j));
                h1_vec = _mm256_add_epi32(h1_vec, w_vec);
                _mm256_storeu_si256((__m256i*)(h1.data() + j), h1_vec);
            }
            // Fallback for remainder
            for (; j < out_features1; ++j) {
                h1[j] += feat_weights[j];
            }
        }

        // Bias + ClippedReLU for first layer (clamped to [0, 128] which represents [0.0, 1.0] float)
        // AVX2 optimized
        {
            __m256i zero_vec = _mm256_setzero_si256();
            __m256i max_vec = _mm256_set1_epi32(128);
            int j = 0;
            for (; j <= out_features1 - 8; j += 8) {
                __m256i h1_vec = _mm256_loadu_si256((const __m256i*)(h1.data() + j));
                __m256i bias_vec = _mm256_loadu_si256((const __m256i*)(fc1.bias.data() + j));
                __m256i val_vec = _mm256_add_epi32(h1_vec, bias_vec);
                val_vec = _mm256_max_epi32(zero_vec, val_vec);
                val_vec = _mm256_min_epi32(max_vec, val_vec);
                _mm256_storeu_si256((__m256i*)(h1.data() + j), val_vec);
            }
            for (; j < out_features1; ++j) {
                h1[j] = std::clamp(h1[j] + fc1.bias[j], 0, 128);
            }
        }

        // Second layer (dense → dense) - AVX2 optimized
        std::fill(h2.begin(), h2.end(), 0);
        const int16_t* w2 = fc2.weights.data();
        const int32_t* bias2_ptr = fc2.bias.data();
        
        for (int i = 0; i < out_features2; ++i) {
            int64_t sum = bias2_ptr[i];
            const int16_t* w2_row = &w2[i * out_features1];
            
            __m256i acc_vec = _mm256_setzero_si256();
            
            int j = 0;
            for (; j <= out_features1 - 8; j += 8) {
                __m256i h1_vec = _mm256_loadu_si256((const __m256i*)(h1.data() + j));
                __m128i w2_vec_16 = _mm_loadu_si128((const __m128i*)(w2_row + j));
                __m256i w2_vec = _mm256_cvtepi16_epi32(w2_vec_16);
                acc_vec = _mm256_add_epi32(acc_vec, _mm256_mullo_epi32(h1_vec, w2_vec));
            }
            
            alignas(32) int32_t acc_arr[8];
            _mm256_store_si256((__m256i*)acc_arr, acc_vec);
            int64_t horizontal_sum = acc_arr[0] + acc_arr[1] + acc_arr[2] + acc_arr[3] +
                                     acc_arr[4] + acc_arr[5] + acc_arr[6] + acc_arr[7];
            sum += horizontal_sum;
            
            // Remainder loop
            for (; j < out_features1; ++j) {
                sum += (int64_t)h1[j] * w2_row[j];
            }
            
            h2[i] = (int32_t)std::clamp(sum, 0LL, 16384LL);
        }
    }
    
    float run_head_single(const std::vector<int32_t>& h2, const Layer& head) {
        const int out_features = h2.size();
        int64_t sum = head.bias[0];
        const int16_t* w = head.weights.data();
        const int32_t* h2_ptr = h2.data();
        
        __m256i acc_vec = _mm256_setzero_si256();
        
        int j = 0;
        for (; j <= out_features - 8; j += 8) {
            __m256i h2_vec = _mm256_loadu_si256((const __m256i*)(h2_ptr + j));
            __m128i w_vec_16 = _mm_loadu_si128((const __m128i*)(w + j));
            __m256i w_vec = _mm256_cvtepi16_epi32(w_vec_16);
            acc_vec = _mm256_add_epi32(acc_vec, _mm256_mullo_epi32(h2_vec, w_vec));
        }
        
        alignas(32) int32_t acc_arr[8];
        _mm256_store_si256((__m256i*)acc_arr, acc_vec);
        int64_t horizontal_sum = acc_arr[0] + acc_arr[1] + acc_arr[2] + acc_arr[3] +
                                 acc_arr[4] + acc_arr[5] + acc_arr[6] + acc_arr[7];
        sum += horizontal_sum;
        
        for (; j < out_features; ++j) {
            sum += (int64_t)h2_ptr[j] * w[j];
        }
        
        return (float)sum / (128.0f * 128.0f * 128.0f);
    }
}

EvalResult Network::forward(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
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
    const int out_features2 = fc2.rows;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return run_head_single(h2, eval_head) * eval_std + eval_mean;
}

float Network::compute_tau(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, tau_head));
}

float Network::compute_rho(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, rho_head));
}

float Network::compute_rs(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    static thread_local std::vector<int32_t> h1;
    static thread_local std::vector<int32_t> h2;
    if (h1.size() != out_features1) h1.resize(out_features1);
    if (h2.size() != out_features2) h2.resize(out_features2);
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, rs_head));
}

static Network global_net;
static bool model_loaded = false;

void GuidanceProvider::init() {
    init_sigmoid_table();
    if (global_net.load("nextfish.harenn")) {
        model_loaded = true;
        sync_cout << "info string HARENN: Full 4-Head Model loaded successfully" << sync_endl;
    } else {
        model_loaded = false;
        sync_cout << "info string HARENN: Failed to load model. Check nextfish.harenn path" << sync_endl;
    }
}

bool GuidanceProvider::is_model_loaded() {
    return model_loaded;
}

EvalResult GuidanceProvider::query(const Position& pos) {
    if (!model_loaded) {
        return EvalResult{0.0f, 0.0f, 0.0f, 0.0f};
    }
    int active_features[64];
    int count = 0;
    
    bool is_black = (pos.side_to_move() == BLACK);
    Bitboard pieces = pos.pieces();
    while (pieces) {
        Square sq = pop_lsb(pieces);
        Piece pc = pos.piece_on(sq);
        if (pc != NO_PIECE) {
            int py_sq;
            int py_pc;
            if (!is_black) {
                py_sq = (int)sq ^ 56;
                py_pc = (pc <= W_KING) ? ((int)pc - 1) : ((int)pc - 3);
            } else {
                py_sq = (int)sq;
                py_pc = (pc <= W_KING) ? ((int)pc + 5) : ((int)pc - 9);
            }
            active_features[count++] = py_sq * 12 + py_pc;
        }
    }
    
    return global_net.forward(active_features, count);
}

} // namespace HARENN

} // namespace Stockfish
