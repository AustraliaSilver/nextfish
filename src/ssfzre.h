#pragma once

#include "types.h"
#include "position.h"

namespace NextFish {

struct SSFZREOutput {
    Value corrected_eval = 0;
    double fortress_prob = 0.0;
    double zugzwang_risk = 0.0;
};

class SSFZREEngine {
public:
    SSFZREEngine() = default;

    // Full static evaluation: HARENN + material blend + cheap positional corrections.
    // This is a pure function of the position (no search-path state), so it is safe
    // to cache in the transposition table.
    Value evaluate_static(const Position& pos) const;

    // Apply the structural corrections to an already blended evaluation.
    SSFZREOutput correct(const Position& pos, Value blended_eval) const;

    // Backwards-compatible wrapper. root_pawn_key / root_material_key / current_ply
    // are intentionally ignored: the correction is position-only so that TT reuse
    // cannot change the meaning of a stored evaluation.
    SSFZREOutput evaluate_and_correct(const Position& pos, Value raw_eval_cp,
                                      Key root_pawn_key, Key root_material_key,
                                      int current_ply) const;

    // Cheap, material based zugzwang danger estimate.
    double zugzwang_risk(const Position& pos) const;

    // Computes exact zugzwang risk based on safe mobility (kept for API compatibility).
    double compute_zugzwang_risk(const Position& pos) const;

private:
    double compute_barrier_cutset(const Position& pos, Color attacking_side) const;
};

extern SSFZREEngine GlobalSSFZRE;

} // namespace NextFish
