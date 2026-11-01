#pragma once

#include "types.h"
#include <cstdint>
#include <vector>

namespace NextFish {

enum BoundType : uint8_t {
    BOUND_NONE = 0,
    BOUND_UPPER = 1,
    BOUND_LOWER = 2,
    BOUND_EXACT = 3
};

#pragma pack(push, 1)
struct SemanticTTEntry {
    uint16_t key16 = 0;
    int16_t score16 = 0;
    int16_t eval16 = 0;
    uint16_t best_move16 = 0;
    int8_t depth8 = -1;
    uint8_t gen_bound_pv8 = 0;
    uint8_t tau8 = 0;
    uint8_t rho8 = 0;
    uint8_t rs8 = 0;
    uint8_t work_log8 = 0;
    uint8_t topo_class8 = 0;
    uint8_t rule50_bucket8 = 0;

    int generation() const { return (gen_bound_pv8 >> 3) & 0x1F; }
    BoundType bound_type() const { return static_cast<BoundType>((gen_bound_pv8 >> 1) & 0x03); }
    bool is_pv() const { return (gen_bound_pv8 & 0x01) != 0; }
    Move move() const { return Move(best_move16); }
};
#pragma pack(pop)
static_assert(sizeof(SemanticTTEntry) == 16, "SemanticTTEntry must be exactly 16 bytes");

struct alignas(64) SMTTACluster {
    SemanticTTEntry slots[4];
};
static_assert(sizeof(SMTTACluster) == 64, "SMTTACluster must be exactly 64 bytes");

struct CachedNeuralState {
    Value eval_cp = 0;
    double tau = 0.5;
    double rho = 0.3;
    double rs = 0.5;
    int topo_class = 0;
    Move best_move = MOVE_NONE;
    bool valid = false;
};

struct SMTTAProbeResult {
    bool cutoff = false;
    Value score = 0;
    Move best_move = MOVE_NONE;
    int8_t depth = -1;
    BoundType bound = BOUND_NONE;
    CachedNeuralState cached_neural;
};

class SemanticMultiTierTT {
public:
    SemanticMultiTierTT();
    ~SemanticMultiTierTT();

    void resize(size_t mb_size);
    void clear();
    void new_search();

    SMTTAProbeResult probe(Key key, int depth, Value alpha, Value beta,
                           int ply, int halfmove_clock, bool has_repeated);

    void store(Key key, Value score, Value static_eval, Move best_move,
               int depth, BoundType bound, bool is_pv, int ply,
               uint64_t subtree_nodes, double tau, double rho, double rs,
               int topo_class, int halfmove_clock, bool path_dependent_draw);

    static Value score_to_tt(Value v, int ply) {
        if (v >= VALUE_MATE_THRESHOLD) return v + ply;
        if (v <= -VALUE_MATE_THRESHOLD) return v - ply;
        return v;
    }

    static Value score_from_tt(Value v, int ply) {
        if (v >= VALUE_MATE_THRESHOLD) return v - ply;
        if (v <= -VALUE_MATE_THRESHOLD) return v + ply;
        return v;
    }

private:
    int compute_utility(const SemanticTTEntry& entry) const;

    SMTTACluster* table = nullptr;
    size_t cluster_count = 0;
    size_t cluster_mask = 0;
    uint8_t current_gen = 0;
};

extern SemanticMultiTierTT GlobalTT;

} // namespace NextFish
