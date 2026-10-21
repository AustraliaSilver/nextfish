/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "harenn.h"
#include "position.h"
#include "bitboard.h"
#include "dee.h"
#include "numa.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <immintrin.h>
#include <functional>

namespace Stockfish {

namespace HARENN {

namespace {
    constexpr int SIGMOID_TABLE_SIZE = 1024;
    constexpr float SIGMOID_SCALE = 100.0f;
    constexpr float SIGMOID_OFFSET = SIGMOID_TABLE_SIZE / 2.0f;

    // Architectural limits of the shipped HARENN architecture. The loader
    // rejects any file that does not respect them, so a malformed or hostile
    // .harenn can never drive an out-of-bounds access.
    constexpr int MAX_HIDDEN1 = 512;    // fc1.cols
    constexpr int MAX_HIDDEN2 = 256;    // fc2.rows
    constexpr int REQUIRED_FC1_ROWS = 768;  // 64 squares * 12 piece codes

    static float sigmoid_table[SIGMOID_TABLE_SIZE];
    static std::atomic<bool> sigmoid_table_initialized{false};

    void init_sigmoid_table() {
        bool expected = false;
        if (!sigmoid_table_initialized.compare_exchange_strong(expected, true))
            return;
        for (int i = 0; i < SIGMOID_TABLE_SIZE; ++i) {
            float x = (i - SIGMOID_OFFSET) / SIGMOID_SCALE;
            sigmoid_table[i] = 1.0f / (1.0f + std::exp(-x));
        }
    }

    inline float fast_sigmoid(float x) {
        if (!std::isfinite(x)) return x > 0.0f ? 1.0f : 0.0f;
        int idx = static_cast<int>(x * SIGMOID_SCALE + SIGMOID_OFFSET);
        idx = std::clamp(idx, 0, SIGMOID_TABLE_SIZE - 1);
        return sigmoid_table[idx];
    }
}

bool Network::load(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f) return false;

    std::streamsize file_size = f.tellg();
    if (file_size < 12) return false; // Magic (4) + eval_mean (4) + eval_std (4)

    f.seekg(0, std::ios::beg);

    std::vector<char> buffer(file_size);
    if (!f.read(buffer.data(), file_size)) return false;
    f.close();

    const char* ptr = buffer.data();
    const char* end = ptr + file_size;

    // Parse magic
    if (std::string(ptr, 4) != "HNN4") return false;
    ptr += 4;

    // Parse mean and std
    std::memcpy(&eval_mean, ptr, 4); ptr += 4;
    std::memcpy(&eval_std, ptr, 4); ptr += 4;

    auto load_layer = [&](Layer& layer, bool bias_size_is_cols) {
        if (end - ptr < 8) return false;
        std::memcpy(&layer.rows, ptr, 4); ptr += 4;
        std::memcpy(&layer.cols, ptr, 4); ptr += 4;

        // 64-bit arithmetic: rows*cols can overflow a 32-bit int for a
        // malicious header, and the byte-count checks below would then pass
        // with a bogus length.
        const int64_t size = (int64_t)layer.rows * (int64_t)layer.cols;
        const int64_t bias_size = bias_size_is_cols ? layer.cols : layer.rows;

        if (layer.rows <= 0 || layer.cols <= 0) return false;
        if (size <= 0 || size > (int64_t) 1 << 30) return false;
        if (bias_size <= 0 || bias_size > (int64_t) 1 << 20) return false;
        if ((int64_t)(end - ptr) < size * 2 + bias_size * 4) return false;

        layer.weights.resize(size);
        std::memcpy(layer.weights.data(), ptr, size * 2); ptr += size * 2;

        layer.bias.resize(bias_size);
        std::memcpy(layer.bias.data(), ptr, bias_size * 4); ptr += bias_size * 4;
        return true;
    };

    if (!load_layer(fc1, true)) return false;
    if (!load_layer(fc2, false)) return false;
    if (!load_layer(eval_head, false)) return false;
    if (!load_layer(tau_head, false)) return false;
    if (!load_layer(rho_head, false)) return false;
    if (!load_layer(rs_head, false)) return false;

    // Architectural invariants. The inference code below indexes these
    // dimensions without further checks, so a file that violates them is
    // rejected here rather than being allowed to read out of bounds.
    if (fc1.rows < REQUIRED_FC1_ROWS) return false;
    if (fc1.cols <= 0 || fc1.cols > MAX_HIDDEN1) return false;
    if (fc2.rows <= 0 || fc2.rows > MAX_HIDDEN2) return false;
    if (fc2.cols != fc1.cols) return false;
    if (fc1.bias.size() != (size_t) fc1.cols) return false;
    if (fc2.bias.size() != (size_t) fc2.rows) return false;

    const Layer* heads[4] = {&eval_head, &tau_head, &rho_head, &rs_head};
    for (const Layer* h : heads) {
        if (h->rows != 1) return false;
        if (h->cols != fc2.rows) return false;
        if (h->weights.size() != (size_t) h->cols) return false;
        if (h->bias.size() != 1) return false;
    }

    return true;
}

namespace {
    // Shared hidden layer computation for lazy evaluation (2-layer architecture)
    void compute_hidden_layer(const int* active_features, int count,
                              const Layer& fc1, const Layer& fc2,
                              int32_t* h1, int32_t* h2) {
        const int out_features1 = fc1.cols;
        const int out_features2 = fc2.rows; // fc2 is (out_features2, out_features1)

        std::fill(h1, h1 + out_features1, 0);

        // First layer (sparse -> dense) - AVX2 optimized
        for (int i = 0; i < count; ++i) {
            const int feat_idx = active_features[i];
            if (feat_idx < 0 || feat_idx >= fc1.rows) continue;
            const int16_t* feat_weights = &fc1.weights[(size_t) feat_idx * out_features1];

            int j = 0;
#if defined(USE_AVX2)
            for (; j <= out_features1 - 8; j += 8) {
                __m128i w_vec_16 = _mm_loadu_si128((const __m128i*)(feat_weights + j));
                __m256i w_vec = _mm256_cvtepi16_epi32(w_vec_16);
                __m256i h1_vec = _mm256_loadu_si256((const __m256i*)(h1 + j));
                h1_vec = _mm256_add_epi32(h1_vec, w_vec);
                _mm256_storeu_si256((__m256i*)(h1 + j), h1_vec);
            }
#endif
            // Fallback for remainder
            for (; j < out_features1; ++j) {
                h1[j] += feat_weights[j];
            }
        }

        // Bias + ClippedReLU for first layer (clamped to [0, 128] which represents [0.0, 1.0] float)
        // AVX2 optimized
        {
            int j = 0;
#if defined(USE_AVX2)
            __m256i zero_vec = _mm256_setzero_si256();
            __m256i max_vec = _mm256_set1_epi32(128);
            for (; j <= out_features1 - 8; j += 8) {
                __m256i h1_vec = _mm256_loadu_si256((const __m256i*)(h1 + j));
                __m256i bias_vec = _mm256_loadu_si256((const __m256i*)(fc1.bias.data() + j));
                __m256i val_vec = _mm256_add_epi32(h1_vec, bias_vec);
                val_vec = _mm256_max_epi32(zero_vec, val_vec);
                val_vec = _mm256_min_epi32(max_vec, val_vec);
                _mm256_storeu_si256((__m256i*)(h1 + j), val_vec);
            }
#endif
            for (; j < out_features1; ++j) {
                h1[j] = std::clamp(h1[j] + fc1.bias[j], 0, 128);
            }
        }

        // Second layer (dense -> dense) - AVX2 optimized.
        // The accumulator is 64-bit: the theoretical maximum is
        // 512 * 128 * 32767 which overflows a 32-bit accumulator.
        std::fill(h2, h2 + out_features2, 0);
        const int16_t* w2 = fc2.weights.data();
        const int32_t* bias2_ptr = fc2.bias.data();

        for (int i = 0; i < out_features2; ++i) {
            int64_t sum = bias2_ptr[i];
            const int16_t* w2_row = &w2[(size_t) i * out_features1];

            int j = 0;
#if defined(USE_AVX2)
            for (; j <= out_features1 - 8; j += 8) {
                __m256i h1_vec = _mm256_loadu_si256((const __m256i*)(h1 + j));
                __m128i w2_vec_16 = _mm_loadu_si128((const __m128i*)(w2_row + j));
                __m256i w2_vec = _mm256_cvtepi16_epi32(w2_vec_16);
                __m256i prod = _mm256_mullo_epi32(h1_vec, w2_vec);
                int32_t prod_arr[8];
                _mm256_storeu_si256((__m256i*) prod_arr, prod);
                for (int k = 0; k < 8; ++k)
                    sum += (int64_t) prod_arr[k];
            }
#endif

            // Remainder loop
            for (; j < out_features1; ++j) {
                sum += (int64_t) h1[j] * w2_row[j];
            }

            h2[i] = (int32_t) std::clamp(sum, (int64_t) 0, (int64_t) 16384);
        }
    }

    float run_head_single(const int32_t* h2, int out_features, const Layer& head) {
        int64_t sum = head.bias[0];
        const int16_t* w = head.weights.data();
        const int32_t* h2_ptr = h2;

        int j = 0;
#if defined(USE_AVX2)
        for (; j <= out_features - 8; j += 8) {
            __m256i h2_vec = _mm256_loadu_si256((const __m256i*)(h2_ptr + j));
            __m128i w_vec_16 = _mm_loadu_si128((const __m128i*)(w + j));
            __m256i w_vec = _mm256_cvtepi16_epi32(w_vec_16);
            __m256i prod = _mm256_mullo_epi32(h2_vec, w_vec);
            int32_t prod_arr[8];
            _mm256_storeu_si256((__m256i*) prod_arr, prod);
            for (int k = 0; k < 8; ++k)
                sum += (int64_t) prod_arr[k];
        }
#endif

        for (; j < out_features; ++j) {
            sum += (int64_t) h2_ptr[j] * w[j];
        }

        return (float) sum / (128.0f * 128.0f * 128.0f);
    }
}

EvalResult Network::forward(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    if (out_features1 <= 0 || out_features1 > MAX_HIDDEN1 || out_features2 <= 0
        || out_features2 > MAX_HIDDEN2)
        return EvalResult{0.0f, 0.0f, 0.0f, 0.0f};
    int32_t h1[MAX_HIDDEN1];
    int32_t h2[MAX_HIDDEN2];

    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);

    EvalResult res;
    res.eval = run_head_single(h2, out_features2, eval_head) * eval_std + eval_mean;
    res.tau  = fast_sigmoid(run_head_single(h2, out_features2, tau_head));
    res.rho  = fast_sigmoid(run_head_single(h2, out_features2, rho_head));
    res.rs   = fast_sigmoid(run_head_single(h2, out_features2, rs_head));

    return res;
}

float Network::compute_eval(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    if (out_features1 <= 0 || out_features1 > MAX_HIDDEN1 || out_features2 <= 0
        || out_features2 > MAX_HIDDEN2)
        return 0.0f;
    int32_t h1[MAX_HIDDEN1];
    int32_t h2[MAX_HIDDEN2];
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return run_head_single(h2, out_features2, eval_head) * eval_std + eval_mean;
}

float Network::compute_tau(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    if (out_features1 <= 0 || out_features1 > MAX_HIDDEN1 || out_features2 <= 0
        || out_features2 > MAX_HIDDEN2)
        return 0.5f;
    int32_t h1[MAX_HIDDEN1];
    int32_t h2[MAX_HIDDEN2];
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, out_features2, tau_head));
}

float Network::compute_rho(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    if (out_features1 <= 0 || out_features1 > MAX_HIDDEN1 || out_features2 <= 0
        || out_features2 > MAX_HIDDEN2)
        return 0.5f;
    int32_t h1[MAX_HIDDEN1];
    int32_t h2[MAX_HIDDEN2];
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, out_features2, rho_head));
}

float Network::compute_rs(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    if (out_features1 <= 0 || out_features1 > MAX_HIDDEN1 || out_features2 <= 0
        || out_features2 > MAX_HIDDEN2)
        return 0.5f;
    int32_t h1[MAX_HIDDEN1];
    int32_t h2[MAX_HIDDEN2];
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    return fast_sigmoid(run_head_single(h2, out_features2, rs_head));
}

std::pair<float, float> Network::compute_rho_and_rs(const int* active_features, int count) const {
    const int out_features1 = fc1.cols;
    const int out_features2 = fc2.rows;
    if (out_features1 <= 0 || out_features1 > MAX_HIDDEN1 || out_features2 <= 0
        || out_features2 > MAX_HIDDEN2) {
        return {0.5f, 0.5f};
    }
    int32_t h1[MAX_HIDDEN1];
    int32_t h2[MAX_HIDDEN2];
    compute_hidden_layer(active_features, count, fc1, fc2, h1, h2);
    float rho = fast_sigmoid(run_head_single(h2, out_features2, rho_head));
    float rs  = fast_sigmoid(run_head_single(h2, out_features2, rs_head));
    return {rho, rs};
}

static Network* global_net = nullptr;
static std::atomic<bool> model_loaded{false};

void GuidanceProvider::init(const std::string& binaryDirectory) {
    init_sigmoid_table();

    if (!global_net) {
        global_net = new Network();
    }

    // Resolve the model next to the executable first, then fall back to the
    // working directory so the engine also works when started from a folder
    // that merely contains the model.
    std::string path = binaryDirectory + "nextfish.harenn";
    bool loaded = global_net->load(path);
    if (!loaded) {
        loaded = global_net->load("nextfish.harenn");
        if (loaded)
            sync_cout << "info string HARENN: model loaded from the working directory" << sync_endl;
    }

    if (loaded) {
        model_loaded = true;
        sync_cout << "info string HARENN: Full 4-Head Model loaded successfully" << sync_endl;
    } else {
        model_loaded = false;
        sync_cout << "info string HARENN: Failed to load model 'nextfish.harenn'. Place it "
                     "next to the executable or in the working directory." << sync_endl;
    }
}

bool GuidanceProvider::is_model_loaded() {
    return model_loaded.load(std::memory_order_acquire);
}

namespace {
    // Piece-square feature encoding. It must stay bit-identical to
    // train_harenn.py's PIECE_MAP, otherwise the network sees different
    // features at inference time than it was trained on:
    //   white pawn/knight/bishop/rook/queen/king -> 0..5
    //   black pawn/knight/bishop/rook/queen/king -> 6..11
    // With W_PAWN=1..W_KING=6 and B_PAWN=9..B_KING=14 this is pc-1 for
    // white and pc-3 for black. The 12 codes per square are packed as
    // py_sq * 12 + py_pc, so py_pc must stay below 12.
    inline int encode_piece(Piece pc) {
        return (pc <= W_KING) ? (int) pc - 1 : (int) pc - 3;
    }

    // Extract the active features of a position. Returns the number of
    // features written to 'out'.
    int extract_features(const Position& pos, int* out) {
        int count = 0;
        Bitboard pieces = pos.pieces();
        while (pieces) {
            Square sq = pop_lsb(pieces);
            Piece pc = pos.piece_on(sq);
            if (pc != NO_PIECE) {
                int py_sq = (int) sq ^ 56;  // Convert bottom-up to top-down (a1->a8 rank flip)
                int py_pc = encode_piece(pc);
                out[count++] = py_sq * 12 + py_pc;
            }
        }
        return count;
    }
}

EvalResult GuidanceProvider::query(const Position& pos, NumaReplicatedAccessToken numaToken) {
    (void) numaToken;
    if (!is_model_loaded() || !global_net) {
        return EvalResult{0.0f, 0.0f, 0.0f, 0.0f};
    }
    int active_features[64];
    int count = extract_features(pos, active_features);

    return global_net->forward(active_features, count);
}

std::pair<float, float> GuidanceProvider::query_rho_and_rs(const Position& pos, NumaReplicatedAccessToken numaToken) {
    (void) numaToken;
    if (!is_model_loaded() || !global_net) {
        return {0.5f, 0.5f};
    }
    int active_features[64];
    int count = extract_features(pos, active_features);

    return global_net->compute_rho_and_rs(active_features, count);
}

} // namespace HARENN

} // namespace Stockfish
