#pragma once

#include "types.h"
#include "position.h"
#include <string>
#include <cmath>

namespace NextFish {

struct HARENNOutput {
    Value eval_score = 0;             // Centipawns relative to side to move
    double tactical_complexity = 0.5; // tau in [0, 1]
    double horizon_risk = 0.3;        // rho in [0, 1]
    double resolution_score = 0.5;    // rs in [0, 1]

    double get_mcs(Move m) const {
        if (!m.is_valid()) return 0.10;
        double base = 0.15;
        if (m.type() == PROMOTION) base += 0.40;
        else if (m.type() == EN_PASSANT) base += 0.35;
        return std::clamp(base * (0.6 + 0.8 * tactical_complexity), 0.0, 1.0);
    }

    double get_mcs(const Position& pos, Move m) const {
        if (!m.is_valid()) return 0.10;
        Piece p_to = pos.piece_on(m.to());
        Piece p_from = pos.piece_on(m.from());
        double base = 0.12;
        if (p_to != NO_PIECE || m.type() == EN_PASSANT) {
            base += 0.30 + 0.08 * (p_to != NO_PIECE ? static_cast<int>(type_of(p_to)) : 0);
        }
        if (p_from != NO_PIECE && type_of(p_from) == KING) base += 0.12;
        if (m.type() == PROMOTION) base += 0.35;
        return std::clamp(base * (0.6 + 0.8 * tactical_complexity), 0.0, 1.0);
    }
};

class HARENNEvaluator {
public:
    HARENNEvaluator();
    ~HARENNEvaluator();

    bool load_weights(const std::string& path);
    bool is_loaded() const { return loaded; }
    int get_hidden2() const { return hidden2_dim; }
    bool is_halfka() const { return halfka_mode; }
    bool is_hkb() const { return hkb_mode; }

    static inline int king_to_bucket(Square ksq) {
        int r = static_cast<int>(ksq) / 8;
        int f = static_cast<int>(ksq) % 8;
        if (r == 0) return (f >= 5) ? 0 : ((f <= 2) ? 1 : 2);
        if (r == 1) return (f >= 5) ? 3 : ((f <= 2) ? 4 : 5);
        if (r == 2 || r == 3) return 6;
        return 7;
    }

    // HalfKA Feature Indexer:
    // Perspective White:
    //   w_ksq (0..63)
    //   White pieces: pt 0..4 (P, N, B, R, Q)
    //   Black pieces: pt 5..9 (P, N, B, R, Q)
    //   Black King:   pt 10
    // Perspective Black:
    //   flip_rank(b_ksq) (0..63)
    //   Black pieces: pt 0..4
    //   White pieces: pt 5..9
    //   White King:   pt 10
    static inline int make_halfka_feature(Square ksq, Piece pc, Square sq, Color perspective) {
        if (perspective == WHITE) {
            Color pc_c = color_of(pc);
            PieceType pt = type_of(pc);
            int p_idx = (pc_c == WHITE) ? static_cast<int>(pt) : (static_cast<int>(pt) + 5);
            return static_cast<int>(ksq) * 704 + p_idx * 64 + static_cast<int>(sq);
        } else {
            Square ksq_flip = flip_rank(ksq);
            Color pc_c = color_of(pc);
            PieceType pt = type_of(pc);
            int p_idx = (pc_c == BLACK) ? static_cast<int>(pt) : (static_cast<int>(pt) + 5);
            return static_cast<int>(ksq_flip) * 704 + p_idx * 64 + static_cast<int>(flip_rank(sq));
        }
    }

    // Unified Feature Indexer (Supports both HalfKAv2 8-bucket and raw HalfKA 64-square)
    static inline int make_feature(Square ksq, Piece pc, Square sq, Color perspective, bool use_buckets) {
        if (!use_buckets) return make_halfka_feature(ksq, pc, sq, perspective);
        if (perspective == WHITE) {
            int b = king_to_bucket(ksq);
            Color pc_c = color_of(pc);
            PieceType pt = type_of(pc);
            int p_idx = (pc_c == WHITE) ? static_cast<int>(pt) : (static_cast<int>(pt) + 5);
            return b * 704 + p_idx * 64 + static_cast<int>(sq);
        } else {
            Square ksq_flip = flip_rank(ksq);
            int b = king_to_bucket(ksq_flip);
            Color pc_c = color_of(pc);
            PieceType pt = type_of(pc);
            int p_idx = (pc_c == BLACK) ? static_cast<int>(pt) : (static_cast<int>(pt) + 5);
            return b * 704 + p_idx * 64 + static_cast<int>(flip_rank(sq));
        }
    }

    // HalfKA Accumulator Operations
    void compute_accumulator_halfka(const Position& pos, int32_t* acc_w, int32_t* acc_b) const;
    void add_feature_halfka(int32_t* acc, int feat) const;
    void sub_feature_halfka(int32_t* acc, int feat) const;

    // Legacy 768 Accumulator support
    void compute_accumulator(const Position& pos, int32_t* acc) const;
    void add_feature(int32_t* acc, int feat) const;
    void sub_feature(int32_t* acc, int feat) const;

    // Multi-head forward inference from precomputed accumulator
    HARENNOutput evaluate(const int32_t* acc, Color stm) const;

    // Fast eval-only forward inference from precomputed accumulator
    Value evaluate_eval_only(const int32_t* acc, Color stm) const;

    // Multi-head forward inference
    HARENNOutput evaluate(const Position& pos) const;

    // Fast eval-only forward inference (for low depth or non-HARE nodes)
    Value evaluate_eval_only(const Position& pos) const;

    void clear_cache();

private:
    bool loaded = false;
    bool halfka_mode = false;
    bool hkb_mode = false;
    int hidden1_dim = 1024;
    int hidden2_dim = 512;
    float eval_mean = 0.0f;
    float eval_std = 1.0f;

    struct EvalCacheEntry {
        Key key = 0;
        int16_t eval = 0;
        uint8_t tau = 0;
        uint8_t rho = 0;
        uint8_t rs = 0;
    };
    static constexpr size_t EVAL_CACHE_SIZE = 1048576;
    mutable EvalCacheEntry* eval_cache = nullptr;

    // Weights stored in memory aligned to 64 bytes for AVX2
    int16_t* fc1_weights = nullptr; // [768][1024] or [45056][512] for HalfKA
    int32_t* fc1_biases = nullptr;  // [1024] or [512] for HalfKA

    int16_t* fc2_weights = nullptr; // [512][1024]
    int16_t* fc2_transposed = nullptr; // [1024][512] column-major for fast sparse inference
    int8_t*  fc2_int8 = nullptr;    // [512][1024] quantized weights (scale FC2_QSCALE)
    int32_t* fc2_biases = nullptr;  // [512]

    int16_t* eval_weights = nullptr; // [512]
    int32_t eval_bias = 0;

    int16_t* tau_weights = nullptr;  // [512]
    int32_t tau_bias = 0;

    int16_t* rho_weights = nullptr;  // [512]
    int32_t rho_bias = 0;

    int16_t* rs_weights = nullptr;   // [512]
    int32_t rs_bias = 0;

    // Column-major sparse inference for FC2
    void pack_accumulator(const int32_t* acc, uint8_t* h1_u8) const;
    void pack_accumulator_512(const int32_t* acc, uint8_t* h1_u8) const;
    void forward_layer2(const uint8_t* h1_u8, int16_t* h2) const;

    // Fallback evaluation if weights file is absent
    Value evaluate_fallback(const Position& pos) const;
};

extern HARENNEvaluator GlobalHARENN;

} // namespace NextFish
