#include <iostream>
#include <chrono>
#include <immintrin.h>
#include <cstring>
#include <algorithm>

alignas(64) int16_t h1[1024];
alignas(64) int16_t fc2_weights[512 * 1024];
alignas(64) int32_t fc2_biases[512];
alignas(64) int16_t h2[512];

__attribute__((always_inline)) static inline int32_t hsum256(const __m256i& v) {
    __m128i vlow = _mm256_castsi256_si128(v);
    __m128i vhigh = _mm256_extracti128_si256(v, 1);
    __m128i sum128 = _mm_add_epi32(vlow, vhigh);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    return _mm_cvtsi128_si32(sum128);
}

void forward_madd(const int16_t* in, int16_t* out) {
    for (int r = 0; r < 512; ++r) {
        const int16_t* w_row = &fc2_weights[r * 1024];
        __m256i sum_v = _mm256_setzero_si256();
        for (int c = 0; c < 1024; c += 16) {
            __m256i in_v = _mm256_load_si256(reinterpret_cast<const __m256i*>(&in[c]));
            __m256i w_v  = _mm256_load_si256(reinterpret_cast<const __m256i*>(&w_row[c]));
            __m256i prod = _mm256_madd_epi16(in_v, w_v);
            sum_v = _mm256_add_epi32(sum_v, prod);
        }
        int32_t total = hsum256(sum_v) + fc2_biases[r];
        int32_t clamped = std::max(0, std::min(16384, total));
        out[r] = static_cast<int16_t>(clamped);
    }
}

int main() {
    for (int i = 0; i < 1024; ++i) h1[i] = (i % 6 == 0) ? (i % 120) : 0;
    for (int i = 0; i < 512 * 1024; ++i) fc2_weights[i] = static_cast<int16_t>(i % 31 - 15);
    for (int i = 0; i < 512; ++i) fc2_biases[i] = 100;

    auto t0 = std::chrono::high_resolution_clock::now();
    int N = 50000;
    for (int it = 0; it < N; ++it) {
        forward_madd(h1, h2);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
    std::cout << N << " forward passes in " << ms << " ms -> " << (N * 1000.0 / ms) << " passes/sec" << std::endl;
    std::cout << "Per-pass latency: " << (ms * 1000.0 / N) << " microseconds" << std::endl;
    return 0;
}
