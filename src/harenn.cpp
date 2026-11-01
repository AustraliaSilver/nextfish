#include "harenn.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <immintrin.h>
#include <cstdlib>

namespace NextFish {

HARENNEvaluator GlobalHARENN;

static constexpr int FC2_QSCALE = 1;

static void* aligned_alloc_64(size_t size) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    return _aligned_malloc(size, 64);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, size) != 0) return nullptr;
    return ptr;
#endif
}

static void aligned_free_64(void* ptr) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

HARENNEvaluator::HARENNEvaluator() {
    eval_cache = new EvalCacheEntry[EVAL_CACHE_SIZE]();
    const char* paths[] = {
        "Neuron Network/nextfish_hkb.harenn",
        "Neuron Network/nextfish.harenn",
        "Neuron Network/nextfish_halfka.harenn",
        "../Neuron Network/nextfish_hkb.harenn",
        "../Neuron Network/nextfish.harenn",
        "../Neuron Network/nextfish_halfka.harenn",
        "Neuron Network/nexfish.harenn",
        "C:\\Users\\hunga\\OneDrive\\Desktop\\NEXUS-CHESS\\Neuron Network\\nextfish_hkb.harenn",
        "C:\\Users\\hunga\\OneDrive\\Desktop\\NEXUS-CHESS\\Neuron Network\\nextfish.harenn",
        "C:\\Users\\hunga\\OneDrive\\Desktop\\NEXUS-CHESS\\Neuron Network\\nextfish_halfka.harenn"
    };
    for (const char* p : paths) {
        if (load_weights(p)) {
            std::cerr << "Loaded HARENN weights successfully from: " << p << "\n";
            return;
        }
    }
    std::cerr << "WARNING: Could not load HARENN weights from any known location. Using classical evaluation fallback!\n";
}

HARENNEvaluator::~HARENNEvaluator() {
    delete[] eval_cache;
    if (fc1_weights) aligned_free_64(fc1_weights);
    if (fc1_biases) aligned_free_64(fc1_biases);
    if (fc2_weights) aligned_free_64(fc2_weights);
    if (fc2_biases) aligned_free_64(fc2_biases);
    if (fc2_transposed) aligned_free_64(fc2_transposed);
    if (fc2_int8) aligned_free_64(fc2_int8);
    if (eval_weights) aligned_free_64(eval_weights);
    if (tau_weights) aligned_free_64(tau_weights);
    if (rho_weights) aligned_free_64(rho_weights);
    if (rs_weights) aligned_free_64(rs_weights);
}

void HARENNEvaluator::clear_cache() {
    if (eval_cache) {
        std::memset(eval_cache, 0, EVAL_CACHE_SIZE * sizeof(EvalCacheEntry));
    }
}

bool HARENNEvaluator::load_weights(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    f.seekg(0, std::ios::end);
    size_t file_sz = f.tellg();
    f.seekg(0, std::ios::beg);
    if (file_sz < 1500000) return false;

    char magic[4];
    f.read(magic, 4);
    if (std::memcmp(magic, "HKB4", 4) == 0) {
        hkb_mode = true;
        halfka_mode = true;
    } else if (std::memcmp(magic, "HKA4", 4) == 0) {
        hkb_mode = false;
        halfka_mode = true;
    } else if (std::memcmp(magic, "HNN4", 4) == 0) {
        hkb_mode = false;
        halfka_mode = false;
    } else {
        return false;
    }

    f.read(reinterpret_cast<char*>(&eval_mean), sizeof(float));
    f.read(reinterpret_cast<char*>(&eval_std), sizeof(float));

    int r1 = 0, c1 = 0;
    f.read(reinterpret_cast<char*>(&r1), 4);
    f.read(reinterpret_cast<char*>(&c1), 4);
    if (hkb_mode) {
        if (r1 != 5632 || c1 != 512) return false;
    } else if (halfka_mode) {
        if (r1 != 45056 || c1 != 512) return false;
    } else {
        if (r1 != 768 || c1 != 1024) return false;
    }
    hidden1_dim = c1;

    if (fc1_weights) { aligned_free_64(fc1_weights); fc1_weights = nullptr; }
    if (fc1_biases) { aligned_free_64(fc1_biases); fc1_biases = nullptr; }

    fc1_weights = static_cast<int16_t*>(aligned_alloc_64(static_cast<size_t>(r1) * c1 * sizeof(int16_t)));
    fc1_biases = static_cast<int32_t*>(aligned_alloc_64(c1 * sizeof(int32_t)));

    f.read(reinterpret_cast<char*>(fc1_weights), static_cast<size_t>(r1) * c1 * sizeof(int16_t));
    f.read(reinterpret_cast<char*>(fc1_biases), c1 * sizeof(int32_t));

    int r2 = 0, c2 = 0;
    f.read(reinterpret_cast<char*>(&r2), 4);
    f.read(reinterpret_cast<char*>(&c2), 4);
    if (c2 != 1024) return false;
    if (r2 != 32 && r2 != 64 && r2 != 128 && r2 != 256 && r2 != 512) return false;
    hidden2_dim = r2;

    if (fc2_weights) { aligned_free_64(fc2_weights); fc2_weights = nullptr; }
    if (fc2_biases) { aligned_free_64(fc2_biases); fc2_biases = nullptr; }
    if (fc2_transposed) { aligned_free_64(fc2_transposed); fc2_transposed = nullptr; }
    if (fc2_int8) { aligned_free_64(fc2_int8); fc2_int8 = nullptr; }

    fc2_weights = static_cast<int16_t*>(aligned_alloc_64(hidden2_dim * 1024 * sizeof(int16_t)));
    fc2_biases = static_cast<int32_t*>(aligned_alloc_64(hidden2_dim * sizeof(int32_t)));

    f.read(reinterpret_cast<char*>(fc2_weights), hidden2_dim * 1024 * sizeof(int16_t));
    f.read(reinterpret_cast<char*>(fc2_biases), hidden2_dim * sizeof(int32_t));

    fc2_transposed = static_cast<int16_t*>(aligned_alloc_64(1024 * hidden2_dim * sizeof(int16_t)));
    for (int r = 0; r < hidden2_dim; ++r) {
        for (int c = 0; c < 1024; ++c) {
            fc2_transposed[c * hidden2_dim + r] = fc2_weights[r * 1024 + c];
        }
    }

    fc2_int8 = static_cast<int8_t*>(aligned_alloc_64(hidden2_dim * 1024 * sizeof(int8_t)));
    for (size_t i = 0; i < static_cast<size_t>(hidden2_dim) * 1024ull; ++i) {
        int q = (static_cast<int>(fc2_weights[i]) + (fc2_weights[i] >= 0 ? FC2_QSCALE / 2 : -(FC2_QSCALE / 2))) / FC2_QSCALE;
        fc2_int8[i] = static_cast<int8_t>(std::clamp(q, -127, 127));
    }

    auto load_head = [&](int16_t*& w, int32_t& b) -> bool {
        int rh = 0, ch = 0;
        f.read(reinterpret_cast<char*>(&rh), 4);
        f.read(reinterpret_cast<char*>(&ch), 4);
        if (rh != 1 || ch != hidden2_dim) return false;
        if (w) { aligned_free_64(w); w = nullptr; }
        w = static_cast<int16_t*>(aligned_alloc_64(hidden2_dim * sizeof(int16_t)));
        f.read(reinterpret_cast<char*>(w), hidden2_dim * sizeof(int16_t));
        f.read(reinterpret_cast<char*>(&b), sizeof(int32_t));
        return true;
    };

    if (!load_head(eval_weights, eval_bias)) return false;
    if (!load_head(tau_weights, tau_bias)) return false;
    if (!load_head(rho_weights, rho_bias)) return false;
    if (!load_head(rs_weights, rs_bias)) return false;

    if (!f.good()) return false;

    loaded = true;
    return true;
}

__attribute__((always_inline)) static inline int32_t hsum256_ps_epi32(const __m256i& v) {
    __m128i vlow = _mm256_castsi256_si128(v);
    __m128i vhigh = _mm256_extracti128_si256(v, 1);
    __m128i sum128 = _mm_add_epi32(vlow, vhigh);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    return _mm_cvtsi128_si32(sum128);
}

void HARENNEvaluator::add_feature(int32_t* acc, int feat) const {
    if (feat < 0 || feat >= 768) return;
    const int16_t* w_row = &fc1_weights[feat * 1024];
    for (int i = 0; i < 1024; i += 16) {
        __m128i w16_0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&w_row[i]));
        __m128i w16_1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&w_row[i + 8]));
        __m256i w32_0 = _mm256_cvtepi16_epi32(w16_0);
        __m256i w32_1 = _mm256_cvtepi16_epi32(w16_1);
        __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i]));
        __m256i a1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 8]));
        a0 = _mm256_add_epi32(a0, w32_0);
        a1 = _mm256_add_epi32(a1, w32_1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&acc[i]), a0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&acc[i + 8]), a1);
    }
}

void HARENNEvaluator::sub_feature(int32_t* acc, int feat) const {
    if (feat < 0 || feat >= 768) return;
    const int16_t* w_row = &fc1_weights[feat * 1024];
    for (int i = 0; i < 1024; i += 16) {
        __m128i w16_0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&w_row[i]));
        __m128i w16_1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&w_row[i + 8]));
        __m256i w32_0 = _mm256_cvtepi16_epi32(w16_0);
        __m256i w32_1 = _mm256_cvtepi16_epi32(w16_1);
        __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i]));
        __m256i a1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 8]));
        a0 = _mm256_sub_epi32(a0, w32_0);
        a1 = _mm256_sub_epi32(a1, w32_1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&acc[i]), a0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&acc[i + 8]), a1);
    }
}

void HARENNEvaluator::compute_accumulator_halfka(const Position& pos, int32_t* acc_w, int32_t* acc_b) const {
    if (!loaded) {
        std::memset(acc_w, 0, 512 * sizeof(int32_t));
        std::memset(acc_b, 0, 512 * sizeof(int32_t));
        return;
    }
    std::memcpy(acc_w, fc1_biases, 512 * sizeof(int32_t));
    std::memcpy(acc_b, fc1_biases, 512 * sizeof(int32_t));

    Square w_ksq = pos.king_square(WHITE);
    Square b_ksq = pos.king_square(BLACK);

    for (int sq = 0; sq < 64; ++sq) {
        Piece pc = pos.piece_on(static_cast<Square>(sq));
        if (pc != NO_PIECE) {
            if (pc != W_KING) {
                int feat_w = make_feature(w_ksq, pc, static_cast<Square>(sq), WHITE, hkb_mode);
                add_feature_halfka(acc_w, feat_w);
            }
            if (pc != B_KING) {
                int feat_b = make_feature(b_ksq, pc, static_cast<Square>(sq), BLACK, hkb_mode);
                add_feature_halfka(acc_b, feat_b);
            }
        }
    }
}

void HARENNEvaluator::add_feature_halfka(int32_t* acc, int feat) const {
    int max_feat = hkb_mode ? 5632 : 45056;
    if (feat < 0 || feat >= max_feat) return;
    const int16_t* w = &fc1_weights[static_cast<size_t>(feat) * 512];
    for (int i = 0; i < 512; i += 16) {
        __m128i w_lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&w[i]));
        __m128i w_hi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&w[i + 8]));
        __m256i w_lo32 = _mm256_cvtepi16_epi32(w_lo);
        __m256i w_hi32 = _mm256_cvtepi16_epi32(w_hi);
        __m256i a_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i]));
        __m256i a_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 8]));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&acc[i]), _mm256_add_epi32(a_lo, w_lo32));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&acc[i + 8]), _mm256_add_epi32(a_hi, w_hi32));
    }
}

void HARENNEvaluator::sub_feature_halfka(int32_t* acc, int feat) const {
    int max_feat = hkb_mode ? 5632 : 45056;
    if (feat < 0 || feat >= max_feat) return;
    const int16_t* w = &fc1_weights[static_cast<size_t>(feat) * 512];
    for (int i = 0; i < 512; i += 16) {
        __m128i w_lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&w[i]));
        __m128i w_hi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&w[i + 8]));
        __m256i w_lo32 = _mm256_cvtepi16_epi32(w_lo);
        __m256i w_hi32 = _mm256_cvtepi16_epi32(w_hi);
        __m256i a_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i]));
        __m256i a_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 8]));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&acc[i]), _mm256_sub_epi32(a_lo, w_lo32));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&acc[i + 8]), _mm256_sub_epi32(a_hi, w_hi32));
    }
}

void HARENNEvaluator::compute_accumulator(const Position& pos, int32_t* acc) const {
    if (!loaded) {
        std::memset(acc, 0, 1024 * sizeof(int32_t));
        return;
    }
    std::memcpy(acc, fc1_biases, 1024 * sizeof(int32_t));
    for (int sq = 0; sq < 64; ++sq) {
        Piece pc = pos.piece_on(static_cast<Square>(sq));
        if (pc != NO_PIECE) {
            Square py_sq = flip_rank(static_cast<Square>(sq));
            int feat = py_sq * 12 + static_cast<int>(pc);
            add_feature(acc, feat);
        }
    }
}

static inline int32_t hsum_epi32_8(__m256i v) {
    __m128i lo = _mm256_castsi256_si128(v);
    __m128i hi = _mm256_extracti128_si256(v, 1);
    __m128i s = _mm_add_epi32(lo, hi);
    s = _mm_hadd_epi32(s, s);
    s = _mm_hadd_epi32(s, s);
    return _mm_cvtsi128_si32(s);
}

void HARENNEvaluator::pack_accumulator(const int32_t* acc, uint8_t* h1_u8) const {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i max_val = _mm256_set1_epi32(128);
    for (int i = 0; i < 1024; i += 32) {
        __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i]));
        __m256i a1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 8]));
        __m256i a2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 16]));
        __m256i a3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 24]));

        a0 = _mm256_min_epi32(_mm256_max_epi32(a0, zero), max_val);
        a1 = _mm256_min_epi32(_mm256_max_epi32(a1, zero), max_val);
        a2 = _mm256_min_epi32(_mm256_max_epi32(a2, zero), max_val);
        a3 = _mm256_min_epi32(_mm256_max_epi32(a3, zero), max_val);

        __m256i p01 = _mm256_packs_epi32(a0, a1);
        p01 = _mm256_permute4x64_epi64(p01, 0xD8);

        __m256i p23 = _mm256_packs_epi32(a2, a3);
        p23 = _mm256_permute4x64_epi64(p23, 0xD8);

        __m256i u8 = _mm256_packus_epi16(p01, p23);
        u8 = _mm256_permute4x64_epi64(u8, 0xD8);

        _mm256_store_si256(reinterpret_cast<__m256i*>(&h1_u8[i]), u8);
    }
}

void HARENNEvaluator::pack_accumulator_512(const int32_t* acc, uint8_t* h1_u8) const {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i max_val = _mm256_set1_epi32(128);
    for (int i = 0; i < 512; i += 32) {
        __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i]));
        __m256i a1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 8]));
        __m256i a2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 16]));
        __m256i a3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[i + 24]));

        a0 = _mm256_min_epi32(_mm256_max_epi32(a0, zero), max_val);
        a1 = _mm256_min_epi32(_mm256_max_epi32(a1, zero), max_val);
        a2 = _mm256_min_epi32(_mm256_max_epi32(a2, zero), max_val);
        a3 = _mm256_min_epi32(_mm256_max_epi32(a3, zero), max_val);

        __m256i p01 = _mm256_packs_epi32(a0, a1);
        p01 = _mm256_permute4x64_epi64(p01, 0xD8);

        __m256i p23 = _mm256_packs_epi32(a2, a3);
        p23 = _mm256_permute4x64_epi64(p23, 0xD8);

        __m256i u8 = _mm256_packus_epi16(p01, p23);
        u8 = _mm256_permute4x64_epi64(u8, 0xD8);

        _mm256_store_si256(reinterpret_cast<__m256i*>(&h1_u8[i]), u8);
    }
}

void HARENNEvaluator::forward_layer2(const uint8_t* h1_u8, int16_t* h2) const {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i max_16384 = _mm256_set1_epi32(16384);
    const __m256i ones = _mm256_set1_epi16(1);

    alignas(64) int32_t sum_col[512];

    for (int j = 0; j < hidden2_dim; j += 2) {
        const int8_t* w_row0 = &fc2_int8[j * 1024];
        const int8_t* w_row1 = &fc2_int8[(j + 1) * 1024];
        __m256i acc0 = _mm256_setzero_si256();
        __m256i acc1 = _mm256_setzero_si256();

        for (int c = 0; c < 1024; c += 32) {
            __m256i h = _mm256_load_si256(reinterpret_cast<const __m256i*>(&h1_u8[c]));
            __m256i w0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&w_row0[c]));
            __m256i w1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&w_row1[c]));

            __m256i p0 = _mm256_maddubs_epi16(h, w0);
            __m256i p1 = _mm256_maddubs_epi16(h, w1);

            acc0 = _mm256_add_epi32(acc0, _mm256_madd_epi16(p0, ones));
            acc1 = _mm256_add_epi32(acc1, _mm256_madd_epi16(p1, ones));
        }

        sum_col[j] = hsum_epi32_8(acc0) * FC2_QSCALE + fc2_biases[j];
        sum_col[j + 1] = hsum_epi32_8(acc1) * FC2_QSCALE + fc2_biases[j + 1];
    }

    for (int j = 0; j < hidden2_dim; j += 16) {
        __m256i s0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&sum_col[j]));
        __m256i s1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&sum_col[j + 8]));
        s0 = _mm256_min_epi32(_mm256_max_epi32(s0, zero), max_16384);
        s1 = _mm256_min_epi32(_mm256_max_epi32(s1, zero), max_16384);
        __m256i packed = _mm256_packs_epi32(s0, s1);
        packed = _mm256_permute4x64_epi64(packed, 0b11011000);
        _mm256_store_si256(reinterpret_cast<__m256i*>(&h2[j]), packed);
    }
}

HARENNOutput HARENNEvaluator::evaluate(const int32_t* acc, Color stm) const {
    if (!loaded) {
        HARENNOutput out;
        return out;
    }

    alignas(64) uint8_t h1_u8[1024];
    if (halfka_mode) {
        const int32_t* friendly_acc = (stm == WHITE) ? &acc[0] : &acc[512];
        const int32_t* opponent_acc = (stm == WHITE) ? &acc[512] : &acc[0];
        pack_accumulator_512(friendly_acc, &h1_u8[0]);
        pack_accumulator_512(opponent_acc, &h1_u8[512]);
    } else {
        pack_accumulator(acc, h1_u8);
    }

    alignas(64) int16_t h2[512] = {0};
    forward_layer2(h1_u8, h2);

    auto compute_head = [&](const int16_t* head_w, int32_t head_b) -> double {
        __m256i sum_v = _mm256_setzero_si256();
        for (int i = 0; i < hidden2_dim; i += 16) {
            __m256i w = _mm256_load_si256(reinterpret_cast<const __m256i*>(&head_w[i]));
            __m256i in = _mm256_load_si256(reinterpret_cast<const __m256i*>(&h2[i]));
            __m256i prod = _mm256_madd_epi16(w, in);
            sum_v = _mm256_add_epi32(sum_v, prod);
        }
        int64_t total = static_cast<int64_t>(hsum256_ps_epi32(sum_v)) + head_b;
        return static_cast<double>(total) / 2097152.0;
    };

    double raw_eval = compute_head(eval_weights, eval_bias);
    double raw_tau = compute_head(tau_weights, tau_bias);
    double raw_rho = compute_head(rho_weights, rho_bias);
    double raw_rs = compute_head(rs_weights, rs_bias);

    HARENNOutput out;
    double eval_pawns = raw_eval * eval_std + eval_mean;
    double eval_cp = eval_pawns * 100.0;
    if (!halfka_mode && stm == BLACK) {
        eval_cp = -eval_cp;
    }
    out.eval_score = static_cast<Value>(std::round(eval_cp));
    out.tactical_complexity = 1.0 / (1.0 + std::exp(-raw_tau));
    out.horizon_risk = 1.0 / (1.0 + std::exp(-raw_rho));
    out.resolution_score = 1.0 / (1.0 + std::exp(-raw_rs));

    return out;
}

Value HARENNEvaluator::evaluate_eval_only(const int32_t* acc, Color stm) const {
    if (!loaded) return 0;

    alignas(64) uint8_t h1_u8[1024];
    if (halfka_mode) {
        const int32_t* friendly_acc = (stm == WHITE) ? &acc[0] : &acc[512];
        const int32_t* opponent_acc = (stm == WHITE) ? &acc[512] : &acc[0];
        pack_accumulator_512(friendly_acc, &h1_u8[0]);
        pack_accumulator_512(opponent_acc, &h1_u8[512]);
    } else {
        pack_accumulator(acc, h1_u8);
    }

    alignas(64) int16_t h2[512] = {0};
    forward_layer2(h1_u8, h2);

    __m256i sum_v = _mm256_setzero_si256();
    for (int i = 0; i < hidden2_dim; i += 16) {
        __m256i w = _mm256_load_si256(reinterpret_cast<const __m256i*>(&eval_weights[i]));
        __m256i in = _mm256_load_si256(reinterpret_cast<const __m256i*>(&h2[i]));
        __m256i prod = _mm256_madd_epi16(w, in);
        sum_v = _mm256_add_epi32(sum_v, prod);
    }
    int64_t total = static_cast<int64_t>(hsum256_ps_epi32(sum_v)) + eval_bias;
    double raw_eval = static_cast<double>(total) / 2097152.0;

    double eval_pawns = raw_eval * eval_std + eval_mean;
    double eval_cp = eval_pawns * 100.0;
    if (!halfka_mode && stm == BLACK) {
        eval_cp = -eval_cp;
    }
    return static_cast<Value>(std::round(eval_cp));
}

HARENNOutput HARENNEvaluator::evaluate(const Position& pos) const {
    if (!loaded) {
        HARENNOutput out;
        out.eval_score = evaluate_fallback(pos);
        return out;
    }

    Key k = pos.hash_key();
    size_t idx = k & (EVAL_CACHE_SIZE - 1);
    if (eval_cache && eval_cache[idx].key == k) {
        HARENNOutput out;
        out.eval_score = eval_cache[idx].eval;
        out.tactical_complexity = eval_cache[idx].tau ? (eval_cache[idx].tau / 255.0) : 0.45;
        out.horizon_risk = eval_cache[idx].rho ? (eval_cache[idx].rho / 255.0) : 0.30;
        out.resolution_score = eval_cache[idx].rs ? (eval_cache[idx].rs / 255.0) : 0.50;
        return out;
    }

    HARENNOutput out = evaluate(pos.accumulator(), pos.side_to_move());
    if (eval_cache) {
        eval_cache[idx].key = k;
        eval_cache[idx].eval = static_cast<int16_t>(out.eval_score);
        eval_cache[idx].tau = static_cast<uint8_t>(std::clamp(static_cast<int>(out.tactical_complexity * 255.0), 1, 255));
        eval_cache[idx].rho = static_cast<uint8_t>(std::clamp(static_cast<int>(out.horizon_risk * 255.0), 0, 255));
        eval_cache[idx].rs = static_cast<uint8_t>(std::clamp(static_cast<int>(out.resolution_score * 255.0), 0, 255));
    }

    return out;
}

Value HARENNEvaluator::evaluate_eval_only(const Position& pos) const {
    if (!loaded) return evaluate_fallback(pos);

    Key k = pos.hash_key();
    size_t idx = k & (EVAL_CACHE_SIZE - 1);
    if (eval_cache && eval_cache[idx].key == k) {
        return eval_cache[idx].eval;
    }

    Value eval = evaluate_eval_only(pos.accumulator(), pos.side_to_move());
    if (eval_cache) {
        eval_cache[idx].key = k;
        eval_cache[idx].eval = static_cast<int16_t>(eval);
    }

    return eval;
}

Value HARENNEvaluator::evaluate_fallback(const Position& pos) const {
    constexpr int PieceVals[] = { 100, 320, 330, 500, 900, 20000 };
    int score = 0;
    for (int sq = 0; sq < 64; ++sq) {
        Piece pc = pos.piece_on(static_cast<Square>(sq));
        if (pc != NO_PIECE) {
            int val = PieceVals[type_of(pc)];
            if (color_of(pc) == WHITE) score += val;
            else score -= val;
        }
    }
    return (pos.side_to_move() == WHITE) ? score : -score;
}

} // namespace NextFish
