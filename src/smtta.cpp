#include "smtta.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace NextFish {

SemanticMultiTierTT GlobalTT;

static void* aligned_alloc_tt(size_t size) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    return _aligned_malloc(size, 64);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, size) != 0) return nullptr;
    return ptr;
#endif
}

static void aligned_free_tt(void* ptr) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

SemanticMultiTierTT::SemanticMultiTierTT() {
    resize(64); // Default 64 MB
}

SemanticMultiTierTT::~SemanticMultiTierTT() {
    if (table) aligned_free_tt(table);
}

void SemanticMultiTierTT::resize(size_t mb_size) {
    if (table) {
        aligned_free_tt(table);
        table = nullptr;
    }

    size_t total_bytes = mb_size * 1024 * 1024;
    size_t num_clusters = 1;
    while ((num_clusters << 1) * sizeof(SMTTACluster) <= total_bytes) {
        num_clusters <<= 1;
    }

    cluster_count = num_clusters;
    cluster_mask = cluster_count - 1;

    table = static_cast<SMTTACluster*>(aligned_alloc_tt(cluster_count * sizeof(SMTTACluster)));
    clear();
}

void SemanticMultiTierTT::clear() {
    if (table) {
        std::memset(table, 0, cluster_count * sizeof(SMTTACluster));
        for (size_t i = 0; i < cluster_count; ++i) {
            for (int s = 0; s < 4; ++s) {
                table[i].slots[s].depth8 = -1;
            }
        }
    }
    current_gen = 0;
}

void SemanticMultiTierTT::new_search() {
    current_gen = (current_gen + 1) & 0x1F;
}

int SemanticMultiTierTT::compute_utility(const SemanticTTEntry& entry) const {
    if (entry.depth8 == -1) return -99999;
    int age_diff = (current_gen - entry.generation()) & 0x1F;
    int exact_bonus = (entry.bound_type() == BOUND_EXACT) ? 128 : 0;
    int tau_rho = ((192 * (entry.tau8 + entry.rho8)) >> 8);
    return 32 * entry.depth8 + entry.work_log8 + tau_rho + exact_bonus - 96 * age_diff;
}

SMTTAProbeResult SemanticMultiTierTT::probe(Key key, int depth, Value alpha, Value beta,
                                           int ply, int halfmove_clock, bool has_repeated) {
    SMTTAProbeResult res;
    if (!table) return res;

    size_t cluster_idx = key & cluster_mask;
    SMTTACluster& cluster = table[cluster_idx];
    uint16_t key16 = static_cast<uint16_t>((key >> 48) & 0xFFFF);

    int best_slot = -1;
    int max_depth = -2;

    for (int slot_idx = 0; slot_idx < 4; ++slot_idx) {
        SemanticTTEntry& entry = cluster.slots[slot_idx];
        if (entry.depth8 != -1 && entry.key16 == key16) {
            if (entry.depth8 > max_depth) {
                max_depth = entry.depth8;
                best_slot = slot_idx;
            }
        }
    }

    if (best_slot != -1) {
        SemanticTTEntry& entry = cluster.slots[best_slot];
        // Unpack neural and topological cache
        res.cached_neural.eval_cp = entry.eval16;
        constexpr double INV_255 = 1.0 / 255.0;
        res.cached_neural.tau = entry.tau8 * INV_255;
        res.cached_neural.rho = entry.rho8 * INV_255;
        res.cached_neural.rs = entry.rs8 * INV_255;
        res.cached_neural.topo_class = (entry.topo_class8 >> 4) & 0x0F;
        res.cached_neural.best_move = entry.move();
        res.cached_neural.valid = true;

        res.best_move = entry.move();
        res.depth = entry.depth8;
        res.bound = entry.bound_type();
        res.score = score_from_tt(entry.score16, ply);

        // Check GHI and 50-move rule safety
        int stored_hmc = (entry.rule50_bucket8 >> 1) & 0x7F;
        bool was_path_draw = (entry.rule50_bucket8 & 0x01) != 0;

        if (was_path_draw && entry.score16 == 0 && !has_repeated) {
            return res; // Reject draw score on non-repeated path
        }
        if (std::max(stored_hmc, halfmove_clock) + depth >= 96 && std::abs(stored_hmc - halfmove_clock) > 4) {
            return res; // Reject near fifty-move boundary
        }

        // Alpha-Beta cutoff checks
        if (entry.depth8 >= depth) {
            Value score = res.score;
            BoundType b_type = entry.bound_type();

            if (b_type == BOUND_EXACT) {
                res.cutoff = true;
                return res;
            } else if (b_type == BOUND_LOWER && score >= beta) {
                res.cutoff = true;
                return res;
            } else if (b_type == BOUND_UPPER && score <= alpha) {
                res.cutoff = true;
                return res;
            }
        }

        return res;
    }

    return res;
}

void SemanticMultiTierTT::store(Key key, Value score, Value static_eval, Move best_move,
                                int depth, BoundType bound, bool is_pv, int ply,
                                uint64_t subtree_nodes, double tau, double rho, double rs,
                                int topo_class, int halfmove_clock, bool path_dependent_draw) {
    if (!table) return;

    size_t cluster_idx = key & cluster_mask;
    SMTTACluster& cluster = table[cluster_idx];
    uint16_t key16 = static_cast<uint16_t>((key >> 48) & 0xFFFF);

    uint8_t work_log = 0;
    if (subtree_nodes > 0) {
        int lz = __builtin_clzll(subtree_nodes);
        work_log = static_cast<uint8_t>(std::min(255, (64 - lz) * 16));
    }
    bool qualifies_for_pv = is_pv || (bound == BOUND_EXACT && work_log >= 128);

    SemanticTTEntry* target = nullptr;

    // 1. If key already exists in ANY slot (0..3), examine that entry
    for (int i = 0; i < 4; ++i) {
        if (cluster.slots[i].depth8 != -1 && cluster.slots[i].key16 == key16) {
            target = &cluster.slots[i];
            break;
        }
    }

    if (target && target->key16 == key16) {
        // Entry already exists for this position!
        // Never overwrite a deeper search from current generation with a shallower search (e.g. depth 0 QSearch)
        if (target->depth8 > depth && target->generation() == (current_gen & 0x1F)) {
            // Preserve deeper search; update best move if existing lacks one
            if (best_move.is_valid() && !target->move().is_valid()) {
                target->best_move16 = best_move.data;
            }
            if (static_eval != 0 && target->eval16 == 0) {
                target->eval16 = static_cast<int16_t>(static_eval);
            }
            return;
        }
    }

    // 2. If not found, look for an empty slot (0..3)
    if (!target) {
        for (int i = 0; i < 4; ++i) {
            if (cluster.slots[i].depth8 == -1) {
                target = &cluster.slots[i];
                break;
            }
        }
    }

    // 3. If all slots full, replace the entry with the lowest utility
    if (!target) {
        int worst_idx = qualifies_for_pv ? 0 : 1;
        int worst_util = compute_utility(cluster.slots[worst_idx]);
        for (int i = 0; i < 4; ++i) {
            int u = compute_utility(cluster.slots[i]);
            if (u < worst_util) {
                worst_util = u;
                worst_idx = i;
            }
        }
        // Protect valuable deep entries from being evicted by depth <= 1 leaves
        if (depth <= 1 && cluster.slots[worst_idx].generation() == (current_gen & 0x1F) && cluster.slots[worst_idx].depth8 >= 4) {
            return;
        }
        target = &cluster.slots[worst_idx];
    }

    // Preserve previous best move if new store has no best move
    if (!best_move.is_valid() && target->key16 == key16) {
        best_move = target->move();
    }

    target->key16 = key16;
    target->score16 = static_cast<int16_t>(score_to_tt(score, ply));
    target->eval16 = static_cast<int16_t>(std::clamp(static_cast<int>(static_eval), -30000, 30000));
    target->best_move16 = best_move.data;
    target->depth8 = static_cast<int8_t>(std::clamp(depth, 0, 127));
    target->gen_bound_pv8 = static_cast<uint8_t>(((current_gen & 0x1F) << 3) | ((bound & 0x03) << 1) | (is_pv ? 1 : 0));

    target->tau8 = static_cast<uint8_t>(std::clamp(static_cast<int>(tau * 255.0), 0, 255));
    target->rho8 = static_cast<uint8_t>(std::clamp(static_cast<int>(rho * 255.0), 0, 255));
    target->rs8 = static_cast<uint8_t>(std::clamp(static_cast<int>(rs * 255.0), 0, 255));

    target->work_log8 = work_log;
    target->topo_class8 = static_cast<uint8_t>((topo_class & 0x0F) << 4);
    target->rule50_bucket8 = static_cast<uint8_t>(((halfmove_clock & 0x7F) << 1) | (path_dependent_draw ? 1 : 0));
}

} // namespace NextFish
